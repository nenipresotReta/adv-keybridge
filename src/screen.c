/* SPDX-License-Identifier: MIT
 * UI-owned display/PWM, bounded command mailbox and brightness preference.
 * No Bluetooth profile management, USB control or raw board registers.
 */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <zmk/keymap.h>
#include <zmk/workqueue.h>
#include "adv_screen.h"
#include "adv_pages.h"
#include "adv_power.h"

LOG_MODULE_REGISTER(adv_screen, LOG_LEVEL_INF);
static const struct device *lcd=DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct pwm_dt_spec backlight=PWM_DT_SPEC_GET(DT_NODELABEL(adv_controls));
static struct adv_screen_state state=ADV_SCREEN_DEFAULT;
static int screen_error;
static atomic_t last_activity;
void adv_screen_activity(void) { atomic_set(&last_activity,(uint32_t)k_uptime_get()); adv_power_activity(); }
static atomic_t sleep_hold, sleep_ack;
void adv_screen_sleep_hold(bool hold) { atomic_set(&sleep_hold,hold); }
int adv_screen_sleep_status(void) { return atomic_get(&sleep_ack); }
static atomic_t loaded_brightness, saved_brightness=100, preference_error;
static atomic_t loaded_sleep, saved_sleep=2, loaded_deep, saved_deep=2;
K_MSGQ_DEFINE(screen_commands, sizeof(unsigned), 16, 4);

/* Cardputer brightness has a nonzero electrical floor. Match Bruce's
 * board calibration (160..255), while keeping zero reserved for screen off.
 * UI percentages are a setting, not the raw PWM duty or optical luminance. */
static uint32_t brightness_pulse(unsigned brightness) {
    if(!brightness)return 0;
    unsigned duty=160u + (95u * brightness / 100u);
    return (uint32_t)(((uint64_t)backlight.period * duty) / 255u);
}

static void save_brightness(struct k_work *work) {
    ARG_UNUSED(work);
    uint8_t brightness=(uint8_t)atomic_get(&saved_brightness);
    int error=settings_save_one("adv_screen/brightness", &brightness, sizeof(brightness));
    uint8_t sleep=(uint8_t)atomic_get(&saved_sleep);
    int sleep_error=settings_save_one("adv_screen/sleep", &sleep, sizeof(sleep));
    if(!error)error=sleep_error;
    uint8_t deep=(uint8_t)atomic_get(&saved_deep);
    int deep_error=settings_save_one("adv_screen/deep",&deep,1);
    if(!error)error=deep_error;
    atomic_set(&preference_error,error);
    if(error)LOG_ERR("Save brightness: %d",error);
}
K_WORK_DELAYABLE_DEFINE(save_work, save_brightness);

