#pragma once
#include <stdbool.h>
#include <stdint.h>
struct pwm_dt_spec { uint32_t period; };
#define PWM_DT_SPEC_GET(...) ((struct pwm_dt_spec){.period=1000000})
bool pwm_is_ready_dt(const struct pwm_dt_spec *);
int pwm_set_pulse_dt(const struct pwm_dt_spec *,uint32_t);
