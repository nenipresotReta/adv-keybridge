/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <esp_rom_sys.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/settings/settings.h>
#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include "adv_ui.h"
#include "adv_keyboard.h"

LOG_MODULE_REGISTER(adv_ui, LOG_LEVEL_INF);
static const struct device *lcd=DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct gpio_dt_spec menu=GPIO_DT_SPEC_GET(DT_NODELABEL(adv_controls),menu_gpios);
static uint8_t band[ADV_WIDTH*12*2];
static struct adv_ui_model previous;
static bool drawn, display_ok, menu_ok;
static struct k_work_delayable ui_work;
static int64_t pressed_since, confirm_until;
static bool menu_held, menu_local, long_action, confirmation_released;
static uint8_t confirm_profile;
static atomic_t ble_error, settings_ready;
static uint8_t reset_reason;

static int settings_committed(void) {
    /* Run after ZMK and Bluetooth have restored their bonds/profile at priority 0. */
    atomic_set(&settings_ready, 1);
    LOG_INF("Settings restored; profile controls ready");
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(adv_ui_ready, "adv_ui", NULL, NULL,
                                        settings_committed, NULL, 100);

static int paint(const struct adv_ui_model *m) {
    if(!display_ok)return -ENODEV;
    if(m->screen.asleep){drawn=false;return 0;}
    if(drawn && m->state==previous.state && m->profile==previous.profile &&
       m->occupied==previous.occupied && m->connected==previous.connected &&
       m->error==previous.error && m->usb==previous.usb &&
       m->usb_reports==previous.usb_reports && m->usb_media_reports==previous.usb_media_reports &&
       m->usb_error==previous.usb_error && m->uptime_seconds==previous.uptime_seconds &&
       m->reset_reason==previous.reset_reason &&
       m->battery.valid==previous.battery.valid &&
       m->battery.millivolts==previous.battery.millivolts &&
       m->battery.percent==previous.battery.percent &&
       m->battery.drain_state==previous.battery.drain_state &&
       m->battery.drain_tenths_per_hour==previous.battery.drain_tenths_per_hour &&
       m->screen.page==previous.screen.page && m->screen.selected==previous.screen.selected &&
       m->screen.brightness==previous.screen.brightness && m->screen_error==previous.screen_error &&
       m->sd_log.state==previous.sd_log.state && m->sd_log.error==previous.sd_log.error &&
       m->sd_log.dropped==previous.sd_log.dropped &&
       m->previous_usb_stage==previous.previous_usb_stage)return 0;
    for(int y=0;y<ADV_HEIGHT;y+=12) {
        int h=MIN(12,ADV_HEIGHT-y);
        adv_render_band(m,y,h,band);
        const struct display_buffer_descriptor desc={.buf_size=ADV_WIDTH*h*2,
            .width=ADV_WIDTH,.height=h,.pitch=ADV_WIDTH};
        int err=display_write(lcd,0,y,&desc,band);
        if(err){LOG_ERR("LCD write: %d",err);return err;}
    }
    previous=*m;drawn=true;
    return 0;
}
static bool active_conn(struct bt_conn *conn) {
    return zmk_ble_active_profile_is_open() ||
        !bt_addr_le_cmp(bt_conn_get_dst(conn),zmk_ble_active_profile_addr());
}
static void security_changed(struct bt_conn *conn,bt_security_t level,enum bt_security_err err) {
    if(active_conn(conn) && err)atomic_set(&ble_error,-(100+err));
    ARG_UNUSED(level);
}
static void connected(struct bt_conn *conn,uint8_t err) {
    if(active_conn(conn))atomic_set(&ble_error,err?-(200+err):0);
    LOG_INF("Connection result: %u",err);
}
static void pairing_failed(struct bt_conn *conn,enum bt_security_err reason) {
    if(active_conn(conn))atomic_set(&ble_error,-(300+reason));
    LOG_ERR("Pairing failed: %d",reason);
}
static void pairing_complete(struct bt_conn *conn,bool bonded) {
    if(active_conn(conn))atomic_clear(&ble_error);
    LOG_INF("Pairing complete; bonded=%d",bonded);
}
static struct bt_conn_auth_info_cb auth_info={.pairing_failed=pairing_failed,
    .pairing_complete=pairing_complete};
BT_CONN_CB_DEFINE(adv_callbacks)={.connected=connected,.security_changed=security_changed};
static int input_activity(const zmk_event_t *event) {
    ARG_UNUSED(event);adv_screen_activity();return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(adv_screen_input,input_activity);
ZMK_SUBSCRIPTION(adv_screen_input,zmk_position_state_changed);
ZMK_SUBSCRIPTION(adv_screen_input,zmk_keycode_state_changed);

static int profile_event(const zmk_event_t *event) {
    ARG_UNUSED(event);atomic_clear(&ble_error);return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(adv_ui,profile_event);
ZMK_SUBSCRIPTION(adv_ui,zmk_ble_active_profile_changed);

static void button(int64_t now) {
    if(!menu_ok)return;
    int value=gpio_pin_get_dt(&menu);
    if(value<0)return;
    bool down=value>0;
    if(down)adv_screen_activity();
    if(down && !menu_held){
        pressed_since=now;long_action=false;
        struct adv_screen_state screen; int error;
        adv_screen_get_state(&screen,&error);
        int action=adv_screen_menu_action(&screen);
        if(action>=0) {
            int result=adv_screen_request(action);
            if(result)LOG_ERR("G0 screen action: %d",result);
            menu_local=true;confirm_until=0;
        }
    }
    if(menu_local){if(!down)menu_local=false;menu_held=down;return;}
    if(!bt_is_ready() || !atomic_get(&settings_ready)){menu_held=down;return;}
    uint8_t profile=zmk_ble_active_profile_index();
    if(confirm_until && confirm_profile!=profile)confirm_until=0;
    if(down && !long_action && now-pressed_since>=1500) {
        confirm_until=now+5000;confirm_profile=profile;
        confirmation_released=false;long_action=true;
    }
    if(!down && menu_held) {
        if(long_action && confirm_until){confirmation_released=true;confirm_until=now+5000;}
        else if(long_action){ /* Profile changed while holding G0: cancel confirmation. */ }
        else if(now-pressed_since>=50) {
            if(confirm_until>now && confirmation_released) {
                zmk_ble_clear_bonds();confirm_until=0;atomic_clear(&ble_error);
            } else {
                confirm_until=0;
                int err=zmk_ble_prof_next();
                if(err)atomic_set(&ble_error,err);
            }
        }
    }
    menu_held=down;
}
static void update(struct k_work *work) {
    ARG_UNUSED(work);
    int64_t now=k_uptime_get();
    adv_screen_tick();
    button(now);
    struct adv_ui_model m={0};
    m.uptime_seconds=(uint32_t)(now/1000);
    m.reset_reason=reset_reason;
    adv_battery_get_status(&m.battery);
#if IS_ENABLED(CONFIG_CARDPUTER_SD_LOG)
    adv_sd_log_get_status(&m.sd_log);
#endif
    adv_screen_get_state(&m.screen,&m.screen_error);
    if(m.screen.page==ADV_PAGE_SETTINGS || m.screen.asleep)confirm_until=0;
#if IS_ENABLED(CONFIG_CARDPUTER_USB_HOST)
    struct adv_usb_status usb;
    adv_usb_get_status(&usb);
    m.usb=usb.state; m.usb_reports=usb.reports; m.usb_error=usb.error;
    m.usb_media_reports=usb.media_reports;
    m.previous_usb_stage=usb.previous_stage;
#endif
    if(!bt_is_ready()){m.state=ADV_ERROR;m.error=-ENODEV;}
    else if(!atomic_get(&settings_ready)){m.state=ADV_STARTING;}
    else {
        m.profile=zmk_ble_active_profile_index();
        for(int n=0;n<ZMK_BLE_PROFILE_COUNT && n<5;n++) {
            if(!zmk_ble_profile_is_open(n))m.occupied|=1<<n;
            if(zmk_ble_profile_is_connected(n))m.connected|=1<<n;
        }
        m.state=zmk_ble_active_profile_is_open()?ADV_PAIRING:ADV_RECONNECTING;
        struct bt_conn *conn=(m.connected & (1<<m.profile))?
            zmk_ble_active_profile_conn():NULL;
        if(conn) {
            if(bt_conn_get_security(conn)>=BT_SECURITY_L2)m.state=ADV_CONNECTED;
            else m.state=ADV_PAIRING;
            bt_conn_unref(conn);
        }
        m.error=atomic_get(&ble_error);
        if(m.error)m.state=ADV_ERROR;
    }
    int key_error=adv_keyboard_error();
    if(key_error){m.state=ADV_ERROR;m.error=key_error;}
    if(confirm_until>now && !m.error)m.state=ADV_CONFIRM_CLEAR;
    paint(&m);
    static int64_t last_heartbeat;
    if(now-last_heartbeat>=60000){last_heartbeat=now;
        LOG_INF("alive %lldms BLE=%d profile=%d state=%d keyboard=%d",now,bt_is_ready(),
                m.profile+1,m.state,key_error);}
    k_work_reschedule(&ui_work,K_MSEC(50));
}
static int early_ui(void) {
    reset_reason=(uint8_t)esp_rom_get_reset_reason(0);
    display_ok=device_is_ready(lcd);
    int screen_error=adv_screen_init();
    if(screen_error)LOG_ERR("Screen init: %d",screen_error);
    if(display_ok) {
        struct adv_ui_model m={.state=ADV_STARTING,.reset_reason=reset_reason};
        adv_screen_get_state(&m.screen,&m.screen_error);
        paint(&m);
    }
    LOG_INF("ADV ZMK: display=%d; next stage Bluetooth init",display_ok);
    LOG_INF("Reset reason: %u; native boot=%d",reset_reason,IS_ENABLED(CONFIG_ESP_SIMPLE_BOOT));
    return 0;
}
SYS_INIT(early_ui,APPLICATION,40);
static int start_ui(void) {
    int err=bt_conn_auth_info_cb_register(&auth_info);
    if(err)LOG_ERR("Pairing observer registration: %d",err);
    menu_ok=gpio_is_ready_dt(&menu) && !gpio_pin_configure_dt(&menu,GPIO_INPUT);
    k_work_init_delayable(&ui_work,update);
    k_work_schedule(&ui_work,K_MSEC(50));
    return 0;
}
SYS_INIT(start_ui,APPLICATION,99);
