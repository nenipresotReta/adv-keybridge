#pragma once
#include <stdint.h>
#include <stddef.h>
#include "adv_usb.h"
#include "adv_battery.h"
#include "adv_screen.h"
#include "adv_sd_log.h"
#include "adv_pages.h"
#include "adv_battery_history.h"
enum adv_state { ADV_STARTING, ADV_PAIRING, ADV_RECONNECTING, ADV_CONNECTED,
                 ADV_ERROR, ADV_CONFIRM_CLEAR };
struct adv_ui_model {
    struct adv_battery_chart chart;
    struct adv_pages_state pages;
    int pages_error;
    enum adv_state state;
    uint8_t profile, occupied, connected;
    int error;
    enum adv_usb_state usb;
    uint32_t usb_reports;
    uint32_t usb_media_reports;
    int usb_error;
    uint32_t uptime_seconds;
    uint8_t reset_reason;
    uint8_t previous_usb_stage;
    struct adv_battery_status battery;
    struct adv_screen_state screen;
    int screen_error;
    struct adv_sd_log_status sd_log;
};
#define ADV_WIDTH 240
#define ADV_HEIGHT 135
/* Render RGB565 in wire byte order; reusable for native preview rendering. */
void adv_render_band(const struct adv_ui_model *model, uint16_t y,
                     uint16_t height, uint8_t *pixels);
