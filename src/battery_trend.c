/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "adv_battery_trend.h"
static void reset(struct adv_battery_trend *t,int64_t now,uint16_t mv) {
    memset(t,0,sizeof(*t));t->started=mv!=0;t->bucket_start=now;t->last_sample=now;
    t->last_mv=mv;t->state=mv?ADV_DRAIN_LEARNING:ADV_DRAIN_UNKNOWN;
}
void adv_battery_trend_update(struct adv_battery_trend *t,int64_t now,uint16_t mv) {
    if (!mv) { reset(t,now,0);return; }
    int step=(int)mv-t->last_mv;
    if (!t->started || now<=t->last_sample || now-t->last_sample>15000 || step>=80 || step<=-80)
        reset(t,now,mv);
    t->last_sample=now;t->last_mv=mv;t->sum+=mv;t->samples++;
    if (now-t->bucket_start<30000) return;
    uint16_t average=t->sum/t->samples;
    t->mv[t->next]=average;t->seconds[t->next]=(uint32_t)(now/1000);
    t->next=(t->next+1)%ADV_TREND_POINTS;
    if(t->count<ADV_TREND_POINTS)t->count++;
    t->sum=t->samples=0;t->bucket_start=now;t->rate=0;
    unsigned first=t->count==ADV_TREND_POINTS?t->next:0;
    uint32_t origin=t->seconds[first];
    if (t->count<11 || now/1000-origin<300) {t->state=ADV_DRAIN_LEARNING;return;}
    if (average<=3450 || average>=4200) {t->state=ADV_DRAIN_UNKNOWN;return;}
    int64_t sx=0,sy=0,sxx=0,sxy=0;
    for(unsigned i=0;i<t->count;i++) {
        int64_t x=t->seconds[i]-origin,y=t->mv[i];
        sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;
    }
    int64_t numerator=(int64_t)t->count*sxy-sx*sy;
    int64_t denominator=(int64_t)t->count*sxx-sx*sx;
    if(denominator<=0){t->state=ADV_DRAIN_LEARNING;return;}
    int64_t mv_per_hour=numerator*3600/denominator;
    if(mv_per_hour>=15){t->state=ADV_DRAIN_RISING;return;}
    if(mv_per_hour>-15){t->state=ADV_DRAIN_STABLE;return;}
    t->state=ADV_DRAIN_FALLING;
    int64_t rate=-mv_per_hour*1000/750;
    t->rate=rate>9999?9999:(uint16_t)rate;
}
