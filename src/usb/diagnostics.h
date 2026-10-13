#pragma once
#include <stdint.h>
#include <stdbool.h>
#define USB_DIAG_CHANNELS 8
struct usb_diag_channel {
    uint32_t events, interrupt, seen, characteristics, size;
};
struct usb_diag_snapshot {
    uint32_t magic, stage, faults, interrupts, pending, port, frame, stop_stage;
    uint32_t retries, sof;
    struct usb_diag_channel channel[USB_DIAG_CHANNELS];
};
/* begin/fault/trace can run with USB IRQs disabled or in ISR context. */
void usb_diag_begin(void);
void usb_diag_fault(unsigned stage);
unsigned usb_diag_previous(void);
/* Owner only, after transport stop or before first start. No live IRQ logging. */
void usb_diag_finish(void);
void usb_diag_stop_stage(unsigned stage);
bool usb_diag_read(struct usb_diag_snapshot *snapshot);
