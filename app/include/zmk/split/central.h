/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/bluetooth/addr.h>
#include <zmk/behavior.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)

#include <zmk/ble.h>
#define BLE_PERIPHERAL_COUNT ZMK_SPLIT_BLE_PERIPHERAL_COUNT

#else

#define BLE_PERIPHERAL_COUNT 0

#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_WIRED)
#define WIRED_PERIPHERAL_COUNT 1
#else
#define WIRED_PERIPHERAL_COUNT 0
#endif

#define ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT MAX(BLE_PERIPHERAL_COUNT, WIRED_PERIPHERAL_COUNT)

#if IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS)
#include <zmk/hid_indicators_types.h>
#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS)

int zmk_split_central_invoke_behavior(uint8_t source, struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event, bool state);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS)

int zmk_split_central_update_hid_indicator(zmk_hid_indicators_t indicators);

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS)

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)

int zmk_split_central_get_peripheral_battery_level(uint8_t source, uint8_t *level);

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_MIRROR)

/* Encoded: bits 0..6 = level, bit 7 = USB powered. */
int zmk_split_central_update_central_battery(uint8_t state);

/* Invalidate the outgoing-state cache and re-send. Call this when a
 * peripheral's central-battery handle has just been discovered so the
 * peripheral doesn't stay stuck on "X" waiting for the next battery
 * event (which can be minutes away on a steady charge). */
void zmk_split_central_resend_central_battery_state(void);

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_MIRROR)

#if IS_ENABLED(CONFIG_ZMK_SPLIT_CENTRAL_STATUS_MIRROR)

/* Push the central's keymap status to peripherals. flags: bit0 caps lock,
 * bit1 active profile connected, bit2 selected endpoint is USB. */
int zmk_split_central_update_central_status(uint8_t layer, uint8_t profile,
                                            uint8_t profile_bonded, uint8_t wpm, uint8_t flags);

/* Invalidate the outgoing-status cache and re-send. Call when a peripheral's
 * central-status handle is freshly discovered so it doesn't wait for the next
 * status change. */
void zmk_split_central_resend_central_status(void);

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_CENTRAL_STATUS_MIRROR)

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_SMART_IDLE_SYNC)

/* Central-side: write the central's own smart-idle state to all
 * connected peripherals. State byte encoding:
 *   bit 0 = ACTIVE flag (1 = central is currently active)
 *   bit 1 = BATTERY_BELOW_CUTOFF flag
 * Peripherals raise zmk_split_remote_smart_idle_state_changed when the
 * write arrives. */
int zmk_split_central_set_central_smart_idle_state(uint8_t state);

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_SMART_IDLE_SYNC)
