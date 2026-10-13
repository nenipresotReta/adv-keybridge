#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "../src/battery.c"

static int16_t readings[8] = {1800,2000,2000,2000,2000,2000,2000,2200};
static unsigned reads, sleeps;
static bool ready = true;
static int setup_error, read_error, conversion_error;
static jmp_buf finished;
bool adc_is_ready_dt(const struct adc_dt_spec *p) { (void)p; return ready; }
int adc_channel_setup_dt(const struct adc_dt_spec *p) { (void)p; return setup_error; }
int adc_sequence_init_dt(const struct adc_dt_spec *p,struct adc_sequence *s) {
    (void)p; assert(!s->calibrate); return 0;
}
int adc_read_dt(const struct adc_dt_spec *p,const struct adc_sequence *s) {
    (void)p; assert(s->buffer_size==sizeof(int16_t)); assert(reads<8);
    *(int16_t *)s->buffer=readings[reads++]; return read_error;
}
int adc_raw_to_millivolts_dt(const struct adc_dt_spec *p,int32_t *mv) {
    (void)p; (void)mv; return conversion_error; /* Supply calibrated pin mV. */
}
void k_msleep(int32_t ms) {
    if(ms==5000)longjmp(finished,1);
    assert(ms==2); sleeps++;
}
static struct adv_battery_status run_sample(void) {
    reads=sleeps=0;
    if(!setjmp(finished))battery_thread(NULL,NULL,NULL);
    struct adv_battery_status status;
    adv_battery_get_status(&status);
    return status;
}
int main(void) {
    struct adv_battery_status status = run_sample();
    assert(reads==8 && sleeps==8);
    assert(status.valid && status.millivolts==4000 && status.percent==73);
    read_error=-EIO;
    status=run_sample();
    assert(!status.valid && status.millivolts==0 && reads==1); /* Old reading invalidated. */
    read_error=0;conversion_error=-ENOTSUP;
    assert(!run_sample().valid);
    conversion_error=0;readings[0]=500;
    assert(!run_sample().valid); /* Invalid voltage must not appear as 0% battery. */
    readings[0]=2400;
    assert(!run_sample().valid);
    ready=false;assert(!run_sample().valid && reads==0);
    ready=true;setup_error=-EIO;assert(!run_sample().valid && reads==0);
    puts("PASS: production battery divider/filter, bounded burst, ADC/setup errors and stale reading invalidation");
}

void adv_history_sample(int64_t now,uint16_t mv) { (void)now;(void)mv; }
