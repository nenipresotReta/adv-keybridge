/* SPDX-License-Identifier: MIT */
#pragma once
#include "adv_battery.h"
#define ADV_TREND_POINTS 21
struct adv_battery_trend {
    uint16_t mv[ADV_TREND_POINTS], last_mv;
    uint32_t seconds[ADV_TREND_POINTS], sum, samples;
    unsigned count, next;
    int64_t bucket_start, last_sample;
    bool started;
    enum adv_drain_state state;
    uint16_t rate;
};
/* Pure bounded estimator. Invalid samples/gaps/large steps reset the window. */
void adv_battery_trend_update(struct adv_battery_trend *trend,int64_t now_ms,uint16_t mv);
