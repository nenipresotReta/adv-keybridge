#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "adv_hid.h"
void test_hid_reports(void) {
    struct adv_hid_layout l;
    struct adv_hid_values v,old;
    /* QMK-style extra-key report: ID 3, one 16-bit Consumer selector. */
    const uint8_t array[]={0x05,12,0x09,1,0xa1,1,0x85,3,0x15,0,0x26,0xff,0x0f,
        0x19,0,0x2a,0xff,0x0f,0x75,16,0x95,1,0x81,0,0xc0};
    assert(adv_hid_parse(&l,array,sizeof(array)));
    assert(l.has_ids && l.pages==ADV_HID_CONSUMER && l.reports[0].bits==16);
    const uint8_t up[]={3,0xe9,0};
    assert(adv_hid_decode(&l,up,3,&v)==0 && v.consumer[0]==0xe9);
    old=v;
    for(unsigned len=0;len<3;len++) {
        assert(adv_hid_decode(&l,up,len,&v)==-1);
        assert(!memcmp(&v,&old,sizeof(v)));
    }
    const uint8_t unknown[]={2,0xe9,0};
    assert(adv_hid_decode(&l,unknown,3,&v)==-1);
    const uint8_t stop[]={3,0,0};
    assert(adv_hid_decode(&l,stop,3,&v)==0 && v.consumer[0]==0);
    for(size_t len=0;len<sizeof(array);len++) {
        /* Prefixes with a complete Input item can be decoded too; all others
         * must at least remain safe under ASan/UBSan. */
        adv_hid_parse(&l,array,len);
    }
    /* Unnumbered 8-bit consumer selectors. */
    const uint8_t bytes[]={0x05,12,0x15,0,0x26,0xff,0,0x19,0,0x29,0xff,
        0x75,8,0x95,2,0x81,0};
    assert(adv_hid_parse(&l,bytes,sizeof(bytes)));
    const uint8_t pair[]={0xea,0xe2};
    assert(adv_hid_decode(&l,pair,2,&v)==0 && v.consumer[0]==0xea && v.consumer[1]==0xe2);
    /* Bitmap usages with a leading vendor byte and trailing constant padding. */
    const uint8_t bitmap[]={0x06,0,0xff,0x85,4,0x75,8,0x95,1,0x81,2,
        0x05,12,0x15,0,0x25,1,0x09,0xe9,0x09,0xea,0x09,0xe2,
        0x75,1,0x95,3,0x81,2,0x95,5,0x81,3};
    assert(adv_hid_parse(&l,bitmap,sizeof(bitmap)));
    const uint8_t bm[]={4,0xff,5};
    assert(adv_hid_decode(&l,bm,3,&v)==0 && v.consumer[0]==0xe9 && v.consumer[1]==0xe2 && !v.consumer[2]);
    /* Keyboard report parsing is used only for combined keyboard/media
     * interfaces that need Report protocol. Normal keyboards keep Boot. */
    const uint8_t keyboard[]={0x05,7,0x15,0,0x25,1,0x19,224,0x29,231,
        0x75,1,0x95,8,0x81,2,0x75,8,0x95,1,0x81,3,
        0x19,0,0x29,101,0x25,101,0x95,6,0x81,0};
    assert(adv_hid_parse(&l,keyboard,sizeof(keyboard)));
    const uint8_t kbd[]={2,0,4,4,5,0,0,0};
    assert(adv_hid_decode(&l,kbd,8,&v)==0 && v.keys[0]==0x30 && v.keys[28]==2);
    old=v;
    const uint8_t rollover[]={0,0,1,1,1,1,1,1};
    assert(adv_hid_decode(&l,rollover,8,&v)==-1 && !memcmp(&v,&old,sizeof(v)));
    /* Separate Consumer report IDs must remain independently addressable. */
    const uint8_t multi[]={0x05,12,0x15,0,0x26,0xff,0x0f,0x19,0,0x2a,0xff,0x0f,
        0x85,3,0x75,16,0x95,1,0x81,0,0x85,4,0x19,0,0x2a,0xff,0x0f,0x81,0};
    assert(adv_hid_parse(&l,multi,sizeof(multi)) && l.report_count==2);
    const uint8_t other[]={4,0xea,0};
    assert(adv_hid_decode(&l,up,3,&v)==0);
    struct adv_hid_values held[ADV_HID_REPORTS]={0},combined;
    assert(adv_hid_apply(&l,held,0,&v,&combined) && combined.consumer[0]==0xe9);
    assert(adv_hid_decode(&l,other,3,&v)==1 && v.consumer[0]==0xea);
    assert(adv_hid_apply(&l,held,1,&v,&combined) && combined.consumer[0]==0xe9 && combined.consumer[1]==0xea);
    assert(adv_hid_decode(&l,stop,3,&v)==0);
    assert(adv_hid_apply(&l,held,0,&v,&combined) && combined.consumer[0]==0xea && !combined.consumer[1]);
    old=held[0];
    for(unsigned k=0;k<ADV_CONSUMER_KEYS;k++)v.consumer[k]=k+1;
    assert(!adv_hid_apply(&l,held,0,&v,&combined) && !memcmp(&old,&held[0],sizeof(old)));
    /* Global push/pop restores usage page, report size/count and report ID. */
    const uint8_t stack[]={0x05,12,0x15,0,0x25,1,0x75,1,0x95,1,0xa4,
        0x06,0,0xff,0x75,8,0x81,3,0xb4,0x09,0xe9,0x81,2};
    assert(adv_hid_parse(&l,stack,sizeof(stack)));
    const uint8_t stacked[]={0xff,1};
    assert(adv_hid_decode(&l,stacked,2,&v)==0 && v.consumer[0]==0xe9);
    const uint8_t pop[]={0xb4};
    assert(!adv_hid_parse(&l,pop,1));
    const uint8_t too_big[]={0x05,12,0x75,16,0x96,0xff,0xff,0x19,0,0x29,255,0x81,0};
    assert(!adv_hid_parse(&l,too_big,sizeof(too_big)));
    uint16_t usages[ADV_CONSUMER_KEYS]={0};
    for(unsigned i=1;i<=ADV_CONSUMER_KEYS;i++)assert(adv_consumer_add(usages,i));
    assert(adv_consumer_add(usages,2) && !adv_consumer_add(usages,7));
    uint8_t owners[4096]={0};
    assert(adv_usage_transition(owners,4096,0,0xe9,true)==1);
    assert(adv_usage_transition(owners,4096,3,0xe9,true)==0);
    assert(adv_usage_transition(owners,4096,0,0xe9,false)==0);
    assert(adv_usage_transition(owners,4096,3,0xe9,false)==1);
    /* Deterministic malformed descriptor corpus exercises item lengths,
     * overflow limits, local usage pools and global stack handling. */
    uint32_t seed=1234;uint8_t noise[96],report[64]={0};
    for(unsigned sample=0;sample<2000;sample++) {
        for(unsigned i=0;i<sizeof(noise);i++) { seed=seed*1664525u+1013904223u;noise[i]=seed>>24; }
        if(adv_hid_parse(&l,noise,sample%sizeof(noise)))
            adv_hid_decode(&l,report,sample%sizeof(report),&v);
    }
}
