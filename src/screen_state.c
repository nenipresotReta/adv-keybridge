/* SPDX-License-Identifier: MIT */
#include "adv_screen.h"

uint32_t adv_screen_sleep_ms(unsigned index) {
    static const uint32_t delays[]={10000,30000,60000,300000,600000,1800000,3600000,0};
    return index<8?delays[index]:60000;
}

int adv_screen_menu_action(const struct adv_screen_state *s) {
    if(s->asleep)return ADV_SCREEN_WAKE;
    if(s->page!=ADV_PAGE_KEYBOARD)return ADV_SCREEN_BACK;
    return -1;
}

void adv_screen_step(struct adv_screen_state *s, unsigned action) {
    if (action == ADV_SCREEN_OPEN) {
        s->page=ADV_PAGE_SETTINGS; s->selected=0; s->asleep=false;
    } else if (action == ADV_SCREEN_WAKE) {
        s->asleep=false;
    } else if (s->page == ADV_PAGE_BATTERY && !s->asleep) {
        if(action==ADV_SCREEN_BACK || action==ADV_SCREEN_SELECT)s->page=ADV_PAGE_SETTINGS;
        if(action==ADV_SCREEN_DIM && s->chart_scale>0)s->chart_scale--;
        if(action==ADV_SCREEN_BRIGHTEN && s->chart_scale<6)s->chart_scale++;
    } else if (s->page == ADV_PAGE_SETTINGS && !s->asleep) {
        switch (action) {
        case ADV_SCREEN_PREVIOUS: s->selected=(s->selected+ADV_SETTINGS_ROWS-1)%ADV_SETTINGS_ROWS; break;
        case ADV_SCREEN_NEXT: s->selected=(s->selected+1)%ADV_SETTINGS_ROWS; break;
        case ADV_SCREEN_DIM:
            if (!s->selected) s->brightness=s->brightness>10?s->brightness-10:s->brightness>5?5:1;
            if (s->selected==3) s->profile_cursor=(s->profile_cursor+4)%5;
            if (s->selected==1 && s->sleep_index>0) s->sleep_index--;
            break;
        case ADV_SCREEN_BRIGHTEN:
            if (!s->selected && s->brightness<100) s->brightness=s->brightness<5?5:s->brightness<10?10:s->brightness+10;
            if (s->selected==3) s->profile_cursor=(s->profile_cursor+1)%5;
            if (s->selected==1 && s->sleep_index<7) s->sleep_index++;
            break;
        case ADV_SCREEN_SELECT:
            if (s->selected==2) { s->asleep=true; s->page=ADV_PAGE_KEYBOARD; }
            else if (s->selected==4) s->page=ADV_PAGE_BATTERY;
            else if (s->selected==5) s->page=ADV_PAGE_KEYBOARD;
            break;
        case ADV_SCREEN_BACK: s->page=ADV_PAGE_KEYBOARD; break;
        default: break;
        }
    }
}
