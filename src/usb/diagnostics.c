/* SPDX-License-Identifier: MIT */
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include "adv_usb_dwc2.h"
#include "diagnostics.h"

/* Diagnostic only. RTC memory may survive a CPU reset, never full power loss.
 * No key payloads, bond data or host commands are retained. */
#ifndef USB_DIAG_RETAINED
#define USB_DIAG_RETAINED __attribute__((section(".rtc_noinit")))
#endif
static volatile struct usb_diag_snapshot retained USB_DIAG_RETAINED;
static unsigned previous_stage;
static bool frozen = true;
#define TRACE_MAGIC 0xad571404u

void adv_usb_trace(unsigned stage) {
    unsigned key = irq_lock();
    if (!frozen) retained.stage = stage;
    irq_unlock(key);
}
void usb_diag_begin(void) {
    unsigned key = irq_lock();
    retained = (struct usb_diag_snapshot){.stage=ADV_USB_TRACE_ROUTE};
    retained.magic = TRACE_MAGIC;
    frozen = false;
    irq_unlock(key);
}
void usb_diag_fault(unsigned stage) {
    unsigned key = irq_lock();
    if (!retained.faults) {
        if (stage) retained.stage = stage;
        retained.faults++;
        frozen = true;
    }
    irq_unlock(key);
}
void adv_usb_diag_bus(uint32_t pending, uint32_t port, uint32_t frame) {
    unsigned key = irq_lock();
    if (!frozen) {
        retained.interrupts++;
        retained.pending = pending;
        retained.port = port;
        retained.frame = frame;
    }
    irq_unlock(key);
}
void adv_usb_diag_channel(unsigned channel, uint32_t interrupt,
                          uint32_t characteristics, uint32_t size) {
    if (channel >= USB_DIAG_CHANNELS) return;
    unsigned key = irq_lock();
    if (!frozen) {
        volatile struct usb_diag_channel *record = &retained.channel[channel];
        record->events++;
        record->interrupt = interrupt;
        record->seen |= interrupt;
        record->characteristics = characteristics;
        record->size = size;
    }
    irq_unlock(key);
}
void usb_diag_finish(void) {
    unsigned key = irq_lock();
    frozen = true; /* Teardown must not erase the last working/failing state. */
    irq_unlock(key);
}
void usb_diag_stop_stage(unsigned stage) {
    unsigned key = irq_lock();
    retained.stop_stage = stage;
    irq_unlock(key);
}
bool usb_diag_read(struct usb_diag_snapshot *snapshot) {
    unsigned key = irq_lock();
    bool valid = retained.magic == TRACE_MAGIC;
    if (valid) *snapshot = retained;
    irq_unlock(key);
    return valid;
}
unsigned usb_diag_previous(void) { return previous_stage; }

static int capture_previous(void) {
    if (retained.magic == TRACE_MAGIC && retained.stage <= ADV_USB_TRACE_DMA_ERROR)
        previous_stage = retained.stage;
    frozen = true;
    return 0;
}
SYS_INIT(capture_previous, PRE_KERNEL_1, 2);

void adv_usb_diag_retry(void) {
    unsigned key=irq_lock();if (!frozen) retained.retries++;irq_unlock(key);
}
void adv_usb_diag_sof(void) {
    unsigned key=irq_lock();if (!frozen) retained.sof++;irq_unlock(key);
}
