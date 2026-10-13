/* Exercise the real lifecycle policy with an ordered operation log. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "usb/session.h"
static char calls[32];
static unsigned used;
static int start_error;
static void record(char c) { assert(used < sizeof(calls)-1);calls[used++]=c;calls[used]=0; }
static void input(bool on) { record(on ? 'I' : 'R'); }
static int start(void) { record('S');return start_error; }
static void stop(void) { record('D'); }
static void poll(void) { record('P'); }
static const struct usb_session_ops ops={input,start,stop,poll};
static void expect(const char *s) { assert(!strcmp(calls,s));used=0;calls[0]=0; }
int main(void) {
    struct usb_session s={0};
    usb_session_step(&s,false,0,&ops);expect("");assert(s.phase==USB_SESSION_OFF);
    usb_session_step(&s,true,0,&ops);expect("IS");assert(s.phase==USB_SESSION_RUNNING);
    usb_session_step(&s,true,0,&ops);expect("P");
    usb_session_step(&s,false,0,&ops);expect("RD");assert(s.phase==USB_SESSION_OFF);
    start_error=-EIO;
    usb_session_step(&s,true,0,&ops);expect("ISRD");assert(s.phase==USB_SESSION_FAILED);
    usb_session_step(&s,false,-EIO,&ops);expect("");
    start_error=0;
    usb_session_step(&s,true,-EIO,&ops);expect("IS");assert(s.error==0);
    usb_session_step(&s,true,-ETIMEDOUT,&ops);expect("RD");
    assert(s.phase==USB_SESSION_FAILED && s.error==-ETIMEDOUT);
    for(unsigned i=0;i<10000;i++) {
        usb_session_step(&s,true,0,&ops);expect("IS");
        usb_session_step(&s,false,0,&ops);expect("RD");
    }
    puts("PASS: USB lifecycle defaults off, partial-start cleanup, release before teardown, explicit retry, 10000 toggles");
}
