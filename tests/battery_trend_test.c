#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "adv_battery_trend.h"
static void feed(struct adv_battery_trend *t,int end,int direction,bool noise) {
    for(int sec=0;sec<=end;sec+=5) {
        int mv=4000+direction*sec/30+(noise?((sec/5)%5-2):0);
        adv_battery_trend_update(t,(int64_t)sec*1000,mv);
    }
}
int main(void) {
    struct adv_battery_trend t={0};feed(&t,300,-1,false);
    assert(t.state==ADV_DRAIN_LEARNING);
    adv_battery_trend_update(&t,305000,3990);
    assert(!t.rate);
    memset(&t,0,sizeof(t));feed(&t,660,-1,false);
    assert(t.state==ADV_DRAIN_FALLING && t.rate>=157 && t.rate<=162);
    assert(t.count==21); /* Ring wrapped, oldest point removed. */
    memset(&t,0,sizeof(t));feed(&t,900,-1,true);
    assert(t.state==ADV_DRAIN_FALLING && t.rate>=150 && t.rate<=170);
    memset(&t,0,sizeof(t));feed(&t,660,0,true);
    assert(t.state==ADV_DRAIN_STABLE && !t.rate);
    memset(&t,0,sizeof(t));feed(&t,660,1,false);
    assert(t.state==ADV_DRAIN_RISING && !t.rate);
    adv_battery_trend_update(&t,665000,4150);
    assert(t.state==ADV_DRAIN_LEARNING && !t.rate && !t.count);
    adv_battery_trend_update(&t,670000,0);
    assert(t.state==ADV_DRAIN_UNKNOWN && !t.rate);
    adv_battery_trend_update(&t,675000,4000);
    assert(t.state==ADV_DRAIN_LEARNING);
    feed(&t,660,-1,false);
    adv_battery_trend_update(&t,800000,3980);
    assert(t.state==ADV_DRAIN_LEARNING && !t.count);
    memset(&t,0,sizeof(t));
    for(int sec=0;sec<=660;sec+=5)adv_battery_trend_update(&t,sec*1000,3400);
    assert(t.state==ADV_DRAIN_UNKNOWN);
    puts("PASS: battery trend warmup, falling/rising/noise, sliding window, voltage step, ADC failure, time gap and clipped voltage");
}
