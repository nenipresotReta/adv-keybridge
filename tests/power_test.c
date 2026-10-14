#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#define TEST_CLOCK
#include "../src/power.c"
int64_t test_clock;
static jmp_buf asleep;
static bool usb_hold,sd_hold,screen_hold,host,inject_input;
static int screen_ready,sd_ready,wake_error,wake_cause,g0;
static bool usb_ready=true;
static unsigned wake_disabled;
int gpio_pin_get_dt(const struct gpio_dt_spec *p) { (void)p;return g0; }
void adv_usb_sleep_hold(bool b) { usb_hold=b; }
bool adv_usb_requested(void) { return host; }
bool adv_usb_sleep_ready(void) { return usb_ready; }
void adv_screen_sleep_hold(bool b) { screen_hold=b; }
int adv_screen_sleep_status(void) { return screen_ready; }
void adv_sd_log_sleep_hold(bool b) { sd_hold=b; }
int adv_sd_log_sleep_status(void) { return sd_ready; }
void k_msleep(int32_t n) { test_clock+=n;if(inject_input)adv_power_activity(); }
int esp_sleep_enable_ext0_wakeup(int pin,int level) { assert(!pin && !level);return wake_error; }
int esp_sleep_disable_wakeup_source(int cause) { assert(cause==ESP_SLEEP_WAKEUP_EXT0);wake_disabled++;return 0; }
int esp_sleep_get_wakeup_cause(void) { return wake_cause; }
int rtc_gpio_pullup_en(int pin) { assert(!pin);return 0; }
int rtc_gpio_pulldown_dis(int pin) { assert(!pin);return 0; }
int rtc_gpio_deinit(int pin) { assert(!pin);return 0; }
void sys_poweroff(void) { assert(usb_hold && sd_hold && screen_hold);longjmp(asleep,1); }
static void reset(void) {
    test_clock=0;activity=held_sources=wake_button=0;timeout_ms=300000;
    usb_hold=sd_hold=screen_hold=host=inject_input=false;usb_ready=true;
    screen_ready=sd_ready=1;wake_error=wake_cause=g0=0;
    test_clock=300000;
}
static void aborted(void) {
    attempt_sleep();assert(!usb_hold && !sd_hold && !screen_hold);
    assert(activity==test_clock && !idle());
}
int main(void) {
    assert(!adv_power_due(100,0,0,false));
    assert(!adv_power_due(299999,0,300000,false));
    assert(adv_power_due(300000,0,300000,false));
    assert(!adv_power_due(300000,0,300000,true));
    assert(adv_power_due(299900,UINT32_MAX-99,300000,false));
    reset();assert(idle());adv_power_input(0,true);test_clock+=300000;assert(!idle());
    adv_power_input(1,true);adv_power_input(0,false);test_clock+=300000;assert(!idle());
    adv_power_input(1,false);assert(!idle());test_clock+=300000;assert(idle());
    g0=1;assert(!idle());g0=-EIO;assert(!idle());
    reset();inject_input=true;aborted();
    reset();screen_ready=-EIO;aborted();
    reset();sd_ready=-EIO;aborted();
    reset();usb_ready=false;aborted();assert(test_clock>=305000);
    reset();wake_error=-EINVAL;aborted();assert(wake_disabled);
    reset();host=true;
    if(!setjmp(asleep)) { attempt_sleep();assert(false); }
    assert(resume_cookie==RESUME_HOST);
    wake_cause=ESP_SLEEP_WAKEUP_EXT0;assert(!power_init() && adv_power_restore_host());
    assert(!resume_cookie && adv_power_consume_wake_button(true));
    assert(adv_power_consume_wake_button(true));
    assert(adv_power_consume_wake_button(false));
    assert(!adv_power_consume_wake_button(true));
    wake_cause=0;resume_cookie=RESUME_HOST;power_init();assert(!adv_power_restore_host());
    puts("PASS: sleep handoff, held inputs, clock wrap, cancellation, timeout, wake failure and RTC Host restoration");
}
