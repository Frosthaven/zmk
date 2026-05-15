/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

/* Raised after the RGB underglow runtime state mutates (via
 * zmk_rgb_underglow_set_hsb() / on / off helpers). Subscribers can read
 * back the current state through the underglow APIs; this event is
 * intentionally minimal so additional fields (e.g. effect, speed) can
 * be added later without breaking ABI. */
struct zmk_rgb_underglow_state_changed {
    bool on;
    uint8_t brightness;
};

ZMK_EVENT_DECLARE(zmk_rgb_underglow_state_changed);
