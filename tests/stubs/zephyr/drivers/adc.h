#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct adc_dt_spec { unsigned unused; };
struct adc_sequence { void *buffer; size_t buffer_size; bool calibrate; };
#define ADC_DT_SPEC_GET(...) ((struct adc_dt_spec){0})
bool adc_is_ready_dt(const struct adc_dt_spec *);
int adc_channel_setup_dt(const struct adc_dt_spec *);
int adc_sequence_init_dt(const struct adc_dt_spec *, struct adc_sequence *);
int adc_read_dt(const struct adc_dt_spec *, const struct adc_sequence *);
int adc_raw_to_millivolts_dt(const struct adc_dt_spec *, int32_t *);
