/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Forwards the central's keymap status (active layer, BLE profile, WPM,
 * caps lock, selected endpoint) to all connected peripherals over the split
 * transport, so a peripheral-side widget can render the central's status
 * screen when the central has no display of its own (e.g. a central-mounted
 * trackpad reusing the display pins). Only the layer index is sent; the
 * peripheral resolves the name from its own copy of the keymap.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>
#include <zmk/hid_indicators.h>
#include <zmk/split/central.h>

#include <zmk/events/layer_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/hid_indicators_changed.h>

#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_WPM)
#include <zmk/wpm.h>
#include <zmk/events/wpm_state_changed.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define FLAG_CAPS 0x01
#define FLAG_ACTIVE_CONNECTED 0x02
#define FLAG_ENDPOINT_USB 0x04

static uint32_t last_sent = 0xFFFFFFFF;

static void mirror_central_status(void) {
    uint8_t layer = (uint8_t)zmk_keymap_highest_layer_active();
    uint8_t profile = 0;
    uint8_t flags = 0;

#if IS_ENABLED(CONFIG_ZMK_BLE)
    profile = (uint8_t)zmk_ble_active_profile_index();
    if (zmk_ble_active_profile_is_connected()) {
        flags |= FLAG_ACTIVE_CONNECTED;
    }
#endif

    struct zmk_endpoint_instance ep = zmk_endpoints_selected();
    if (ep.transport == ZMK_TRANSPORT_USB) {
        flags |= FLAG_ENDPOINT_USB;
    }

    uint8_t wpm = 0;
#if IS_ENABLED(CONFIG_ZMK_WPM)
    int w = zmk_wpm_get_state();
    wpm = (w < 0) ? 0 : (w > 255 ? 255 : (uint8_t)w);
#endif

    if ((zmk_hid_indicators_get_current_profile() & 0x02) != 0) {
        flags |= FLAG_CAPS;
    }

    uint32_t packed = ((uint32_t)layer) | ((uint32_t)profile << 8) | ((uint32_t)wpm << 16) |
                      ((uint32_t)flags << 24);
    if (packed == last_sent) {
        return;
    }
    last_sent = packed;

    int ret = zmk_split_central_update_central_status(layer, profile, 0, wpm, flags);
    if (ret < 0) {
        LOG_DBG("Unable to mirror central status (%d); will retry on next event", ret);
        last_sent = 0xFFFFFFFF;
    }
}

static void resend_work_cb(struct k_work *work) { mirror_central_status(); }

static K_WORK_DELAYABLE_DEFINE(resend_work, resend_work_cb);

/* Called from the split transport when a peripheral's central-status handle
 * is freshly discovered, so it doesn't wait for the next status change. */
void zmk_split_central_resend_central_status(void) {
    last_sent = 0xFFFFFFFF;
    k_work_reschedule(&resend_work, K_MSEC(500));
}

static int central_status_sync_cb(const zmk_event_t *eh) {
    mirror_central_status();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(central_status_sync, central_status_sync_cb);
ZMK_SUBSCRIPTION(central_status_sync, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(central_status_sync, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(central_status_sync, zmk_hid_indicators_changed);
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(central_status_sync, zmk_ble_active_profile_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_WPM)
ZMK_SUBSCRIPTION(central_status_sync, zmk_wpm_state_changed);
#endif
