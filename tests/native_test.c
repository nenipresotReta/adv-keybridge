#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "adv_keyboard.h"
#include "adv_ui.h"
#include "adv_usb_keys.h"
void test_hid_reports(void);

int main(int argc,char **argv) {
    test_hid_reports();
    assert(adv_battery_estimate(0)==0);
    assert(adv_battery_estimate(3450)==0);
    assert(adv_battery_estimate(3825)==50);
    assert(adv_battery_estimate(4200)==100);
    assert(adv_battery_estimate(65535)==100);
    for(unsigned mv=1;mv<65536;mv++) {
        assert(adv_battery_estimate(mv)<=100);
        assert(adv_battery_estimate(mv)>=adv_battery_estimate(mv-1));
    }
    uint8_t keys[32], previous[32];
    const uint8_t down[]={2,0,4,4,5,0,0,0};
    assert(adv_usb_boot_keys(down,8,keys));
    assert(keys[0]==0x30 && keys[28]==2);
    memcpy(previous,keys,32);
    uint8_t rollover[]={0,0,1,1,1,1,1,1};
    assert(!adv_usb_boot_keys(rollover,8,keys) && !memcmp(keys,previous,32));
    for(int len=0;len<8;len++) assert(!adv_usb_boot_keys(down,len,keys));
    assert(!adv_usb_boot_keys(down,9,keys));
    uint8_t empty[8]={0};
    assert(adv_usb_boot_keys(empty,8,keys));
    for(int i=0;i<32;i++) assert(keys[i]==0);
    struct adv_key_owners owners={0};
    assert(adv_key_transition(&owners,0,4,true)==1);
    assert(adv_key_transition(&owners,0,4,true)==2);
    assert(adv_key_transition(&owners,1,4,true)==0);
    assert(adv_key_transition(&owners,0,4,false)==0);
    assert(adv_key_transition(&owners,1,4,false)==1);
    assert(adv_key_transition(&owners,1,4,false)==0);
    assert(adv_key_transition(&owners,4,224,true)==1);
    assert(adv_key_transition(&owners,1,224,true)==0);
    assert(adv_key_transition(&owners,4,224,false)==0);
    assert(adv_key_transition(&owners,1,224,false)==1);
    assert(adv_key_transition(&owners,5,4,true)==-1);
    assert(adv_key_transition(&owners,1,256,true)==-1);
    uint8_t row,col;bool pressed;
    assert(adv_decode_key(0x81,&row,&col,&pressed) && row==0 && col==0 && pressed);
    assert(adv_decode_key(0x05,&row,&col,&pressed) && row==0 && col==1 && !pressed);
    assert(adv_decode_key(0xc4,&row,&col,&pressed) && row==3 && col==13 && pressed);
    assert(adv_decode_key(0x84,&row,&col,&pressed) && row==3 && col==0 && pressed);
    for(int half=0;half<2;half++) {
        bool seen[56]={0};int count=0;
        for(int raw=0;raw<128;raw++) {
            if(!adv_decode_key(raw | (half?0x80:0),&row,&col,&pressed))continue;
            assert(row<4 && col<14 && pressed==!!half);
            assert(!seen[row*14+col]);seen[row*14+col]=true;count++;
        }
        assert(count==56);
    }
    for(int page=ADV_PAGE_KEYBOARD;page<=ADV_PAGE_SETTINGS;page++)
    for(int s=ADV_STARTING;s<=ADV_CONFIRM_CLEAR;s++) for(int usb=ADV_USB_OFF;usb<=ADV_USB_ERROR;usb++) {
        struct adv_ui_model m={.state=s,.profile=4,.occupied=0x13,.connected=0x10,.error=-19,
            .usb=usb,.usb_reports=0xffffffffu,.usb_media_reports=0xffffffffu,.usb_error=-12345,
            .battery={.valid=true,.millivolts=4200,.percent=100,.drain_state=s%5,.drain_tenths_per_hour=123},
            .screen={.page=page,.selected=s%3,.brightness=s%2?10:100},.screen_error=-12345};
        uint8_t guarded[ADV_WIDTH*12*2+32];
        for(int y=0;y<ADV_HEIGHT;y+=12) {
            memset(guarded,0xa5,sizeof(guarded));
            int h=(ADV_HEIGHT-y)<12?ADV_HEIGHT-y:12;
            adv_render_band(&m,y,h,guarded+16);
            for(int i=0;i<16;i++)assert(guarded[i]==0xa5);
            for(size_t i=16+ADV_WIDTH*h*2;i<sizeof(guarded);i++)assert(guarded[i]==0xa5);
        }
    }
    if(argc==2) {
        FILE *f=fopen(argv[1],"wb");assert(f);
        fprintf(f,"P6\n%d %d\n255\n",ADV_WIDTH*3,ADV_HEIGHT*2);
        uint8_t rowbuf[ADV_WIDTH*2];
        for(int y=0;y<ADV_HEIGHT*2;y++)for(int panel=0;panel<3;panel++) {
            enum adv_state state=(y<ADV_HEIGHT?panel:panel+3);
            struct adv_ui_model m={.state=state,
                .profile=1,.occupied=(state==ADV_PAIRING?1:3),
                .connected=(state==ADV_CONNECTED?2:0),.error=-19,
                .battery={.valid=state!=ADV_STARTING,.millivolts=state==ADV_ERROR?3500:4050,
                          .percent=state==ADV_ERROR?6:80,.drain_state=state==ADV_ERROR?ADV_DRAIN_FALLING:ADV_DRAIN_LEARNING,.drain_tenths_per_hour=87},
                .screen={.page=state>=ADV_ERROR?ADV_PAGE_SETTINGS:ADV_PAGE_KEYBOARD,
                         .selected=state==ADV_CONFIRM_CLEAR?1:0,.brightness=state==ADV_ERROR?50:100}};
            adv_render_band(&m,y%ADV_HEIGHT,1,rowbuf);
            for(int x=0;x<ADV_WIDTH;x++) {
                uint16_t c=(rowbuf[x*2]<<8)|rowbuf[x*2+1];
                unsigned char rgb[3]={((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};
                fwrite(rgb,1,3,f);
            }
        }
        fclose(f);
    }
    puts("PASS: USB boot reports/rollover/source ownership, 56 keys, UI band bounds");
}
