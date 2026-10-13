/* SPDX-License-Identifier: MIT
 * TinyUSB lifetime and HID callbacks. All callbacks except the vendor fault
 * hooks execute on the USB owner thread. No BLE or UI implementation here.
 */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include "tusb.h"
#include "adv_usb_keys.h"
#include "adv_usb_dwc2.h"
#include "transport.h"
#include "board.h"
#include "diagnostics.h"
LOG_MODULE_REGISTER(adv_usb_transport, LOG_LEVEL_INF);
static const struct usb_transport_sink *sink;
static atomic_t failed;
void tuh_event_hook_cb(uint8_t rhport,uint32_t eventid,bool in_isr) {
    ARG_UNUSED(rhport);ARG_UNUSED(eventid);ARG_UNUSED(in_isr);
    if(sink && sink->wake)sink->wake();
}
static bool live; /* Owner thread only; false before deinit invokes callbacks. */

static void fail(int error, unsigned stage) {
    usb_board_quiet_irq();
    if (atomic_cas(&failed, 0, error)) {
        usb_diag_fault(stage);
        sink->fault(error);
    }
}
/* Vendor adaptation hooks; only latch a fault and quiet the peripheral.
 * The USB owner performs release/teardown on its next iteration. */
void adv_usb_irq_error(int error) { fail(error, 0); }
void adv_usb_event_queue_error(void) { fail(-ENOBUFS, ADV_USB_TRACE_QUEUE); }
void adv_usb_hw_timeout(unsigned stage) { fail(-ETIMEDOUT, stage); }
void adv_usb_dma_error(void) { fail(-EIO, ADV_USB_TRACE_DMA_ERROR); }

