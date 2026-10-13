#pragma once
#include <stdint.h>
#include <stdbool.h>
struct zmk_keycode_state_changed {
    uint8_t adv_source;
    uint16_t usage_page, keycode;
    bool state;
    int64_t timestamp;
};
int raise_zmk_keycode_state_changed(struct zmk_keycode_state_changed event);
