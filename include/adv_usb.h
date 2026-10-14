#pragma once
#include <stdbool.h>
#include <stdint.h>
enum adv_usb_state { ADV_USB_OFF, ADV_USB_STARTING, ADV_USB_WAITING,
    ADV_USB_DEVICE, ADV_USB_KEYBOARD, ADV_USB_UNSUPPORTED, ADV_USB_ERROR };
struct adv_usb_status { enum adv_usb_state state; uint32_t reports, media_reports; int error; unsigned previous_stage; };
void adv_usb_request_toggle(void);
bool adv_usb_requested(void);
void adv_usb_sleep_hold(bool hold);
bool adv_usb_sleep_ready(void);
void adv_usb_get_status(struct adv_usb_status *status);
