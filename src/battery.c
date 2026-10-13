/* SPDX-License-Identifier: MIT
 * ADC1 channel 9 = GPIO10. ADV R8/R12 are 100k/100k: VBAT = ADC * 2.
 * No charger-status GPIO is available, so no charging state is inferred.
 */
#include <errno.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include "adv_battery.h"
#include "adv_battery_history.h"
#include "adv_battery_trend.h"

LOG_MODULE_REGISTER(adv_battery, LOG_LEVEL_INF);
static const struct adc_dt_spec adc = ADC_DT_SPEC_GET(DT_PATH(adv_battery));
static atomic_t voltage, drain;
static struct adv_battery_trend trend;

void adv_battery_get_status(struct adv_battery_status *status) {
    uint16_t mv = (uint16_t)atomic_get(&voltage);
    uint32_t packed=(uint32_t)atomic_get(&drain);
    *status = (struct adv_battery_status){
        .drain_state = mv ? (enum adv_drain_state)(packed>>16) : ADV_DRAIN_UNKNOWN,
        .drain_tenths_per_hour = packed&0xffff,
        .valid = mv != 0, .millivolts = mv, .percent = adv_battery_estimate(mv),
    };
}

static int sample(uint16_t *battery_mv) {
    int16_t raw;
    struct adc_sequence sequence = {.buffer = &raw, .buffer_size = sizeof(raw)};
    int error = adc_sequence_init_dt(&adc, &sequence);
    if (error) return error;
    /* A bounded eight-sample burst, dropping the minimum and maximum. */
    int32_t sum = 0, minimum = INT32_MAX, maximum = 0;
    for (unsigned i = 0; i < 8; i++) {
        error = adc_read_dt(&adc, &sequence);
        if (error) return error;
        int32_t mv = raw;
        error = adc_raw_to_millivolts_dt(&adc, &mv);
        if (error) return error;
        mv *= 2;
        if (mv < 2000 || mv > 4500) return -ERANGE;
        sum += mv;
        minimum = MIN(minimum, mv);
        maximum = MAX(maximum, mv);
        k_msleep(2);
    }
    *battery_mv = (sum - minimum - maximum) / 6;
    return 0;
}

static void battery_thread(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    int error = adc_is_ready_dt(&adc) ? adc_channel_setup_dt(&adc) : -ENODEV;
    if (error) {
        LOG_ERR("Battery ADC setup: %d", error);
        return; /* Unknown battery never prevents keyboard/BLE startup. */
    }
    unsigned samples=0;
    for (;;) {
        uint16_t mv = 0;
        error = sample(&mv);
        adv_history_sample(k_uptime_get(),error?0:mv);
        adv_battery_trend_update(&trend,k_uptime_get(),error?0:mv);
        atomic_set(&drain,((uint32_t)trend.state<<16)|trend.rate);
        atomic_set(&voltage, error ? 0 : mv);
        if (error) LOG_WRN("Battery sample: %d", error);
        else if(samples++%12==0) LOG_INF("Battery %umV estimate %u%%", mv, adv_battery_estimate(mv));
        k_msleep(5000);
    }
}
K_THREAD_DEFINE(adv_battery_thread, 2048, battery_thread, NULL, NULL, NULL,
                10, 0, 200);
