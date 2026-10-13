/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum adv_drain_state { ADV_DRAIN_UNKNOWN, ADV_DRAIN_LEARNING,
    ADV_DRAIN_FALLING, ADV_DRAIN_STABLE, ADV_DRAIN_RISING };
struct adv_battery_status {
    bool valid;
    enum adv_drain_state drain_state;
    uint16_t drain_tenths_per_hour; /* Voltage-derived percentage points/hour. */
    uint16_t millivolts;
    uint8_t percent; /* Voltage estimate, not a measured state of charge. */
};

/* Read-only, nonblocking snapshot. ADC sampling has a separate owner. */
void adv_battery_get_status(struct adv_battery_status *status);

/* Same 3.45..4.20V approximation used by ZMK's battery_common.c. */
static inline uint8_t adv_battery_estimate(uint16_t millivolts) {
    if (millivolts <= 3450) return 0;
    if (millivolts >= 4200) return 100;
    return (uint32_t)(millivolts - 3450) * 100 / 750;
}
