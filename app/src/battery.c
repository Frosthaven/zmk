/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/battery.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/activity.h>
#include <zmk/workqueue.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) &&                  \
    IS_ENABLED(CONFIG_BT_BAS) && IS_ENABLED(CONFIG_NRFX_POWER)
#define ZMK_BATTERY_ENCODE_PERIPHERAL_CHARGING 1
#include <hal/nrf_power.h>
#include <nrfx_power.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif

// On USB drop, schedule a single delayed re-poll so the cell has had a moment
// to start relaxing from charging voltage before we sample. 5s is short enough
// that the user barely notices the "stale" UI placeholder, but the reading
// won't be perfectly settled (5-10% inflation possible). The next regular 60s
// poll refines it further.
#if defined(ZMK_BATTERY_ENCODE_PERIPHERAL_CHARGING) || IS_ENABLED(CONFIG_ZMK_USB)
#define ZMK_BATTERY_RELAX_AFTER_UNPLUG 1
static void battery_relax_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(battery_relax_work, battery_relax_work_handler);
#endif

static uint8_t last_state_of_charge = 0;

uint8_t zmk_battery_state_of_charge(void) { return last_state_of_charge; }

#if IS_ENABLED(CONFIG_ZMK_BATTERY_MONOTONIC_REPORTING)
/* Cached lowest reading since the last USB plug-in. 0xFF = no reading
 * yet, accept whatever the next sample produces. Reset to 0xFF when
 * USB is connected so the next discharge cycle starts fresh. */
static uint8_t battery_monotonic_floor = 0xFF;

static inline bool battery_usb_is_currently_powered(void) {
#if defined(ZMK_BATTERY_ENCODE_PERIPHERAL_CHARGING)
    return nrf_power_usbregstatus_vbusdet_get(NRF_POWER);
#elif IS_ENABLED(CONFIG_ZMK_USB)
    return zmk_usb_is_powered();
#else
    return false;
#endif
}

static uint8_t battery_apply_monotonic(uint8_t raw_pct) {
    if (battery_usb_is_currently_powered()) {
        /* Charging or plugged in - allow upward changes and reset the
         * floor so the next discharge starts at the current reading. */
        battery_monotonic_floor = raw_pct;
        return raw_pct;
    }
    if (battery_monotonic_floor == 0xFF || raw_pct < battery_monotonic_floor) {
        battery_monotonic_floor = raw_pct;
    }
    return battery_monotonic_floor;
}

/* Reset the floor as soon as USB plug-in is detected (peripheral side
 * calls this from the nrfx_power ISR helper; central side wires a ZMK
 * event listener below). Without an explicit reset the floor would
 * stay stale until the next periodic battery poll - up to a minute on
 * default settings - which could clamp a freshly-charged level back
 * down to the pre-charge value. */
static inline void battery_reset_monotonic_floor(void) {
    battery_monotonic_floor = 0xFF;
}
#endif

#if IS_ENABLED(CONFIG_BT_BAS)
// Push last_state_of_charge to the BAS characteristic. On peripheral builds,
// bit 7 of the transmitted byte carries the USB-powered flag so the central
// can decode charging state. Relies on the forked Zephyr BAS, which accepts
// any uint8_t value (upstream rejects > 100).
static int zmk_battery_publish_bas(void) {
    uint8_t bas_level = last_state_of_charge;
#if defined(ZMK_BATTERY_ENCODE_PERIPHERAL_CHARGING)
    if (nrf_power_usbregstatus_vbusdet_get(NRF_POWER)) {
        bas_level |= 0x80;
    }
#endif
    if (bt_bas_get_battery_level() == bas_level) {
        return 0;
    }
    LOG_DBG("Setting BAS GATT battery level to %d.", bas_level);
    int rc = bt_bas_set_battery_level(bas_level);
    if (rc != 0) {
        LOG_WRN("Failed to set BAS GATT battery level (err %d)", rc);
    }
    return rc;
}
#endif

#if defined(ZMK_BATTERY_ENCODE_PERIPHERAL_CHARGING)
// Forward BAS publish + local widget refresh to the low-prio workqueue; called
// from the POWER ISR so the work must be deferred out of interrupt context.
static void peripheral_usb_publish_work_handler(struct k_work *work) {
    (void)zmk_battery_publish_bas();
    // Raise a battery-state-changed event so the peripheral's own display
    // widget (which subscribes to this event) re-reads VBUS and repaints.
    (void)raise_zmk_battery_state_changed(
        (struct zmk_battery_state_changed){.state_of_charge = last_state_of_charge});
}
K_WORK_DEFINE(peripheral_usb_publish_work, peripheral_usb_publish_work_handler);

