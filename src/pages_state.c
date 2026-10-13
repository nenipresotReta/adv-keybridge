/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include "adv_pages.h"
unsigned adv_pages_count(uint8_t mask) {
    unsigned count=0;
    for(unsigned i=0;i<ADV_PAGE_LIMIT;i++)count+=(mask>>i)&1;
    return count;
}
int adv_pages_at(uint8_t mask,unsigned position) {
    for(unsigned i=0;i<ADV_PAGE_LIMIT;i++)if(mask&(1u<<i)) {
        if(!position--)return (int)i;
    }
    return -EINVAL;
}
int adv_pages_toggle_state(struct adv_pages_state *s,unsigned id) {
    if(id>=ADV_PAGE_LIMIT)return -EINVAL;
    uint8_t mask=s->enabled^(1u<<id);
    if(!(mask&31))return -EINVAL;
    if(adv_pages_count(mask)>ADV_PAGE_LIMIT)return -ENOSPC;
    s->enabled=mask;
    if(!(mask&(1u<<s->current)))s->current=(uint8_t)adv_pages_at(mask,0);
    return 0;
}
