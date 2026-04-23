/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Forwards the central's own battery level + USB state to all connected
 * peripherals over the split transport, so a peripheral-side widget can
 * render both halves in one battery row.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/battery.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/central.h>

#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static uint8_t last_sent_state = 0xFF;

static void mirror_central_battery_state(void) {
    uint8_t level = zmk_battery_state_of_charge() & 0x7F;
    uint8_t state = level;
#if IS_ENABLED(CONFIG_ZMK_USB)
    if (zmk_usb_is_powered()) {
        state |= 0x80;
    }
#endif

    if (state == last_sent_state) {
        return;
    }
    last_sent_state = state;

    int ret = zmk_split_central_update_central_battery(state);
    if (ret < 0) {
        LOG_DBG("Unable to mirror central battery (%d); will retry on next event", ret);
        last_sent_state = 0xFF;
    }
}

static void resend_work_cb(struct k_work *work) { mirror_central_battery_state(); }

static K_WORK_DELAYABLE_DEFINE(resend_work, resend_work_cb);

/* Called from the split transport when a peripheral's central-battery
 * characteristic handle has just been discovered. Battery events fire
 * rarely (only on SoC change), so without this the peripheral would
 * stay stuck on "X" until the central's level moved — which can take
 * tens of minutes on a steady charge. Invalidate the dedup cache and
 * schedule a fresh write; the small delay lets the rest of service
 * discovery finish settling first. */
void zmk_split_central_resend_central_battery_state(void) {
    last_sent_state = 0xFF;
    k_work_reschedule(&resend_work, K_MSEC(500));
}

static int central_battery_sync_cb(const zmk_event_t *eh) {
    mirror_central_battery_state();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(central_battery_sync, central_battery_sync_cb);
ZMK_SUBSCRIPTION(central_battery_sync, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(central_battery_sync, zmk_usb_conn_state_changed);
#endif
