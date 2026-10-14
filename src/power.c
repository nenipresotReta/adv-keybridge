/* SPDX-License-Identifier: MIT
 * Sleep coordinator. Peripheral owners acknowledge quiescence; failures or
 * fresh input abort the transition. No FS, display or USB register access.
 */
#include <errno.h>
#include <esp_attr.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/logging/log.h>
#include "adv_power.h"
#include "adv_screen.h"
#include "adv_usb.h"
#include "adv_sd_log.h"

LOG_MODULE_REGISTER(adv_power,LOG_LEVEL_INF);
static atomic_t activity, timeout_ms=1800000, held_sources, wake_button, restore_host;
static const struct gpio_dt_spec menu=GPIO_DT_SPEC_GET(DT_NODELABEL(adv_controls),menu_gpios);
static RTC_NOINIT_ATTR uint32_t resume_cookie;
#define RESUME_HOST 0x41445631u

void adv_power_activity(void) { atomic_set(&activity,(uint32_t)k_uptime_get()); }
void adv_power_set_timeout(uint32_t ms) { atomic_set(&timeout_ms,ms); }
void adv_power_input(unsigned source,bool held) {
    if(source>4)return;
    atomic_val_t bit=1u<<source;
    atomic_val_t old=held?atomic_or(&held_sources,bit):atomic_and(&held_sources,~bit);
    if(!!(old&bit)!=held)adv_power_activity();
}
bool adv_power_restore_host(void) { return atomic_get(&restore_host)!=0; }
bool adv_power_consume_wake_button(bool down) {
    if(!atomic_get(&wake_button))return false;
    if(!down)atomic_clear(&wake_button);
    return true;
}
static bool idle(void) {
    return gpio_pin_get_dt(&menu)==0 && adv_power_due((uint32_t)k_uptime_get(),
        (uint32_t)atomic_get(&activity),(uint32_t)atomic_get(&timeout_ms),
        atomic_get(&held_sources)!=0);
}
static int ready(void) {
    int screen=adv_screen_sleep_status(), sd=adv_sd_log_sleep_status();
    if(screen<0)return screen;
    if(sd<0)return sd;
    return screen==1 && sd==1 && adv_usb_sleep_ready();
}
static void attempt_sleep(void) {
    uint32_t generation=(uint32_t)atomic_get(&activity);
    bool host=adv_usb_requested();
    LOG_INF("Deep sleep requested; Host=%d, G0 wakes",host);
    adv_usb_sleep_hold(true);
    adv_screen_sleep_hold(true);
    /* Allow the sleep marker to reach the deferred log backend. */
    k_msleep(100);
    adv_sd_log_sleep_hold(true);
    int result=0;
    int64_t deadline=k_uptime_get()+5000;
    do {
        if((uint32_t)atomic_get(&activity)!=generation || !idle()) { result=-ECANCELED;break; }
        result=ready();
        if(result)break;
        k_msleep(20);
    } while(k_uptime_get()<deadline);
    if(result==1) {
        /* RTC pull-up remains active in deep sleep. GPIO0 is active-low. */
        result=esp_sleep_enable_ext0_wakeup(GPIO_NUM_0,0);
        if(!result)result=rtc_gpio_pullup_en(GPIO_NUM_0);
        if(!result)result=rtc_gpio_pulldown_dis(GPIO_NUM_0);
        if(!result && (uint32_t)atomic_get(&activity)==generation && idle()) {
            resume_cookie=host?RESUME_HOST:0;
            sys_poweroff();
        }
        if(!result)result=-ECANCELED;
    } else if(!result)result=-ETIMEDOUT;
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
    adv_sd_log_sleep_hold(false);
    adv_screen_sleep_hold(false);
    adv_usb_sleep_hold(false);
    /* No rapid retry loop on failing hardware. */
    adv_power_activity();
    LOG_WRN("Deep sleep cancelled: %d",result);
}
static void power_thread(void *a,void *b,void *c) {
    ARG_UNUSED(a);ARG_UNUSED(b);ARG_UNUSED(c);
    for(;;) { if(idle())attempt_sleep();k_msleep(1000); }
}
static int power_init(void) {
    bool woke=esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT0;
    atomic_set(&restore_host,woke && resume_cookie==RESUME_HOST);
    resume_cookie=0;
    /* Return the RTC wake pin to the normal GPIO driver before UI setup. */
    if(woke) { rtc_gpio_deinit(GPIO_NUM_0);atomic_set(&wake_button,1); }
    adv_power_activity();
    LOG_INF("Power ready: deep wake=%d",woke);
    return 0;
}
SYS_INIT(power_init,APPLICATION,80);
K_THREAD_DEFINE(adv_power_task,3072,power_thread,NULL,NULL,NULL,13,0,5000);