struct keyboard {
    uint8_t address, instance;
    bool usable, boot, parsed;
    struct adv_hid_layout layout;
    struct adv_hid_values held[ADV_HID_REPORTS];
};
static struct keyboard keyboards[4];
static void interface_failed(unsigned i, int error) {
    memset(keyboards[i].held, 0, sizeof(keyboards[i].held));
    fail(error, 0);
}
static void deliver(unsigned i, uint8_t pages, const struct adv_hid_values *values) {
    if (atomic_get(&failed)) return;
    int error = sink->input(i + 1, pages, values);
    if (error) interface_failed(i, error);
}
static int slot(uint8_t address,uint8_t instance) {
    for(int i=0;i<4;i++) if(keyboards[i].address==address && keyboards[i].instance==instance) return i;
    return -1;
}
static void state_from_interfaces(void) {
    bool any=false;
    for(int i=0;i<4;i++) {
        if(keyboards[i].usable) { sink->state(ADV_USB_KEYBOARD); return; }
        any |= keyboards[i].address!=0;
    }
    sink->state(any?ADV_USB_UNSUPPORTED:ADV_USB_WAITING);
}
void tuh_mount_cb(uint8_t address) {
    if (!live || atomic_get(&failed)) return;
    ARG_UNUSED(address);
    LOG_INF("USB device mounted: %u", address);
    for (unsigned i=0; i<4; i++) {
        if (keyboards[i].address) { state_from_interfaces(); return; }
    }
    sink->state(ADV_USB_DEVICE);
}
void tuh_umount_cb(uint8_t address) {
    if (!live || atomic_get(&failed)) return;
    ARG_UNUSED(address); state_from_interfaces();
    LOG_INF("USB device removed: %u", address);
}
void tuh_hid_mount_cb(uint8_t address,uint8_t instance,const uint8_t *desc,uint16_t length) {
    if (!live || atomic_get(&failed)) return;
    adv_usb_trace(ADV_USB_TRACE_HID);
    int i;
    for(i=0;i<4 && keyboards[i].address;i++);
    if(i==4) { fail(-ENOSPC, 0); return; }
    bool boot=tuh_hid_interface_protocol(address,instance)==HID_ITF_PROTOCOL_KEYBOARD &&
                tuh_hid_get_protocol(address,instance)==HID_PROTOCOL_BOOT;
    struct keyboard *keyboard=&keyboards[i];
    memset(keyboard,0,sizeof(*keyboard));
    keyboard->address=address; keyboard->instance=instance; keyboard->boot=boot;
    keyboard->parsed=desc && length && adv_hid_parse(&keyboard->layout,desc,length);
    keyboard->usable=boot || keyboard->parsed;
    LOG_INF("HID %u/%u slot=%d boot=%d parsed=%d pages=%u desc=%u",address,instance,
            i+1,boot,keyboard->parsed,keyboard->layout.pages,length);
    state_from_interfaces();
    /* Preserve the tested Boot path. Only a combined keyboard/media interface
     * needs Report protocol to make its Consumer usages available. */
    if(boot && keyboard->parsed && keyboard->layout.pages==(ADV_HID_KEYBOARD|ADV_HID_CONSUMER) &&
       tuh_hid_set_protocol(address,instance,HID_PROTOCOL_REPORT)) return;
    /* Poll even unknown HID interfaces so RX distinguishes silence from an
     * unsupported mouse/vendor report. Such reports never generate keys. */
    if(!tuh_hid_receive_report(address,instance)) interface_failed(i,-EIO);
}
void tuh_hid_set_protocol_complete_cb(uint8_t address,uint8_t instance,uint8_t protocol) {
    if (!live || atomic_get(&failed)) return;
    int i=slot(address,instance); if(i<0)return;
    LOG_INF("HID protocol %u/%u: %u", address, instance, protocol);
    keyboards[i].boot=protocol==HID_PROTOCOL_BOOT;
    if(!tuh_hid_receive_report(address,instance))interface_failed(i,-EIO);
}
void tuh_hid_umount_cb(uint8_t address,uint8_t instance) {
    if (!live || atomic_get(&failed)) return;
    int i=slot(address,instance);
    if(i<0) return;
    const struct adv_hid_values empty={0};
    deliver(i, ADV_HID_KEYBOARD|ADV_HID_CONSUMER, &empty);
    memset(&keyboards[i],0,sizeof(keyboards[i]));
    state_from_interfaces();
}
void tuh_hid_report_received_cb(uint8_t address,uint8_t instance,const uint8_t *report,uint16_t len) {
    if (!live || atomic_get(&failed)) return;
    adv_usb_trace(ADV_USB_TRACE_REPORT);
    int i=slot(address,instance);
    if(i<0)return;
    sink->report(1, 0);
    /* TinyUSB's HID driver maps transfer failures to an empty callback. */
    if(!len) { interface_failed(i,-EIO);return; }
    struct keyboard *keyboard=&keyboards[i];
    if(keyboard->boot) {
        struct adv_hid_values values={0};
        if(adv_usb_boot_keys(report,len,values.keys))deliver(i, ADV_HID_KEYBOARD, &values);
    } else if(keyboard->parsed) {
        struct adv_hid_values values;
        int r=adv_hid_decode(&keyboard->layout,report,len,&values);
        if(r>=0) {
            struct adv_hid_values combined={0};
            if(adv_hid_apply(&keyboard->layout,keyboard->held,r,&values,&combined))
                deliver(i, keyboard->layout.pages, &combined);
            if(keyboard->layout.reports[r].pages & ADV_HID_CONSUMER)sink->report(0, 1);
        }
    }
    if(!atomic_get(&failed) && !tuh_hid_receive_report(address,instance))interface_failed(i,-EIO);
}

int usb_transport_start(const struct usb_transport_sink *listener) {
    sink = listener;
    atomic_clear(&failed);
    live = true;
    memset(keyboards, 0, sizeof(keyboards));
    const tusb_rhport_init_t init = {.role=TUSB_ROLE_HOST, .speed=TUSB_SPEED_FULL};
    if (!tusb_init(0, &init)) {
        if (!atomic_get(&failed)) fail(-ENODEV, 0);
    }
    return atomic_get(&failed);
}
void usb_transport_stop(void) {
    live = false;
    /* Safe for a partially initialized TinyUSB controller as well. */
    tuh_deinit(0);
    memset(keyboards, 0, sizeof(keyboards));
}
void usb_transport_poll(void) {
    tuh_task_ext(10, false);
    /* A bounded task pass may leave events queued without a new IRQ edge. */
    if(tuh_task_event_ready() && sink->wake)sink->wake();
}
