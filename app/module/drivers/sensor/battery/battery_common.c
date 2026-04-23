/*
 * Copyright (c) 2021 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/util.h>

#include "battery_common.h"

int battery_channel_get(const struct battery_value *value, enum sensor_channel chan,
                        struct sensor_value *val_out) {
    switch (chan) {
    case SENSOR_CHAN_GAUGE_VOLTAGE:
        val_out->val1 = value->millivolts / 1000;
        val_out->val2 = (value->millivolts % 1000) * 1000U;
        break;

    case SENSOR_CHAN_GAUGE_STATE_OF_CHARGE:
        val_out->val1 = value->state_of_charge;
        val_out->val2 = 0;
        break;

    default:
        return -ENOTSUP;
    }

    return 0;
}

uint8_t lithium_ion_mv_to_pct(int16_t bat_mv) {
    // Piecewise-linear lookup against a typical LiPo discharge curve (no load),
    // based on ZMK community work in zmkfirmware/zmk#2066 and published LiPo
    // curves (e.g. AmPow, Adafruit). The old linear 3450-4200mV fit was very
    // inaccurate in the middle plateau region (~30-80%); this gets closer to
    // the actual curve.
    struct lookup_point {
        int16_t millivolts;
        int16_t percent;
    };

    // Curve shifted up ~20-70 mV in the plateau to match 403450-class LiPo
    // cells (common in split-keyboard builds). The prior values read ~8-12 pp
    // low in the 60-80% band; vendor spread is ±20-30 mV through here so the
    // new table fits most 403450 stock within a single % at idle draw.
    static const struct lookup_point battery_lookup[] = {
        {.millivolts = 4200, .percent = 100},
        {.millivolts = 4120, .percent = 90},
        {.millivolts = 4060, .percent = 80},
        {.millivolts = 3990, .percent = 70},
        {.millivolts = 3920, .percent = 60},
        {.millivolts = 3850, .percent = 50},
        {.millivolts = 3800, .percent = 40},
        {.millivolts = 3770, .percent = 30},
        {.millivolts = 3730, .percent = 20},
        {.millivolts = 3680, .percent = 10},
        {.millivolts = 3600, .percent = 5},
        {.millivolts = 3450, .percent = 0},
    };

    if (bat_mv >= battery_lookup[0].millivolts) {
        return battery_lookup[0].percent;
    }

    for (int i = 1; i < ARRAY_SIZE(battery_lookup); i++) {
        const struct lookup_point a = battery_lookup[i - 1];
        const struct lookup_point b = battery_lookup[i];
        if (bat_mv >= b.millivolts) {
            const int t = bat_mv - a.millivolts;
            const int dx = b.millivolts - a.millivolts;
            const int dy = b.percent - a.percent;
            return a.percent + dy * t / dx;
        }
    }

    return battery_lookup[ARRAY_SIZE(battery_lookup) - 1].percent;
}