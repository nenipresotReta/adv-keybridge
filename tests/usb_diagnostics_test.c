/* Exercise production retention logic; warm reset is simulated, not hardware. */
#include <assert.h>
#include <stdio.h>
#define USB_DIAG_RETAINED /* Host tests simulate RTC storage in ordinary RAM. */
static unsigned lock_depth;
static unsigned irq_lock(void) { return lock_depth++; }
static void irq_unlock(unsigned key) { assert(lock_depth==key+1);lock_depth=key; }
#include "../src/usb/diagnostics.c"
int main(void) {
    struct usb_diag_snapshot snapshot;
    retained.magic=0;
    assert(!usb_diag_read(&snapshot));
    assert(capture_previous()==0 && usb_diag_previous()==0);
    usb_diag_begin();adv_usb_trace(ADV_USB_TRACE_HID);adv_usb_diag_retry();adv_usb_diag_sof();
    adv_usb_diag_bus(1,2,3);
    adv_usb_diag_channel(0,4,5,6);
    adv_usb_diag_channel(0,8,9,10);
    adv_usb_diag_channel(USB_DIAG_CHANNELS,~0u,~0u,~0u);
    assert(usb_diag_read(&snapshot));
    assert(snapshot.retries==1 && snapshot.sof==1);
    assert(snapshot.interrupts==1 && snapshot.pending==1 && snapshot.port==2 && snapshot.frame==3);
    assert(snapshot.channel[0].events==2 && snapshot.channel[0].seen==12);
    assert(snapshot.channel[0].interrupt==8 && snapshot.channel[0].characteristics==9);
    usb_diag_fault(ADV_USB_TRACE_DMA_ERROR);
    usb_diag_fault(ADV_USB_TRACE_QUEUE);
    adv_usb_diag_bus(0,0,0);adv_usb_diag_channel(0,0,0,0);
    adv_usb_trace(ADV_USB_TRACE_OFF);
    assert(usb_diag_read(&snapshot));
    assert(snapshot.stage==ADV_USB_TRACE_DMA_ERROR && snapshot.faults==1 && snapshot.port==2);
    assert(capture_previous()==0 && usb_diag_previous()==ADV_USB_TRACE_DMA_ERROR);
    assert(usb_diag_read(&snapshot) && snapshot.channel[0].events==2);
    usb_diag_begin();adv_usb_trace(ADV_USB_TRACE_REPORT);usb_diag_finish();
    adv_usb_trace(ADV_USB_TRACE_OFF);adv_usb_diag_bus(5,5,5);
    assert(usb_diag_read(&snapshot) && snapshot.stage==ADV_USB_TRACE_REPORT);
    assert(!snapshot.faults && !snapshot.interrupts && !snapshot.channel[0].events);
    usb_diag_stop_stage(1);usb_diag_fault(ADV_USB_TRACE_QUEUE);
    usb_diag_stop_stage(2);usb_diag_fault(ADV_USB_TRACE_DMA_ERROR);
    assert(usb_diag_read(&snapshot) && snapshot.stop_stage==2);
    assert(snapshot.faults==1 && snapshot.stage==ADV_USB_TRACE_QUEUE);
    assert(!snapshot.interrupts);usb_diag_stop_stage(3);
    usb_diag_begin();assert(usb_diag_read(&snapshot) && !snapshot.stop_stage && !snapshot.faults);
    assert(!lock_depth);
    puts("PASS: production USB diagnostic retention, reset, first fault, freeze, bounds, no teardown overwrite");
}