static int load_brightness(const char *name,size_t length,settings_read_cb read,void *arg) {
    if(!strcmp(name,"deep")) {
        uint8_t value;
        if(length!=1)return -EINVAL;
        int n=read(arg,&value,1);
        if(n<0)return n;
        if(n!=1 || value>4)return -EINVAL;
        atomic_set(&loaded_deep,value+1);
        return 0;
    }
    if(!strcmp(name,"sleep")) {
        uint8_t value;
        if(length!=1)return -EINVAL;
        int bytes=read(arg,&value,1);
        if(bytes<0)return bytes;
        if(bytes!=1 || value>7)return -EINVAL;
        atomic_set(&loaded_sleep,value+1);
        return 0;
    }
    if(strcmp(name,"brightness"))return -ENOENT;
    if(length!=1)return -EINVAL;
    uint8_t value;
    int bytes=read(arg,&value,sizeof(value));
    if(bytes<0)return bytes;
    if(bytes!=1 || (value!=1 && value!=5 && (value<10 || value>100 || value%10)))return -EINVAL;
    atomic_set(&loaded_brightness,value);
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(adv_screen_preferences,"adv_screen",NULL,
                              load_brightness,NULL,NULL);

int adv_screen_request(unsigned action) {
    if(action>ADV_SCREEN_WAKE)return -EINVAL;
    adv_screen_activity();
    bool was_active=zmk_keymap_layer_active(ADV_SCREEN_LAYER);
    if(action==ADV_SCREEN_OPEN) {
        int error=zmk_keymap_layer_activate(ADV_SCREEN_LAYER,false);
        if(error)return error;
    }
    if(k_msgq_put(&screen_commands,&action,K_NO_WAIT)) {
        if(action==ADV_SCREEN_OPEN && !was_active)
            zmk_keymap_layer_deactivate(ADV_SCREEN_LAYER,false);
        return -ENOBUFS;
    }
    return 0;
}

static int apply_hardware(const struct adv_screen_state *next) {
    if(next->asleep==state.asleep && next->brightness==state.brightness)return 0;
    if(next->asleep) {
        int error=display_blanking_on(lcd);
        if(error)return error;
        error=pwm_set_pulse_dt(&backlight,0);
        if(error)display_blanking_off(lcd);
        return error;
    }
    int error=pwm_set_pulse_dt(&backlight,brightness_pulse(next->brightness));
    if(error)return error;
    if(state.asleep) {
        error=display_blanking_off(lcd);
        if(error)pwm_set_pulse_dt(&backlight,0);
    }
    return error;
}

int adv_screen_init(void) {
    adv_screen_activity();
    if(!device_is_ready(lcd) || !pwm_is_ready_dt(&backlight))return screen_error=-ENODEV;
    screen_error=pwm_set_pulse_dt(&backlight,backlight.period);
    if(!screen_error)screen_error=display_blanking_off(lcd);
    LOG_INF("Screen PWM init: %d, brightness=%u, period=%u ns, floor=160/255",
            screen_error,state.brightness,backlight.period);
    return screen_error;
}

static void commit(struct adv_screen_state next,bool persist) {
    bool hardware_change=next.asleep!=state.asleep || next.brightness!=state.brightness;
    int error=apply_hardware(&next);
    if(error) {
        LOG_ERR("Screen control: %d",error);
        next.asleep=state.asleep; next.brightness=state.brightness;
    }
    if(hardware_change)screen_error=error;
    bool changed=next.brightness!=state.brightness || next.sleep_index!=state.sleep_index || next.deep_sleep_index!=state.deep_sleep_index;
    state=next;
    if(state.page!=ADV_PAGE_KEYBOARD)zmk_keymap_layer_activate(ADV_SCREEN_LAYER,false);
    else zmk_keymap_layer_deactivate(ADV_SCREEN_LAYER,false);
    if(changed) {
        atomic_set(&saved_brightness,state.brightness);
        atomic_set(&saved_sleep,state.sleep_index);
        atomic_set(&saved_deep,state.deep_sleep_index);
        if(persist)k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(),
                                             &save_work,K_SECONDS(2));
    }
}

void adv_screen_tick(void) {
    static bool held, was_asleep;
    if(atomic_get(&sleep_hold)) {
        if(!held) { was_asleep=state.asleep;held=true; }
        struct adv_screen_state next=state;next.asleep=true;
        commit(next,false);
        atomic_set(&sleep_ack,screen_error?screen_error:1);
        return;
    }
    if(held) {
        struct adv_screen_state next=state;next.asleep=was_asleep;
        commit(next,false);held=false;atomic_clear(&sleep_ack);
    }
    uint32_t idle=(uint32_t)k_uptime_get()-(uint32_t)atomic_get(&last_activity);
    uint32_t timeout=adv_screen_sleep_ms(state.sleep_index);
    if (!state.asleep && timeout && idle>=timeout) {
        struct adv_screen_state next=state;
        next.asleep=true; next.page=ADV_PAGE_KEYBOARD;
        commit(next,false);
        /* Bound hardware-error retries to once per idle interval. */
        atomic_set(&last_activity,(uint32_t)k_uptime_get());
    }
    unsigned loaded=(unsigned)atomic_set(&loaded_brightness,0);
    if(loaded) { struct adv_screen_state next=state; next.brightness=loaded; commit(next,false); }
    unsigned sleep=(unsigned)atomic_set(&loaded_sleep,0);
    if(sleep) { struct adv_screen_state next=state; next.sleep_index=sleep-1;commit(next,false); }
    unsigned deep=(unsigned)atomic_set(&loaded_deep,0);
    if(deep) { struct adv_screen_state next=state;next.deep_sleep_index=deep-1;commit(next,false); }
    adv_power_set_timeout(adv_deep_sleep_ms(state.deep_sleep_index));
    unsigned action;
    for(unsigned n=0;n<16 && !k_msgq_get(&screen_commands,&action,K_NO_WAIT);n++) {
        struct adv_screen_state next=state;
        if(action==ADV_SCREEN_SELECT && state.page==ADV_PAGE_SETTINGS && state.selected==3)
            (void)adv_pages_toggle(state.profile_cursor);
        adv_screen_step(&next,action);
        commit(next,true);
        LOG_INF("Screen page=%u asleep=%d brightness=%u",state.page,state.asleep,state.brightness);
    }
}
void adv_screen_get_state(struct adv_screen_state *out,int *error) {
    *out=state; *error=screen_error ? screen_error : atomic_get(&preference_error);
}
