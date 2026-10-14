/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void adv_power_activity(void);
void adv_power_set_timeout(uint32_t milliseconds);
/* Source 0 is the physical matrix, 1..4 are USB interfaces. */
void adv_power_input(unsigned source, bool held);
bool adv_power_restore_host(void);
bool adv_power_consume_wake_button(bool down);
bool adv_power_due(uint32_t now, uint32_t last, uint32_t timeout, bool held);
