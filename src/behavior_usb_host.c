#define DT_DRV_COMPAT zmk_behavior_usb_host
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include "adv_usb.h"
static int pressed(struct zmk_behavior_binding *binding,struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding); ARG_UNUSED(event); adv_usb_request_toggle(); return 0;
}
static int released(struct zmk_behavior_binding *binding,struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding); ARG_UNUSED(event); return 0;
}
static const struct behavior_driver_api api={.binding_pressed=pressed,.binding_released=released};
BEHAVIOR_DT_INST_DEFINE(0,NULL,NULL,NULL,NULL,POST_KERNEL,CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,&api);
