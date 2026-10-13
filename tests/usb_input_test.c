/* Exercise the production input worker with a deterministic bounded queue. */
#include <stdio.h>
#include <assert.h>
#include <zephyr/kernel.h>
#include <zmk/events/keycode_state_changed.h>
#include "adv_hid.h"
#include "usb/input.h"
struct k_work *test_work;
static bool down[256], media[4096], reset_during_press;
static unsigned presses[256];
static int fault;
extern int adv_hid_key_transition(const struct zmk_keycode_state_changed *event);
int raise_zmk_keycode_state_changed(struct zmk_keycode_state_changed event) {
    if(adv_hid_key_transition(&event)) {
        if(event.usage_page==7) {
            down[event.keycode]=event.state;
            if(event.state)presses[event.keycode]++;
        } else media[event.keycode]=event.state;
    }
    if(reset_during_press && event.state) { reset_during_press=false;adv_usb_input_reset(false); }
    return 0;
}
static void pump(void) {
    for(unsigned i=0;test_work && test_work->pending;i++) {
        assert(i<32);test_work->pending=false;test_work->handler(test_work);
    }
}
static void send(unsigned source,unsigned key,unsigned consumer) {
    struct adv_hid_values v={0};
    if(key)v.keys[key/8]|=1u<<(key%8);
    v.consumer[0]=consumer;
    int error=adv_usb_submit_values(source,ADV_HID_KEYBOARD|ADV_HID_CONSUMER,&v);
    if(error)fault=error;
}
int main(void) {
    adv_usb_input_reset(true);pump();
    send(1,4,0xe9);pump();assert(down[4] && media[0xe9]);
    for(unsigned i=0;i<50;i++)pump(); /* Idle is not a reason to release a held key. */
    assert(down[4] && media[0xe9]);
    /* Real overflow: release the previous press, discard queued stale presses. */
    for(unsigned i=0;i<17;i++)send(1,5,0xea);
    assert(fault==-ENOBUFS);pump();
    assert(!down[4] && !media[0xe9] && !down[5] && !presses[5]);
    send(1,5,0xea);pump();assert(!down[5]); /* Closed until explicit restart. */
    adv_usb_input_reset(true);pump();fault=0;
    send(1,6,0);adv_usb_input_reset(false);adv_usb_input_reset(true);send(1,7,0);pump();
    assert(!down[6] && !presses[6] && down[7]);
    /* Release USB ownership without releasing the built-in keyboard's key. */
    struct zmk_keycode_state_changed builtin={.usage_page=7,.keycode=7,.state=true};
    raise_zmk_keycode_state_changed(builtin);adv_usb_input_reset(false);pump();assert(down[7]);
    builtin.state=false;raise_zmk_keycode_state_changed(builtin);assert(!down[7]);
    adv_usb_input_reset(true);pump();
    for(unsigned i=0;i<10000;i++) { send(1,i%2?0:4,0);pump(); }
    assert(!down[4] && !fault);
    /* Reset while delivering an event: the next worker pass must release it. */
    reset_during_press=true;send(1,8,0);pump();assert(!down[8]);
    adv_usb_input_reset(true);pump();send(0,9,0);send(5,9,0);pump();assert(!down[9]);
    puts("PASS: production USB input overflow, stale-session barrier, release ownership, 10000 edges");
}
