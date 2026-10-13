#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "adv_usb.h"
#include "adv_hid.h"

struct usb_transport_sink {
    /* state/report/input: owner thread only. fault: may also run in ISR. */
    void (*state)(enum adv_usb_state state);
    void (*report)(unsigned received, unsigned media);
    int (*input)(uint8_t source, uint8_t pages, const struct adv_hid_values *values);
    void (*fault)(int error);
    /* Event queued by TinyUSB, may run in ISR; wake owner only. */
    void (*wake)(void);
};
/* Sink must have static lifetime. start/stop/poll: one owner thread only.
 * stop is mandatory even after a failed start (TinyUSB may be partially live). */
int usb_transport_start(const struct usb_transport_sink *sink);
void usb_transport_stop(void);
void usb_transport_poll(void);
