/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

enum adv_usb_stage {
    ADV_USB_TRACE_OFF, ADV_USB_TRACE_ROUTE, ADV_USB_TRACE_RESET_IDLE,
    ADV_USB_TRACE_RESET_DONE, ADV_USB_TRACE_RESET_FINAL,
    ADV_USB_TRACE_TX_FLUSH, ADV_USB_TRACE_RX_FLUSH,
    ADV_USB_TRACE_HOST_MODE, ADV_USB_TRACE_FIFO, ADV_USB_TRACE_READY,
    ADV_USB_TRACE_ATTACH, ADV_USB_TRACE_HID, ADV_USB_TRACE_REPORT,
    ADV_USB_TRACE_STOP, ADV_USB_TRACE_QUEUE, ADV_USB_TRACE_DMA_ERROR,
};
void adv_usb_trace(unsigned stage);
void adv_usb_hw_timeout(unsigned stage);
void adv_usb_dma_error(void);
/* ISR observations only: no payload, allocation, logging or hardware access. */
void adv_usb_diag_retry(void);
void adv_usb_diag_sof(void);
void adv_usb_diag_bus(uint32_t pending, uint32_t port, uint32_t frame);
void adv_usb_diag_channel(unsigned channel, uint32_t interrupt,
                          uint32_t characteristics, uint32_t size);

/* Cycle time keeps advancing inside an ISR, unlike a timer tick. Unsigned
 * subtraction handles counter wrap. All supported budgets are below 2^31. */
static inline bool adv_usb_wait_bits(const volatile uint32_t *reg, uint32_t mask,
                                    uint32_t value, uint32_t timeout_us,
                                    unsigned stage) {
    const uint32_t start = k_cycle_get_32();
    const uint32_t budget = k_us_to_cyc_ceil32(timeout_us);
    while ((*reg & mask) != value) {
        if ((uint32_t)(k_cycle_get_32() - start) >= budget) {
            adv_usb_hw_timeout(stage);
            return false;
        }
    }
    return true;
}

/* PTX has seven request-count bits; NPTX has eight. PTX bit 23 describes
 * the queue entry, and must not be mistaken for available capacity.
 * This path can execute in an ISR: bound its spin to 20us, then stop the host. */
static inline bool adv_usb_wait_queue(const volatile uint32_t *reg, bool periodic) {
    const uint32_t start = k_cycle_get_32();
    const uint32_t budget = k_us_to_cyc_ceil32(20);
    const uint32_t mask=periodic ? 0x007f0000u : 0x00ff0000u;
    while ((*reg & mask) == 0) {
        if ((uint32_t)(k_cycle_get_32() - start) >= budget) {
            adv_usb_hw_timeout(ADV_USB_TRACE_QUEUE);
            return false;
        }
    }
    return true;
}
