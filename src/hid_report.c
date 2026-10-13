/* SPDX-License-Identifier: MIT
 * Bounded HID short-item parser for keyboard and absolute Consumer Control.
 * Other input fields still advance the report offset, but are never mapped.
 */
#include <string.h>
#include "adv_hid.h"
struct globals { uint32_t page,size,count; int32_t logical_min,logical_max; uint8_t id; };
struct locals { uint32_t usages[32],min,max; uint8_t count; bool range; };
static uint32_t usage_page(uint32_t u,const struct globals *g) { return u>>16 ? u>>16 : g->page; }
static uint16_t usage_id(uint32_t u) { return (uint16_t)u; }
static int report_index(struct adv_hid_layout *l,uint8_t id) {
    for(unsigned i=0;i<l->report_count;i++) if(l->reports[i].id==id) return i;
    if(l->report_count==ADV_HID_REPORTS) return -1;
    unsigned i=l->report_count++; l->reports[i].id=id; return i;
}
static bool field(struct adv_hid_layout *l,int report,uint16_t offset,uint16_t count,
                  uint32_t min,uint32_t max,const struct globals *g,bool variable) {
    uint32_t page=usage_page(min,g);
    if(page!=7 && page!=12) return true;
    if(usage_page(max,g)!=page || usage_id(max)<usage_id(min)) return false;
    if(g->size==0 || g->size>16 || l->field_count==ADV_HID_FIELDS) return false;
    /* Array selectors require a range; variable fields with widths >1 are
     * treated as switches only when their logical minimum is zero. */
    if(variable && (g->logical_min!=0 || g->logical_max>1)) return true;
    if(!variable && g->logical_min<0) return false;
    struct adv_hid_field *f=&l->fields[l->field_count++];
    *f=(struct adv_hid_field){offset,count,usage_id(min),usage_id(max),g->logical_min,
        g->size,report,page,variable};
    uint8_t mask=page==7?ADV_HID_KEYBOARD:ADV_HID_CONSUMER;
    l->reports[report].pages|=mask; l->pages|=mask;
    return true;
}
bool adv_hid_parse(struct adv_hid_layout *l,const uint8_t *desc,size_t length) {
    memset(l,0,sizeof(*l));
    struct globals g={0},stack[4]; unsigned depth=0;
    struct locals local={0};
    for(size_t pos=0;pos<length;) {
        uint8_t prefix=desc[pos++];
        if(prefix==0xfe) return false; /* Long items unsupported. */
        unsigned n=prefix&3; if(n==3)n=4;
        if(n>length-pos) return false;
        uint32_t val=0;
        for(unsigned b=0;b<n;b++) val|=(uint32_t)desc[pos++]<<(8*b);
        unsigned type=(prefix>>2)&3,tag=prefix>>4;
        if(type==1) {
            switch(tag) {
              case 0:g.page=val;break;
              case 1:g.logical_min=n==1?(int8_t)val:n==2?(int16_t)val:(int32_t)val;break;
              case 2:g.logical_max=g.logical_min<0 ? (n==1?(int8_t)val:n==2?(int16_t)val:(int32_t)val) : (int32_t)val;break;
              case 7:g.size=val;break;
              case 8:if(!val || val>255)return false;g.id=val;l->has_ids=true;break;
              case 9:g.count=val;break;
              case 10:if(depth==4)return false;stack[depth++]=g;break;
              case 11:if(!depth)return false;g=stack[--depth];break;
              default:break;
            }
        } else if(type==2) {
            switch(tag) {
              case 0:if(local.count==32)return false;local.usages[local.count++]=val;break;
              case 1:local.min=val;local.range=true;break;
              case 2:local.max=val;break;
              case 10:return false; /* Alternate usage sets unsupported. */
              default:break;
            }
        } else if(type==0) {
            if(tag==8) { /* Input; output/feature have separate offsets. */
                if(!g.size || !g.count || g.size>512 || g.count>512 || g.size*g.count>512) return false;
                int r=report_index(l,g.id); if(r<0)return false;
                struct adv_hid_report *report=&l->reports[r];
                uint16_t offset=report->bits;
                if(offset+g.size*g.count>512)return false;
                report->bits+=g.size*g.count;
                if(!(val&1) && !(val&4)) { /* Data, absolute. */
                    bool variable=val&2;
                    if(local.range) {
                        if(!field(l,r,offset,g.count,local.min,local.max,&g,variable))return false;
                    } else if(local.count && variable) {
                        for(unsigned j=0;j<g.count;j++) {
                            uint32_t u=local.usages[j<local.count?j:local.count-1];
                            if(!field(l,r,offset+j*g.size,1,u,u,&g,true))return false;
                        }
                    } else if(local.count && (g.page==7 || g.page==12)) return false;
                }
            }
            /* HID local state expires at every Main item, including collections. */
            memset(&local,0,sizeof(local));
        }
    }
    if(depth || !l->field_count) return false;
    if(l->has_ids) for(unsigned r=0;r<l->report_count;r++) if(!l->reports[r].id)return false;
    return true;
}
static uint32_t bits(const uint8_t *data,unsigned offset,unsigned size) {
    uint32_t value=0;
    for(unsigned b=0;b<size;b++) if(data[(offset+b)/8]&(1u<<((offset+b)%8))) value|=1u<<b;
    return value;
}
bool adv_consumer_add(uint16_t keys[ADV_CONSUMER_KEYS],uint16_t usage) {
    if(!usage || usage>0xfff)return true; /* Outside the existing ZMK report. */
    for(unsigned k=0;k<ADV_CONSUMER_KEYS;k++) {
        if(keys[k]==usage)return true;
        if(!keys[k]) { keys[k]=usage;return true; }
    }
    return false;
}
int adv_hid_decode(const struct adv_hid_layout *l,const uint8_t *data,size_t length,
                   struct adv_hid_values *out) {
    uint8_t id=0;
    if(l->has_ids) { if(!length)return -1;id=*data++;length--; }
    int r=-1;
    for(unsigned i=0;i<l->report_count;i++) if(l->reports[i].id==id) { r=i;break; }
    if(r<0 || !l->reports[r].pages || length<(l->reports[r].bits+7u)/8u)return -1;
    struct adv_hid_values values={0};
    for(unsigned i=0;i<l->field_count;i++) {
        const struct adv_hid_field *f=&l->fields[i]; if(f->report!=r)continue;
        for(unsigned j=0;j<f->count;j++) {
            uint32_t raw=bits(data,f->offset+j*f->size,f->size);
            if(f->variable && !raw)continue;
            int64_t usage=f->variable ? f->usage_min+j : (int64_t)raw-f->logical_min+f->usage_min;
            if(f->variable && usage>f->usage_max)usage=f->usage_max;
            if(usage<f->usage_min || usage>f->usage_max || usage==0)continue;
            if(f->page==7) {
                if(usage<4)return -1; /* ErrorRollOver, POSTFail, ErrorUndefined. */
                if(usage<=255)values.keys[usage/8]|=1u<<(usage%8);
            } else if(!adv_consumer_add(values.consumer,usage))return -1;
        }
    }
    *out=values;return r;
}
bool adv_hid_apply(const struct adv_hid_layout *l,struct adv_hid_values held[ADV_HID_REPORTS],
                    unsigned report,const struct adv_hid_values *values,struct adv_hid_values *out) {
    if(report>=l->report_count)return false;
    struct adv_hid_values combined={0};
    for(unsigned r=0;r<l->report_count;r++) {
        const struct adv_hid_values *v=r==report?values:&held[r];
        for(unsigned k=0;k<ADV_KEY_BYTES;k++)combined.keys[k]|=v->keys[k];
        for(unsigned k=0;k<ADV_CONSUMER_KEYS;k++)
            if(!adv_consumer_add(combined.consumer,v->consumer[k]))return false;
    }
    held[report]=*values;*out=combined;return true;
}
