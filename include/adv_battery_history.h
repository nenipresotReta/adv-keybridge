/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#define ADV_HISTORY_COLUMNS 180
enum adv_battery_eta { ADV_ETA_UNKNOWN, ADV_ETA_LEFT, ADV_ETA_FULL };
struct adv_battery_chart {
    uint16_t mv[ADV_HISTORY_COLUMNS]; /* Bit 15 breaks the line across boot/gaps. */
    uint32_t revision;
    enum adv_battery_eta eta;
    uint16_t eta_minutes;
    int error;
};
unsigned adv_history_minutes(unsigned scale);
void adv_history_sample(int64_t now, uint16_t mv);
void adv_history_snapshot(unsigned scale, struct adv_battery_chart *out);
/* Existing SD owner only; RAM sampling remains available without a card. */
void adv_history_storage_start(int available);
void adv_history_storage_tick(void);
void adv_history_storage_stop(int error);
