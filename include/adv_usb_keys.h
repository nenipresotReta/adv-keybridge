/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ADV_KEY_BYTES 32
/* Boot reports are accepted only after SET_PROTOCOL(BOOT) succeeds. Invalid
 * and rollover reports must leave the last valid state intact. */
static inline bool adv_usb_boot_keys(const uint8_t *report, size_t len,
                                     uint8_t keys[ADV_KEY_BYTES]) {
    if(len != 8) return false;
    for(unsigned i=2;i<8;i++) if(report[i] && report[i]<4) return false;
    for(unsigned i=0;i<ADV_KEY_BYTES;i++) keys[i]=0;
    for(unsigned i=0;i<8;i++) if(report[0] & (1u<<i))
        keys[(224+i)/8] |= 1u<<((224+i)%8);
    for(unsigned i=2;i<8;i++) if(report[i]>=4)
        keys[report[i]/8] |= 1u<<(report[i]%8);
    return true;
}
/* Sources: built-in ZMK events = 0, four external HID interfaces = 1..4.
 * Keep a bit for each source so disconnect/release cannot cancel another
 * keyboard holding the same key. Return 2 for built-in repeat semantics. */
struct adv_key_owners { uint8_t owners[256]; };
static inline int adv_usage_transition(uint8_t *owners,unsigned limit,unsigned source,
                                       unsigned key,bool down) {
    if(source>4 || key>=limit)return -1;
    uint8_t old=owners[key],bit=1u<<source;
    if(down && (old&bit))return source==0 && old==1 ? 2 : 0;
    uint8_t next=down ? old|bit : old&~bit; owners[key]=next;
    return (!old && next) || (old && !next);
}
static inline int adv_key_transition(struct adv_key_owners *m, unsigned source,
                                     unsigned key, bool down) {
    return adv_usage_transition(m->owners,256,source,key,down);
}
