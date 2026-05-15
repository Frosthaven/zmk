/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

/* Raised on either half when the other half writes/notifies its
 * smart-idle state byte over the split transport. State byte encoding:
 * bit 0 = ACTIVE flag (1 if the remote half is currently active), bit 1
 * = BATTERY_BELOW_CUTOFF flag (1 if the remote half is below its
 * configured cutoff threshold). External modules subscribe to this
 * event to apply cross-half policies (e.g. either-active = both active
 * for idle fade; either-below-cutoff = both off).
 */
struct zmk_split_remote_smart_idle_state_changed {
    bool active;
    bool battery_below_cutoff;
};

ZMK_EVENT_DECLARE(zmk_split_remote_smart_idle_state_changed);
