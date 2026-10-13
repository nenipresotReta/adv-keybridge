#pragma once
#include <stdint.h>
#include <stdbool.h>
/* Validate the sparse controller matrix before subtracting its 1-based index. */
static inline bool adv_decode_key(uint8_t event, uint8_t *row, uint8_t *col,
                                  bool *pressed) {
    uint8_t code = event & 0x7f;
    if (!code) return false;
    uint8_t raw_row = (code - 1) / 10, raw_col = (code - 1) % 10;
    if (raw_row >= 7 || raw_col >= 8) return false;
    *row = raw_col % 4;
    *col = raw_row * 2 + (raw_col >= 4);
    *pressed = (event & 0x80) != 0;
    return true;
}
int adv_keyboard_error(void);
