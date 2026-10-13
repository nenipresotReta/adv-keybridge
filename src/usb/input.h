#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "adv_hid.h"
/* USB owner thread only. Reset invalidates old frames; release is performed
 * on the ZMK workqueue. submit returns an error rather than controlling Host. */
void adv_usb_input_reset(bool accepting);
int adv_usb_submit_values(uint8_t source, uint8_t pages, const struct adv_hid_values *values);
