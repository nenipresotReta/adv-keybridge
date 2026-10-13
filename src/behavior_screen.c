/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_behavior_adv_screen
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include "adv_screen.h"
static int pressed(struct zmk_behavior_binding *binding,struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event); return adv_screen_request(binding->param1);
}
static int released(struct zmk_behavior_binding *binding,struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding); ARG_UNUSED(event); return 0;
}
static const struct behavior_driver_api api={.binding_pressed=pressed,.binding_released=released};
BEHAVIOR_DT_INST_DEFINE(0,NULL,NULL,NULL,NULL,POST_KERNEL,CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,&api);
