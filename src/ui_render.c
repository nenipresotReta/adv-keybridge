/* SPDX-License-Identifier: MIT */
#include "adv_ui.h"
#include <stdio.h>
#include <string.h>

enum { BG=0x0841, PANEL=0x1082, ACCENT=0xb81f, WHITE=0xffff,
       MUTED=0x94b2, GREEN=0x07e0, AMBER=0xfd20, RED=0xf800 };
/* Compact, hand-defined 5x7 uppercase glyphs; six-pixel character advance. */
static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-/.?%~";
static const uint8_t glyphs[][7] = {
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,14},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},{0,4,4,0,4,4,0},{0,0,0,31,0,0,0},
 {1,1,2,4,8,16,16},{0,0,0,0,0,6,6},{14,17,1,2,4,0,4},
 {25,26,2,4,8,11,19},{0,0,9,22,0,0,0},
};
struct canvas { uint8_t *pixels; int y, height; };
static void rect(struct canvas *c, int x, int y, int w, int h, uint16_t color) {
    int x0=x<0?0:x, x1=x+w>ADV_WIDTH?ADV_WIDTH:x+w;
    int y0=y<c->y?c->y:y, y1=y+h>c->y+c->height?c->y+c->height:y+h;
    for(int row=y0;row<y1;row++) for(int col=x0;col<x1;col++) {
        size_t p=((row-c->y)*ADV_WIDTH+col)*2;
        c->pixels[p]=color>>8; c->pixels[p+1]=color&255;
    }
}
static void text(struct canvas *c,int x,int y,const char *s,int scale,uint16_t color) {
    for(;*s;s++,x+=6*scale) {
        const char *g=strchr(chars,*s);
        if(!g || *s==' ') continue;
        const uint8_t *rows=glyphs[g-chars];
        for(int yy=0;yy<7;yy++) for(int xx=0;xx<5;xx++)
            if(rows[yy]&(1<<(4-xx)))rect(c,x+xx*scale,y+yy*scale,scale,scale,color);
    }
}
static void dot(struct canvas *c,int x,int y,uint16_t color) {
    for(int yy=-3;yy<=3;yy++) for(int xx=-3;xx<=3;xx++)
        if(xx*xx+yy*yy<=10)rect(c,x+xx,y+yy,1,1,color);
}
void adv_render_band(const struct adv_ui_model *m,uint16_t y,uint16_t h,uint8_t *pixels) {
    struct canvas c={pixels,y,h};
    rect(&c,0,y,ADV_WIDTH,h,BG);
    text(&c,10,9,m->screen.page==ADV_PAGE_SETTINGS?"SETTINGS":"ADV KEYBRIDGE",2,WHITE);
    uint16_t battery_color = !m->battery.valid ? MUTED :
        m->battery.percent <= 20 ? RED : m->battery.percent <= 40 ? AMBER : GREEN;
    rect(&c,172,10,19,11,battery_color);
    rect(&c,191,13,2,5,battery_color);
    rect(&c,174,12,15,7,BG);
    if(m->battery.valid)rect(&c,174,12,15*(m->battery.percent>100?100:m->battery.percent)/100,7,battery_color);
    char battery_label[8];
    if(m->battery.valid)snprintf(battery_label,sizeof(battery_label),"~%u%%",m->battery.percent);
    else snprintf(battery_label,sizeof(battery_label),"--%%");
    text(&c,198,12,battery_label,1,battery_color);
    if(m->screen.page==ADV_PAGE_SETTINGS) {
        char sd_label[39];
        if(m->sd_log.state==ADV_SD_ERROR)
            snprintf(sd_label,sizeof(sd_label),"SD LOG ERROR %d",m->sd_log.error);
        else if(m->sd_log.dropped)
            snprintf(sd_label,sizeof(sd_label),"SD LOG ON / LOST %luB",(unsigned long)m->sd_log.dropped);
        else snprintf(sd_label,sizeof(sd_label),"DISPLAY / SD LOG %s",
            m->sd_log.state==ADV_SD_ACTIVE?"ON":"STARTING");
        text(&c,10,28,sd_label,1,m->sd_log.state==ADV_SD_ERROR?RED:MUTED);
        const char *rows[]={"BRIGHTNESS","SCREEN OFF","BACK TO KEYBOARD"};
        for(int n=0;n<3;n++) {
            int top=42+n*20;
            rect(&c,10,top,220,19,PANEL);
            if(n==m->screen.selected)rect(&c,10,top,3,19,ACCENT);
            text(&c,19,top+6,rows[n],1,n==m->screen.selected?WHITE:MUTED);
        }
        unsigned brightness=m->screen.brightness>100?100:m->screen.brightness;
        rect(&c,96,50,65,5,BG);
        rect(&c,96,50,65*brightness/100,5,ACCENT);
        char brightness_label[8];
        snprintf(brightness_label,sizeof(brightness_label),"%u%%",brightness);
        text(&c,180,49,brightness_label,1,WHITE);
        char drain_label[38];
        switch(m->battery.drain_state) {
        case ADV_DRAIN_FALLING:
            snprintf(drain_label,sizeof(drain_label),"DRAIN ~%u.%u%%/H  VOLT EST.",
                m->battery.drain_tenths_per_hour/10,m->battery.drain_tenths_per_hour%10);break;
        case ADV_DRAIN_STABLE: snprintf(drain_label,sizeof(drain_label),"DRAIN: STABLE / VOLT EST.");break;
        case ADV_DRAIN_RISING: snprintf(drain_label,sizeof(drain_label),"DRAIN: VOLTAGE RISING");break;
        case ADV_DRAIN_LEARNING: snprintf(drain_label,sizeof(drain_label),"DRAIN: LEARNING ~5 MIN");break;
        default: snprintf(drain_label,sizeof(drain_label),"DRAIN: --");break;
        }
        text(&c,10,104,drain_label,1,MUTED);
        if(m->screen_error) {
            char error[32];snprintf(error,sizeof(error),"SCREEN ERROR %d",m->screen_error);
            text(&c,10,115,error,1,RED);
        } else text(&c,10,115,"W/S MOVE   A/D BRIGHTNESS",1,MUTED);
        text(&c,10,125,"ENTER SELECT   BKSP BACK",1,MUTED);
        return;
    }
    char input[39];
    switch(m->usb) {
      case ADV_USB_STARTING: snprintf(input,sizeof(input),"USB HOST: STARTING"); break;
      case ADV_USB_WAITING: snprintf(input,sizeof(input),"USB HOST: WAITING FOR KEYBOARD"); break;
      case ADV_USB_DEVICE: snprintf(input,sizeof(input),"USB DEVICE / ENUMERATING HID"); break;
      case ADV_USB_KEYBOARD: snprintf(input,sizeof(input),"USB RX %lu / MEDIA %lu",(unsigned long)m->usb_reports,(unsigned long)m->usb_media_reports); break;
      case ADV_USB_UNSUPPORTED: snprintf(input,sizeof(input),"USB HID / RX %lu / UNSUPPORTED",(unsigned long)m->usb_reports); break;
      case ADV_USB_ERROR: snprintf(input,sizeof(input),"USB HOST ERROR %d",m->usb_error); break;
      default: snprintf(input,sizeof(input),"ZMK / BUILT-IN KEYBOARD"); break;
    }
    text(&c,10,28,input,1,m->usb==ADV_USB_ERROR?RED:MUTED);
    for(int n=0;n<5;n++) {
        int x=10+n*46;
        rect(&c,x,43,40,29,PANEL);
        if(n==m->profile)rect(&c,x,70,40,2,ACCENT);
        char label[3]={'P',(char)('1'+n),0};
        text(&c,x+6,51,label,1,n==m->profile?WHITE:MUTED);
        dot(&c,x+31,54,(m->connected&(1<<n))?GREEN:(m->occupied&(1<<n))?MUTED:AMBER);
    }
    const char *label="STARTING";uint16_t color=AMBER;
    switch(m->state) {
      case ADV_PAIRING:label="PAIRING";break;
      case ADV_RECONNECTING:label="RECONNECTING";break;
      case ADV_CONNECTED:label="CONNECTED";color=GREEN;break;
      case ADV_ERROR:label="ERROR";color=RED;break;
      case ADV_CONFIRM_CLEAR:label="CLEAR THIS PROFILE?";color=AMBER;break;
      default:break;
    }
    dot(&c,14,86,color);text(&c,25,82,label,1,WHITE);
    char voltage_label[8];
    if(m->battery.valid)snprintf(voltage_label,sizeof(voltage_label),"%u.%02uV",
        m->battery.millivolts/1000,(m->battery.millivolts%1000)/10);
    else snprintf(voltage_label,sizeof(voltage_label),"--.--V");
    text(&c,194,82,voltage_label,1,battery_color);
    char detail[38];
    if(m->state==ADV_ERROR)snprintf(detail,sizeof(detail),m->usb==ADV_USB_OFF?
        "CODE %d / USB LOG AVAILABLE":"BLE CODE %d / FN-U: USB DEBUG",m->error);
    else if(m->state==ADV_STARTING)snprintf(detail,sizeof(detail),"STARTING BLUETOOTH / USB LOG ON");
    else if(m->state==ADV_CONFIRM_CLEAR)snprintf(detail,sizeof(detail),"RELEASE G0 THEN TAP TO CONFIRM");
    else if(m->previous_usb_stage)snprintf(detail,sizeof(detail),"UP %luS / R%u / USB LAST %u",
        (unsigned long)m->uptime_seconds,m->reset_reason,m->previous_usb_stage);
    else snprintf(detail,sizeof(detail),"UP %luS / RESET %u",(unsigned long)m->uptime_seconds,m->reset_reason);
    text(&c,10,99,detail,1,MUTED);
    rect(&c,10,114,220,1,PANEL);
    text(&c,10,122,m->state==ADV_CONFIRM_CLEAR?"WAIT 5S TO CANCEL":"FN-I: SETTINGS   FN-U: USB",1,MUTED);
}
