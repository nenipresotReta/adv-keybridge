/* SPDX-License-Identifier: MIT */
#include "adv_ui.h"
#include <stdio.h>
#include <errno.h>
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
static void voltage_chart(struct canvas *c,const struct adv_ui_model *m) {
    static const char *scales[]={"10 MIN","30 MIN","1 H","2 H","4 H","8 H","24 H"};
    unsigned scale=m->screen.chart_scale<7?m->screen.chart_scale:2;
    char label[36];
    snprintf(label,sizeof(label),"%s / %u.%02uV",scales[scale],m->battery.millivolts/1000,(m->battery.millivolts%1000)/10);
    text(c,10,28,label,1,MUTED);
    const char *kind=m->chart.eta==ADV_ETA_FULL?"FULL":m->chart.eta==ADV_ETA_LEFT?"LEFT":"ETA";
    if(m->chart.eta_minutes)snprintf(label,sizeof(label),"~%s %uH%02uM",kind,m->chart.eta_minutes/60,m->chart.eta_minutes%60);
    else snprintf(label,sizeof(label),"%s --",kind);
    text(c,139,28,label,1,MUTED);
    unsigned low=4500,high=0;
    for(unsigned i=0;i<ADV_HISTORY_COLUMNS;i++) {
        unsigned mv=m->chart.mv[i]&0x7fff;if(!mv)continue;
        if(mv<low)low=mv;
        if(mv>high)high=mv;
    }
    if(!high) { text(c,44,70,"WAITING FOR SAMPLES",1,MUTED); }
    else {
        low=low>=100?(low/100)*100-100:0;high=((high+99)/100)*100+100;
        for(unsigned i=0;i<3;i++) {
            unsigned value=high-(high-low)*i/2;int y=43+i*30;
            snprintf(label,sizeof(label),"%u.%02uV",value/1000,(value%1000)/10);
            text(c,8,y-3,label,1,MUTED);rect(c,46,y,180,1,PANEL);
        }
        int prev_x=-1,prev_y=0;
        for(unsigned i=0;i<ADV_HISTORY_COLUMNS;i++) {
            unsigned raw=m->chart.mv[i],mv=raw&0x7fff;
            if(raw&0x8000)prev_x=-1;
            if(!mv)continue;
            int x=46+i,y=103-(mv-low)*60/(high-low);
            if(prev_x>=0)for(int xx=prev_x;xx<=x;xx++) {
                int yy=prev_y+(y-prev_y)*(xx-prev_x)/(x-prev_x);
                rect(c,xx,yy,1,2,ACCENT);
            }
            rect(c,x,y,2,2,ACCENT);prev_x=x;prev_y=y;
        }
        text(c,46,108,scales[scale],1,MUTED);text(c,208,108,"NOW",1,MUTED);
    }
    text(c,10,119,"A/D ZOOM   G0/BKSP BACK",1,MUTED);
    if(m->chart.error)snprintf(label,sizeof(label),"RAM ONLY / SD ERROR %d",m->chart.error);
    else snprintf(label,sizeof(label),"VOLTAGE / RUN TIME");
    text(c,10,128,label,1,m->chart.error?AMBER:MUTED);
}
void adv_render_band(const struct adv_ui_model *m,uint16_t y,uint16_t h,uint8_t *pixels) {
    struct canvas c={pixels,y,h};
    rect(&c,0,y,ADV_WIDTH,h,BG);
    text(&c,10,9,m->screen.page==ADV_PAGE_BATTERY?"BATTERY":m->screen.page==ADV_PAGE_SETTINGS?"SETTINGS":"ADV KEYBRIDGE",2,WHITE);
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
    if(m->screen.page==ADV_PAGE_BATTERY) { voltage_chart(&c,m);return; }
    if(m->screen.page==ADV_PAGE_SETTINGS) {
        char sd_label[39];
        if(m->sd_log.state==ADV_SD_ERROR)
            snprintf(sd_label,sizeof(sd_label),"SD LOG ERROR %d",m->sd_log.error);
        else if(m->sd_log.dropped)
            snprintf(sd_label,sizeof(sd_label),"SD LOG ON / LOST %luB",(unsigned long)m->sd_log.dropped);
        else snprintf(sd_label,sizeof(sd_label),"DISPLAY / SD LOG %s",
            m->sd_log.state==ADV_SD_ACTIVE?"ON":"STARTING");
        text(&c,10,28,sd_label,1,m->sd_log.state==ADV_SD_ERROR?RED:MUTED);
        const char *rows[]={"BRIGHTNESS","SLEEP TIME","SCREEN OFF","SLOTS","BATTERY","BACK"};
        for(unsigned row=0;row<3;row++) {
            unsigned n=(m->screen.selected+ADV_SETTINGS_ROWS+row-1)%ADV_SETTINGS_ROWS;
            int top=42+row*20;
            rect(&c,10,top,220,19,row==1?PANEL:BG);
            if(row==1)rect(&c,10,top,3,19,ACCENT);
            text(&c,19,top+6,rows[n],1,row==1?WHITE:MUTED);
            char value[12];value[0]=0;
            if(n==0)snprintf(value,sizeof(value),"%u%%",m->screen.brightness);
            if(n==3) {
                for(unsigned id=0;id<5;id++) {
                    int x=83+id*29;
                    bool focused=row==1 && id==m->screen.profile_cursor;
                    bool enabled=m->pages.enabled&(1u<<id);
                    char slot[3]={'P',(char)('1'+id),0};
                    if(focused)rect(&c,x-2,top+2,25,15,BG);
                    text(&c,x,top+6,slot,1,enabled?WHITE:MUTED);
                    rect(&c,x+15,top+8,3,3,enabled?GREEN:MUTED);
                    if(focused)rect(&c,x-2,top+17,25,1,ACCENT);
                }
            }
            if(n==1) {
                static const char *labels[]={"10S","30S","1MIN","5MIN","10MIN","30MIN","60MIN","INF"};
                snprintf(value,sizeof(value),"%s",labels[m->screen.sleep_index<8?m->screen.sleep_index:2]);
            }
            if(n<=1) {
                unsigned level=n==0?(m->screen.brightness<5?0:m->screen.brightness<10?1:m->screen.brightness/10+1):m->screen.sleep_index;
                unsigned ticks=n==0?12:8, spacing=n==0?5:7;
                for(unsigned tick=0;tick<ticks;tick++)rect(&c,112+tick*spacing,top+7,spacing-2,5,tick<=level?ACCENT:BG);
            }
            text(&c,180,top+6,value,1,row==1?WHITE:MUTED);
        }
        if(m->pages_error) {
            char error[38];
            if(m->pages_error==-ENOSPC)snprintf(error,sizeof(error),"MAX 5 PAGES / DISABLE ONE FIRST");
            else if(m->pages_error==-EINVAL)snprintf(error,sizeof(error),"KEEP AT LEAST ONE BLUETOOTH PAGE");
            else snprintf(error,sizeof(error),"PAGE ERROR %d",m->pages_error);
            text(&c,10,115,error,1,AMBER);
        } else if(m->screen_error) {
            char error[32];snprintf(error,sizeof(error),"SCREEN ERROR %d",m->screen_error);
            text(&c,10,115,error,1,RED);
        } else text(&c,10,115,"W/S MOVE   A/D ADJUST",1,MUTED);
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
    unsigned mask=m->pages.enabled?m->pages.enabled:31;
    unsigned position=0;
    for(unsigned id=0;id<ADV_PAGE_LIMIT;id++) {
        if(!(mask&(1u<<id)))continue;
        int x=10+position++*46;
        rect(&c,x,43,40,29,PANEL);
        bool selected=id==m->pages.current;
        if(selected)rect(&c,x,70,40,2,ACCENT);
        char label[5];
        snprintf(label,sizeof(label),"P%u",id+1);
        text(&c,x+3,51,label,1,selected?WHITE:MUTED);
        if(id<5)dot(&c,x+32,54,(m->connected&(1<<id))?GREEN:(m->occupied&(1<<id))?MUTED:AMBER);
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
