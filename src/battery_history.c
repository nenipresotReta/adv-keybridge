/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include "adv_battery_history.h"
#define POINTS 1441
#define PATH "/SD:/BATTERY.bin"
#define BREAK 0x8000
/* Fixed little-endian records, checksum last; a torn write loses one point. */
struct point { uint32_t sequence; uint16_t mv; };
static struct point points[POINTS];
static uint32_t latest, total, sum, count, smoothed;
static uint16_t previous_mv;
static int64_t last;
static bool ready, storage, first=true, gap;
static int storage_error;
K_MUTEX_DEFINE(history_lock);
K_MSGQ_DEFINE(history_writes,sizeof(struct point),16,4);
unsigned adv_history_minutes(unsigned scale) {
    static const unsigned windows[]={10,30,60,120,240,480,1440};
    return windows[scale<7?scale:2];
}
void adv_history_sample(int64_t now,uint16_t mv) {
    if(k_mutex_lock(&history_lock,K_NO_WAIT))return;
    if(!ready) { k_mutex_unlock(&history_lock);return; }
    if(mv>=2000 && mv<=4500) {
        int step=(int)mv-previous_mv;
        if(previous_mv && (step>=80 || step<=-80)) { gap=true;sum=count=0; }
        previous_mv=mv;sum+=mv;count++;
    } else { gap=true;previous_mv=0; }
    if(!first && now-last<60000) { k_mutex_unlock(&history_lock);return; }
    unsigned elapsed=first?1:(unsigned)((now-last)/60000);
    bool broken=first || gap || elapsed>1;
    if(count) {
        unsigned average=sum/count;
        smoothed=broken?average:(smoothed+average+1)/2;
    } else { smoothed=0;broken=true; }
    latest+=elapsed;
    struct point p={.sequence=latest,.mv=(uint16_t)(smoothed|(broken?BREAK:0))};
    points[latest%POINTS]=p;total++;
    if(storage && k_msgq_put(&history_writes,&p,K_NO_WAIT))storage_error=-ENOBUFS;
    first=false;gap=false;sum=count=0;last=now;
    k_mutex_unlock(&history_lock);
}
/* Voltage extrapolation only: reject short, reversed, noisy or tapering trends. */
static void estimate(struct adv_battery_chart *out) {
    unsigned values[16],n=0;
    for(unsigned age=0;age<16 && age<latest;age++) {
        struct point p=points[(latest-age)%POINTS];
        unsigned mv=p.mv&0x7fff;
        if(p.sequence!=latest-age || !mv || (p.mv&BREAK))break;
        values[n++]=mv;
    }
    if(n<11)return;
    int change=(int)values[0]-values[n-1];
    unsigned mid=(n-1)/2;
    int recent=(int)values[0]-values[mid],older=(int)values[mid]-values[n-1];
    if(change>-15 && change<15)return;
    out->eta=change>0?ADV_ETA_FULL:ADV_ETA_LEFT;
    if((change>0 && (recent<=0 || older<=0)) || (change<0 && (recent>=0 || older>=0)))return;
    unsigned a=recent<0?-recent:recent,b=older<0?-older:older;
    if(a>2*b || b>2*a)return;
    for(unsigned i=0;i<n;i++) {
        int predicted=(int)values[0]-change*(int)i/(int)(n-1);
        int residual=(int)values[i]-predicted;
        if(residual>15 || residual<-15)return;
    }
    unsigned current=values[0];
    /* Voltage alone cannot predict the constant-voltage charging tail. */
    if(current<=3450 || current>=4100)return;
    unsigned distance=change>0?4200-current:current-3450;
    unsigned magnitude=change>0?change:-change;
    unsigned minutes=(distance*(n-1)+magnitude/2)/magnitude;
    if(!minutes || minutes>2880)return;
    out->eta_minutes=((minutes+4)/5)*5;
}
void adv_history_snapshot(unsigned scale,struct adv_battery_chart *out) {
    if(k_mutex_lock(&history_lock,K_NO_WAIT))return;
    memset(out,0,sizeof(*out));
    if(!ready) { k_mutex_unlock(&history_lock);return; }
    out->revision=total;out->error=storage_error;
    if(!first && !gap)estimate(out);
    unsigned minutes=adv_history_minutes(scale);
    /* Iterate oldest to newest so each pixel retains its newest measurement. */
    bool missing=false;
    for(unsigned age=minutes+1;age-->0;) {
        if(age>latest)continue;
        uint32_t sequence=latest-age;struct point p=points[sequence%POINTS];
        if(!sequence || p.sequence!=sequence) { missing=true;continue; }
        unsigned x=(minutes-age)*(ADV_HISTORY_COLUMNS-1)/minutes;
        out->mv[x]=p.mv|(out->mv[x]&BREAK)|(missing?BREAK:0);
        missing=false;
    }
    k_mutex_unlock(&history_lock);
}
static uint32_t get32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void put32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8)); }
static uint32_t checksum(const uint8_t *p) { uint32_t n=2166136261u;for(unsigned i=0;i<8;i++)n=(n^p[i])*16777619u;return n; }
void adv_history_storage_start(int available) {
    int err=available;struct fs_file_t file;fs_file_t_init(&file);
    if(!err) {
        err=fs_open(&file,PATH,FS_O_READ);
        if(!err) {
            for(unsigned i=0;i<POINTS;i++) {
                uint8_t raw[12];ssize_t n=fs_read(&file,raw,sizeof(raw));
                if(n==0)break;
                if(n<0) { err=(int)n;break; }
                if(n!=sizeof(raw))break;
                uint32_t seq=get32(raw),value=get32(raw+4);
                if(!seq || seq%POINTS!=i || get32(raw+8)!=checksum(raw) || value>65535)continue;
                unsigned mv=value&~BREAK;
                if(mv && (mv<2000 || mv>4500))continue;
                points[i]=(struct point){seq,(uint16_t)value};
                if(seq>latest)latest=seq;
                total++;
            }
            int closed=fs_close(&file);if(!err)err=closed;
        } else if(err==-ENOENT)err=0;
    }
    k_mutex_lock(&history_lock,K_FOREVER);
    storage=!err;storage_error=err;ready=true;
    k_mutex_unlock(&history_lock);
}
void adv_history_storage_tick(void) {
    struct point p;
    if(k_msgq_get(&history_writes,&p,K_NO_WAIT))return;
    uint8_t raw[12];put32(raw,p.sequence);put32(raw+4,p.mv);put32(raw+8,checksum(raw));
    struct fs_dirent entry;
    int err=fs_stat(PATH,&entry);
    size_t size=err?0:entry.size;
    if(err==-ENOENT)err=0;
    struct fs_file_t file;fs_file_t_init(&file);
    if(!err)err=fs_open(&file,PATH,FS_O_CREATE|FS_O_WRITE);
    if(!err) {
        size_t offset=(p.sequence%POINTS)*sizeof(raw);
        /* Zephyr rejects seeks past EOF, even when the file is writable. */
        if(size<offset) {
            err=fs_seek(&file,size,FS_SEEK_SET);
            const uint8_t zeros[64]={0};
            while(!err && size<offset) {
                size_t chunk=MIN(sizeof(zeros),offset-size);
                ssize_t n=fs_write(&file,zeros,chunk);
                if(n<0)err=(int)n;
                else if((size_t)n!=chunk)err=-ENOSPC;
                else size+=chunk;
            }
        }
        if(!err)err=fs_seek(&file,offset,FS_SEEK_SET);
        if(!err) { ssize_t n=fs_write(&file,raw,sizeof(raw));err=n<0?(int)n:n==sizeof(raw)?0:-ENOSPC; }
        if(!err)err=fs_sync(&file);
        int closed=fs_close(&file);if(!err)err=closed;
    }
    k_mutex_lock(&history_lock,K_FOREVER);storage_error=err;k_mutex_unlock(&history_lock);
}

int adv_history_storage_flush(void) {
    for(unsigned i=0;i<16 && k_msgq_num_used_get(&history_writes);i++) {
        adv_history_storage_tick();
        k_mutex_lock(&history_lock,K_FOREVER);
        int err=storage_error;
        k_mutex_unlock(&history_lock);
        if(err)return err;
    }
    return k_msgq_num_used_get(&history_writes)?-EBUSY:0;
}

void adv_history_storage_stop(int err) {
    k_mutex_lock(&history_lock,K_FOREVER);storage=false;storage_error=err;k_mutex_unlock(&history_lock);
    k_msgq_purge(&history_writes);
}
