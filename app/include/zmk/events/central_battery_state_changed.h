/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

/* Raised on the peripheral half when the central writes its own battery
 * state. The byte encoding matches the BAS high-bit scheme used in the
 * other direction: bit 7 = USB charging, bits 0..6 = level (0..100).
 */
struct zmk_central_battery_state_changed {
    uint8_t state_of_charge;
    bool usb_powered;
};

ZMK_EVENT_DECLARE(zmk_central_battery_state_changed);
