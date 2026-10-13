/* SPDX-License-Identifier: MIT */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zmk/events/keycode_state_changed.h>
#include "adv_usb_keys.h"
#include "adv_hid.h"
#include "usb/input.h"
#include <errno.h>

struct usb_frame { uint8_t source, pages; uint32_t epoch; struct adv_hid_values values; };
K_MSGQ_DEFINE(usb_frames, sizeof(struct usb_frame), 16, 4);
static uint8_t held[4][ADV_KEY_BYTES];
static struct adv_key_owners owners;
static uint16_t consumer_held[4][ADV_CONSUMER_KEYS];
static uint8_t consumer_owners[4096];
static atomic_t input_epoch, accepting;
static uint32_t applied_epoch;

int adv_hid_key_transition(const struct zmk_keycode_state_changed *ev) {
    int result;
    if(ev->usage_page==7) result=adv_key_transition(&owners,ev->adv_source,ev->keycode,ev->state);
    else if(ev->usage_page==12) result=adv_usage_transition(consumer_owners,4096,ev->adv_source,ev->keycode,ev->state);
    else return 2;
    return result<0 ? 0 : result;
}
static void apply(const struct usb_frame *input) {
        const struct usb_frame frame=*input;
        uint8_t *old=held[frame.source-1];
        /* Releases first, then modifiers, then ordinary presses. */
        if(frame.pages & ADV_HID_KEYBOARD) {
        for(int pass=0;pass<3;pass++) for(unsigned key=4;key<256;key++) {
            bool before=old[key/8] & (1u<<(key%8));
            bool after=frame.values.keys[key/8] & (1u<<(key%8));
            if(before==after || (pass==0 && after) ||
               (pass==1 && (!after || key<224 || key>231)) ||
               (pass==2 && (!after || (key>=224 && key<=231)))) continue;
            struct zmk_keycode_state_changed ev={.adv_source=frame.source,
                .usage_page=7,.keycode=key,.state=after,.timestamp=k_uptime_get()};
            raise_zmk_keycode_state_changed(ev);
        }
        memcpy(old,frame.values.keys,ADV_KEY_BYTES);
        }
        if(frame.pages & ADV_HID_CONSUMER) {
            uint16_t *before=consumer_held[frame.source-1];
            const uint16_t *after=frame.values.consumer;
            for(int pass=0;pass<2;pass++) for(unsigned k=0;k<ADV_CONSUMER_KEYS;k++) {
                uint16_t key=pass?after[k]:before[k];
                if(!key)continue;
                const uint16_t *other=pass?before:after;
                bool found=false;
                for(unsigned j=0;j<ADV_CONSUMER_KEYS;j++)found|=other[j]==key;
                if(found)continue;
                struct zmk_keycode_state_changed ev={.adv_source=frame.source,
                    .usage_page=12,.keycode=key,.state=pass!=0,.timestamp=k_uptime_get()};
                raise_zmk_keycode_state_changed(ev);
            }
            memcpy(before,after,sizeof(consumer_held[0]));
        }
}
static void drain(struct k_work *work) {
    ARG_UNUSED(work);
    struct usb_frame frame;
    /* A reset is a barrier: releases run even when the queue was full, and
     * stale reports cannot re-press a key after that barrier. */
    for(unsigned n=0;n<8;n++) {
        uint32_t epoch=(uint32_t)atomic_get(&input_epoch);
        if(epoch!=applied_epoch) {
            const struct usb_frame empty={.pages=ADV_HID_KEYBOARD|ADV_HID_CONSUMER};
            for(unsigned source=1;source<=4;source++) {
                frame=empty;frame.source=source;apply(&frame);
            }
            applied_epoch=epoch;
        }
        if(k_msgq_get(&usb_frames,&frame,K_NO_WAIT))break;
        if(frame.epoch==applied_epoch && frame.epoch==(uint32_t)atomic_get(&input_epoch))apply(&frame);
    }
    if(k_msgq_num_used_get(&usb_frames) || applied_epoch!=(uint32_t)atomic_get(&input_epoch))
        k_work_submit(work);
}
K_WORK_DEFINE(input_work,drain);
void adv_usb_input_reset(bool active) {
    atomic_clear(&accepting);
    atomic_inc(&input_epoch);
    k_msgq_purge(&usb_frames);
    k_work_submit(&input_work);
    if(active)atomic_set(&accepting,1);
}
static int submit(struct usb_frame *frame) {
    if(frame->source<1 || frame->source>4)return -EINVAL;
    if(!atomic_get(&accepting))return -ESHUTDOWN;
    frame->epoch=(uint32_t)atomic_get(&input_epoch);
    if(k_msgq_put(&usb_frames,frame,K_NO_WAIT)) {
        /* Never block the TinyUSB event thread behind Bluetooth backpressure. */
        adv_usb_input_reset(false);
        return -ENOBUFS;
    }
    k_work_submit(&input_work);
    return 0;
}
/* Called only from the USB thread, never from an ISR. */
int adv_usb_submit_values(uint8_t source,uint8_t pages,const struct adv_hid_values *values) {
    struct usb_frame frame={.source=source,.pages=pages,.values=*values};
    return submit(&frame);
}
