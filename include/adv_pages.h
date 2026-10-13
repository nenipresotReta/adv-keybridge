/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define ADV_PAGE_LIMIT 5
struct adv_pages_state { uint8_t enabled, current; };
unsigned adv_pages_count(uint8_t mask);
int adv_pages_at(uint8_t mask, unsigned position);
int adv_pages_toggle_state(struct adv_pages_state *state, unsigned id);
/* Commands serialize changes; snapshots and input gating are atomic. */
int adv_pages_select(unsigned position);
int adv_pages_next(void);
int adv_pages_toggle(unsigned id);
void adv_pages_get(struct adv_pages_state *out);
int adv_pages_error(void);
