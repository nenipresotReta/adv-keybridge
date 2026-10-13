/* Production TinyUSB adapter, with only the vendor driver/board calls faked. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "usb/transport.h"
#include "usb/board.h"
#include "usb/diagnostics.h"
#include "adv_usb_dwc2.h"
static bool init_ok=true, receive_ok=true, closed;
static int input_error, fault;
static unsigned received, media_count, submits, rearms, quiets, stops;
static enum adv_usb_state state;
static struct adv_hid_values last;
void usb_board_quiet_irq(void) { quiets++; }
void usb_diag_fault(unsigned stage) { (void)stage; }
void adv_usb_trace(unsigned stage) { (void)stage; }
bool tusb_init(uint8_t port,const tusb_rhport_init_t *init) {
    assert(port==0 && init->role==TUSB_ROLE_HOST);return init_ok;
}
bool tuh_deinit(uint8_t port) {
    assert(port==0);stops++;
    tuh_hid_umount_cb(1,0); // real TinyUSB deinit invokes class callbacks
    return true;
}
void tuh_task_ext(uint32_t timeout,bool isr) { assert(timeout==10 && !isr); }
uint8_t tuh_hid_interface_protocol(uint8_t a,uint8_t i) { (void)a;return i==0?HID_ITF_PROTOCOL_KEYBOARD:0; }
uint8_t tuh_hid_get_protocol(uint8_t a,uint8_t i) { (void)a;(void)i;return HID_PROTOCOL_BOOT; }
bool tuh_hid_set_protocol(uint8_t a,uint8_t i,uint8_t p) { (void)a;(void)i;(void)p;return true; }
bool tuh_hid_receive_report(uint8_t a,uint8_t i) { (void)a;(void)i;rearms++;return receive_ok; }
static void publish(enum adv_usb_state next) { state=next; }
static void count(unsigned rx,unsigned media) { received+=rx;media_count+=media; }
static int input(uint8_t source,uint8_t pages,const struct adv_hid_values *v) {
    assert(!closed && source>=1 && source<=4 && pages);submits++;last=*v;return input_error;
}
static void error(int code) { assert(!fault);fault=code; }
static bool pending_events;
bool tuh_task_event_ready(void) { return pending_events; }
static unsigned wakes;
static void wake(void) { wakes++; }
void tuh_event_hook_cb(uint8_t,uint32_t,bool);
static const struct usb_transport_sink sink={publish,count,input,error,wake};
static void start(void) { closed=false;fault=0;input_error=0;receive_ok=true;assert(!usb_transport_start(&sink)); }
int main(void) {
    static const uint8_t consumer_desc[]={0x05,0x0c,0x09,0x01,0xa1,0x01,
        0x15,0x00,0x26,0xff,0x03,0x19,0x00,0x2a,0xff,0x03,0x75,0x10,0x95,0x01,0x81,0x00,0xc0};
    start();wakes=0;tuh_event_hook_cb(0,1,true);assert(wakes==1);
    pending_events=true;usb_transport_poll();assert(wakes==2);
    pending_events=false;usb_transport_poll();assert(wakes==2);
    tuh_hid_mount_cb(1,0,NULL,0);assert(state==ADV_USB_KEYBOARD);
    tuh_mount_cb(1);assert(state==ADV_USB_KEYBOARD); // device mount follows HID mount
    uint8_t key[8]={0,0,4};
    tuh_hid_report_received_cb(1,0,key,sizeof(key));assert(last.keys[0]&(1u<<4));
    tuh_hid_mount_cb(1,1,consumer_desc,sizeof(consumer_desc));
    uint8_t volume[]={0xe9,0};
    tuh_hid_report_received_cb(1,1,volume,sizeof(volume));
    assert(last.consumer[0]==0xe9 && received==2 && media_count==1);
    tuh_hid_umount_cb(1,1);assert(!last.consumer[0]);
    unsigned before=submits;
    closed=true;usb_transport_stop();assert(!fault && submits==before && stops==1);
    start();wakes=0;tuh_event_hook_cb(0,1,true);assert(wakes==1);
    pending_events=true;usb_transport_poll();assert(wakes==2);
    pending_events=false;usb_transport_poll();assert(wakes==2);
    tuh_hid_mount_cb(1,0,NULL,0);before=rearms;
    tuh_hid_report_received_cb(1,0,key,0);assert(fault==-EIO && rearms==before && quiets);
    tuh_hid_report_received_cb(1,0,key,sizeof(key));assert(rearms==before); // failed session is closed
    usb_transport_stop();
    start();wakes=0;tuh_event_hook_cb(0,1,true);assert(wakes==1);
    pending_events=true;usb_transport_poll();assert(wakes==2);
    pending_events=false;usb_transport_poll();assert(wakes==2);
    tuh_hid_mount_cb(1,0,NULL,0);input_error=-ENOBUFS;
    tuh_hid_report_received_cb(1,0,key,sizeof(key));assert(fault==-ENOBUFS);
    usb_transport_stop();
    start();wakes=0;tuh_event_hook_cb(0,1,true);assert(wakes==1);
    pending_events=true;usb_transport_poll();assert(wakes==2);
    pending_events=false;usb_transport_poll();assert(wakes==2);
    tuh_hid_mount_cb(1,0,NULL,0);before=rearms;
    adv_usb_dma_error();assert(fault==-EIO);
    adv_usb_hw_timeout(ADV_USB_TRACE_QUEUE);assert(fault==-EIO); // first fault wins
    tuh_hid_report_received_cb(1,0,key,sizeof(key));assert(rearms==before);
    usb_transport_stop();
    init_ok=false;fault=0;assert(usb_transport_start(&sink)==-ENODEV);usb_transport_stop();
    puts("PASS: production USB transport keyboard/media delivery, callback ordering, stop callbacks, failed reports, bridge overflow, partial init");
}