static void peripheral_usb_evt_handler(nrfx_power_usb_evt_t event) {
    if (event == NRFX_POWER_USB_EVT_REMOVED) {
        k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &battery_relax_work,
                                  K_SECONDS(5));
    } else if (event == NRFX_POWER_USB_EVT_DETECTED) {
        k_work_cancel_delayable(&battery_relax_work);
#if IS_ENABLED(CONFIG_ZMK_BATTERY_MONOTONIC_REPORTING)
        battery_reset_monotonic_floor();
#endif
    }
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &peripheral_usb_publish_work);
}

static int peripheral_usb_evt_init(void) {
    nrfx_power_config_t pwr_config = {0};
    nrfx_err_t rc = nrfx_power_init(&pwr_config);
    if (rc != NRFX_SUCCESS && rc != NRFX_ERROR_ALREADY_INITIALIZED) {
        LOG_WRN("nrfx_power_init failed: 0x%x", rc);
        return -EIO;
    }
    nrfx_power_usbevt_config_t usb_config = {
        .handler = peripheral_usb_evt_handler,
    };
    nrfx_power_usbevt_init(&usb_config);
    nrfx_power_usbevt_enable();
    return 0;
}
SYS_INIT(peripheral_usb_evt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif

#if DT_HAS_CHOSEN(zmk_battery)
static const struct device *const battery = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));
#else
#warning                                                                                           \
    "Using a node labeled BATTERY for the battery sensor is deprecated. Set a zmk,battery chosen node instead. (Ignore this if you don't have a battery sensor.)"
static const struct device *battery;
#endif

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING_FETCH_MODE_LITHIUM_VOLTAGE)
static uint8_t lithium_ion_mv_to_pct(int16_t bat_mv) {
    // Simple linear approximation of a battery based off adafruit's discharge graph:
    // https://learn.adafruit.com/li-ion-and-lipoly-batteries/voltages

    if (bat_mv >= 4200) {
        return 100;
    } else if (bat_mv <= 3450) {
        return 0;
    }

    return bat_mv * 2 / 15 - 459;
}

#endif // IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING_FETCH_MODE_LITHIUM_VOLTAGE)

static int zmk_battery_update(const struct device *battery) {
    struct sensor_value state_of_charge;
    int rc;

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING_FETCH_MODE_STATE_OF_CHARGE)

    rc = sensor_sample_fetch_chan(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE);
    if (rc != 0) {
        LOG_DBG("Failed to fetch battery values: %d", rc);
        return rc;
    }

    rc = sensor_channel_get(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &state_of_charge);

    if (rc != 0) {
        LOG_DBG("Failed to get battery state of charge: %d", rc);
        return rc;
    }
#elif IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING_FETCH_MODE_LITHIUM_VOLTAGE)
    rc = sensor_sample_fetch_chan(battery, SENSOR_CHAN_VOLTAGE);
    if (rc != 0) {
        LOG_DBG("Failed to fetch battery values: %d", rc);
        return rc;
    }

    struct sensor_value voltage;
    rc = sensor_channel_get(battery, SENSOR_CHAN_VOLTAGE, &voltage);

    if (rc != 0) {
        LOG_DBG("Failed to get battery voltage: %d", rc);
        return rc;
    }

    uint16_t mv = voltage.val1 * 1000 + (voltage.val2 / 1000);
    state_of_charge.val1 = lithium_ion_mv_to_pct(mv);

    LOG_DBG("State of change %d from %d mv", state_of_charge.val1, mv);
#else
#error "Not a supported reporting fetch mode"
#endif

#if IS_ENABLED(CONFIG_ZMK_BATTERY_MONOTONIC_REPORTING)
    state_of_charge.val1 = battery_apply_monotonic((uint8_t)state_of_charge.val1);
#endif

    if (last_state_of_charge != state_of_charge.val1) {
        last_state_of_charge = state_of_charge.val1;

        rc = raise_zmk_battery_state_changed(
            (struct zmk_battery_state_changed){.state_of_charge = last_state_of_charge});

        if (rc != 0) {
            LOG_ERR("Failed to raise battery state changed event: %d", rc);
            return rc;
        }
    }

