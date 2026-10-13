/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "adv_usb_keys.h"
#define ADV_HID_REPORTS 8
#define ADV_HID_FIELDS 48
#define ADV_CONSUMER_KEYS 6
#define ADV_HID_KEYBOARD 1
#define ADV_HID_CONSUMER 2
struct adv_hid_field {
    uint16_t offset, count, usage_min, usage_max;
    int32_t logical_min;
    uint8_t size, report, page, variable;
};
struct adv_hid_report { uint16_t bits; uint8_t id, pages; };
struct adv_hid_layout {
    struct adv_hid_field fields[ADV_HID_FIELDS];
    struct adv_hid_report reports[ADV_HID_REPORTS];
    uint8_t field_count, report_count, pages;
    bool has_ids;
};
struct adv_hid_values { uint8_t keys[ADV_KEY_BYTES]; uint16_t consumer[ADV_CONSUMER_KEYS]; };
/* Layout and output are only usable on success. Limits fail closed. */
bool adv_hid_parse(struct adv_hid_layout *layout,const uint8_t *desc,size_t length);
/* Returns report index, or -1 for unknown/malformed/rollover report. */
int adv_hid_decode(const struct adv_hid_layout *layout,const uint8_t *data,size_t length,
                   struct adv_hid_values *values);
bool adv_consumer_add(uint16_t keys[ADV_CONSUMER_KEYS],uint16_t usage);
bool adv_hid_apply(const struct adv_hid_layout *layout,struct adv_hid_values held[ADV_HID_REPORTS],
                    unsigned report,const struct adv_hid_values *values,struct adv_hid_values *combined);
