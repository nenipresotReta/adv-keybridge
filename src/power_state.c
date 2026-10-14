/* SPDX-License-Identifier: MIT */
#include "adv_power.h"

bool adv_power_due(uint32_t now,uint32_t last,uint32_t timeout,bool held) {
    return timeout && !held && (uint32_t)(now-last)>=timeout;
}