#if IS_ENABLED(CONFIG_BT_BAS)
    rc = zmk_battery_publish_bas();
    if (rc != 0) {
        return rc;
    }
#endif

    return rc;
}

static void zmk_battery_work(struct k_work *work) {
    int rc = zmk_battery_update(battery);

    if (rc != 0) {
        LOG_DBG("Failed to update battery value: %d.", rc);
    }
}

K_WORK_DEFINE(battery_work, zmk_battery_work);

#if defined(ZMK_BATTERY_RELAX_AFTER_UNPLUG)
static void battery_relax_work_handler(struct k_work *work) {
    // Read the (now slightly relaxed) ADC value. May or may not change
    // last_state_of_charge.
    (void)zmk_battery_update(battery);

#if defined(ZMK_BATTERY_ENCODE_PERIPHERAL_CHARGING)
    // Force-notify BAS so the central sees a peripheral-battery event even
    // when the level didn't actually change. Bypasses the dedup in
    // zmk_battery_publish_bas (which would no-op an unchanged value).
    uint8_t bas_level = last_state_of_charge;
    if (nrf_power_usbregstatus_vbusdet_get(NRF_POWER)) {
        bas_level |= 0x80;
    }
    (void)bt_bas_set_battery_level(bas_level);
#endif

    // Always raise locally so the widget can clear its "stale" placeholder
    // even when the level happens to be unchanged.
    (void)raise_zmk_battery_state_changed(
        (struct zmk_battery_state_changed){.state_of_charge = last_state_of_charge});
}
#endif

static void zmk_battery_timer(struct k_timer *timer) {
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &battery_work);
}

K_TIMER_DEFINE(battery_timer, zmk_battery_timer, NULL);

static void zmk_battery_start_reporting() {
    if (device_is_ready(battery)) {
        k_timer_start(&battery_timer, K_NO_WAIT, K_SECONDS(CONFIG_ZMK_BATTERY_REPORT_INTERVAL));
    }
}

static int zmk_battery_init(void) {
#if !DT_HAS_CHOSEN(zmk_battery)
    battery = device_get_binding("BATTERY");

    if (battery == NULL) {
        return -ENODEV;
    }

    LOG_WRN("Finding battery device labeled BATTERY is deprecated. Use zmk,battery chosen node.");
#endif

    if (!device_is_ready(battery)) {
        LOG_ERR("Battery device \"%s\" is not ready", battery->name);
        return -ENODEV;
    }

    zmk_battery_start_reporting();

#if defined(ZMK_BATTERY_RELAX_AFTER_UNPLUG)
    /* Cold-boot transient: the very first poll happens before RGB / BLE /
     * display have ramped up, so the ADC reads the cell's relaxed
     * open-circuit voltage and the widget renders an inflated %.
     * Schedule a fresh poll a few seconds out so the load is fully
     * ramped by the time we re-sample - the monotonic floor then lands
     * on a realistic loaded value and the widget snaps to it. Reuses
     * the existing relax handler that already covers the post-USB-
     * unplug version of the same artifact. */
    k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &battery_relax_work,
                              K_SECONDS(5));
#endif

    return 0;
}

static int battery_event_listener(const zmk_event_t *eh) {

    if (as_zmk_activity_state_changed(eh)) {
        switch (zmk_activity_get_state()) {
        case ZMK_ACTIVITY_ACTIVE:
            zmk_battery_start_reporting();
            return 0;
        case ZMK_ACTIVITY_IDLE:
        case ZMK_ACTIVITY_SLEEP:
            k_timer_stop(&battery_timer);
            return 0;
        default:
            break;
        }
    }
#if IS_ENABLED(CONFIG_ZMK_USB)
    if (as_zmk_usb_conn_state_changed(eh)) {
        if (zmk_usb_is_powered()) {
            k_work_cancel_delayable(&battery_relax_work);
#if IS_ENABLED(CONFIG_ZMK_BATTERY_MONOTONIC_REPORTING)
            battery_reset_monotonic_floor();
#endif
        } else {
            k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &battery_relax_work,
                                      K_SECONDS(5));
        }
        return 0;
    }
#endif
    return -ENOTSUP;
}

ZMK_LISTENER(battery, battery_event_listener);

ZMK_SUBSCRIPTION(battery, zmk_activity_state_changed);
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(battery, zmk_usb_conn_state_changed);
#endif

SYS_INIT(zmk_battery_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
