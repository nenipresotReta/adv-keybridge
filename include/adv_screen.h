/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "dt-bindings/adv_screen.h"

enum adv_page { ADV_PAGE_KEYBOARD, ADV_PAGE_SETTINGS, ADV_PAGE_BATTERY };
struct adv_screen_state {
    enum adv_page page;
    uint8_t selected, brightness, sleep_index, deep_sleep_index, profile_cursor, chart_scale;
    bool asleep;
};
#define ADV_SCREEN_DEFAULT { .page=ADV_PAGE_KEYBOARD, .brightness=100, .sleep_index=2, .deep_sleep_index=2, .chart_scale=2 }

uint32_t adv_screen_sleep_ms(unsigned index);
uint32_t adv_deep_sleep_ms(unsigned index);
/* UI owner acknowledges blanking; callers never touch display hardware. */
void adv_screen_sleep_hold(bool hold);
int adv_screen_sleep_status(void);

/* Hardware-independent settings page transitions. */
void adv_screen_step(struct adv_screen_state *state, unsigned action);
/* Negative means normal Bluetooth G0 handling; other values are local only. */
int adv_screen_menu_action(const struct adv_screen_state *state);

/* Requests are nonblocking, thread context only. Opening also activates the
 * isolated ZMK settings layer before the next key can reach normal bindings. */
int adv_screen_request(unsigned action);
/* The UI workqueue is the sole runtime owner of display/PWM/state. */
int adv_screen_init(void);
void adv_screen_tick(void);
/* Nonblocking input notification; does not wake a deliberately dark screen. */
void adv_screen_activity(void);
void adv_screen_get_state(struct adv_screen_state *state, int *error);
