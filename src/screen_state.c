/* SPDX-License-Identifier: MIT */
#include "adv_screen.h"

int adv_screen_menu_action(const struct adv_screen_state *s) {
    if(s->asleep)return ADV_SCREEN_WAKE;
    if(s->page==ADV_PAGE_SETTINGS)return ADV_SCREEN_BACK;
    return -1;
}

void adv_screen_step(struct adv_screen_state *s, unsigned action) {
    if (action == ADV_SCREEN_OPEN) {
        s->page=ADV_PAGE_SETTINGS; s->selected=0; s->asleep=false;
    } else if (action == ADV_SCREEN_WAKE) {
        s->asleep=false;
    } else if (s->page == ADV_PAGE_SETTINGS && !s->asleep) {
        switch (action) {
        case ADV_SCREEN_PREVIOUS: s->selected=(s->selected+2)%3; break;
        case ADV_SCREEN_NEXT: s->selected=(s->selected+1)%3; break;
        case ADV_SCREEN_DIM:
            if (!s->selected && s->brightness>10) s->brightness-=10;
            break;
        case ADV_SCREEN_BRIGHTEN:
            if (!s->selected && s->brightness<100) s->brightness+=10;
            break;
        case ADV_SCREEN_SELECT:
            if (s->selected==1) { s->asleep=true; s->page=ADV_PAGE_KEYBOARD; }
            else if (s->selected==2) s->page=ADV_PAGE_KEYBOARD;
            break;
        case ADV_SCREEN_BACK: s->page=ADV_PAGE_KEYBOARD; break;
        default: break;
        }
    }
}
