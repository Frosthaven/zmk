/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

/* Raised on the peripheral half when the central writes its keymap status.
 * Lets a peripheral-side widget render the central's status screen (used
 * when the central hosts a trackpad and has no display of its own). The
 * layer name is resolved on the peripheral from its own keymap using the
 * forwarded index, so only the index travels over the link.
 */
struct zmk_central_status_changed {
    uint8_t layer;          // highest active layer index
    uint8_t profile;        // active BLE profile index
    uint8_t profile_bonded; // bitmap: bit i set = profile i bonded
    uint8_t wpm;            // current words-per-minute
    bool caps_lock;
    bool active_profile_connected;
    bool endpoint_usb; // selected endpoint is USB (else BLE)
};

ZMK_EVENT_DECLARE(zmk_central_status_changed);
