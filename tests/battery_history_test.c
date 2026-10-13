#include <assert.h>
#include <stdio.h>
#include "../src/battery_history.c"
static uint8_t disk[POINTS*12];
static size_t disk_size;
static int fail;
int fs_open(struct fs_file_t *f,const char *p,int flags) { assert(!strcmp(p,PATH));f->offset=0;if(fail)return fail;if(!(flags&FS_O_CREATE) && !disk_size)return -ENOENT;return 0; }
int fs_stat(const char *p,struct fs_dirent *e) { assert(!strcmp(p,PATH));if(!disk_size)return -ENOENT;e->size=disk_size;return 0; }
int fs_close(struct fs_file_t *f) { (void)f;return 0; }
int fs_sync(struct fs_file_t *f) { (void)f;return fail; }
int fs_seek(struct fs_file_t *f,off_t off,int whence) { assert(whence==FS_SEEK_SET && off>=0 && off<(off_t)sizeof(disk));if((size_t)off>disk_size)return -EINVAL;f->offset=off;return 0; }
ssize_t fs_read(struct fs_file_t *f,void *out,size_t n) { n=MIN(n,disk_size-f->offset);memcpy(out,disk+f->offset,n);f->offset+=n;return n; }
ssize_t fs_write(struct fs_file_t *f,const void *in,size_t n) { assert(f->offset+n<=sizeof(disk));memcpy(disk+f->offset,in,n);disk_size=MAX(disk_size,f->offset+n);f->offset+=n;return n; }
static void reset(void) { memset(points,0,sizeof(points));latest=total=sum=count=smoothed=0;last=0;previous_mv=0;ready=storage=false;first=true;gap=false;storage_error=0;k_msgq_purge(&history_writes); }
int main(void) {
    struct adv_battery_chart chart={0};
    adv_history_sample(0,4000);assert(!total);
    adv_history_storage_start(0);assert(ready && storage);
    adv_history_sample(0,4000);assert(total==1 && (points[1].mv&0x7fff)==4000);
    for(unsigned i=1;i<=12;i++)adv_history_sample(i*5000,3960);
    assert(total==2 && (points[2].mv&0x7fff)==3980);
    adv_history_storage_tick();adv_history_storage_tick();assert(disk_size==36);
    adv_history_snapshot(0,&chart);assert((chart.mv[179]&0x7fff)==3980);
    for(unsigned s=0;s<7;s++)assert(adv_history_minutes(s)>=(s?adv_history_minutes(s-1):10));
    reset();adv_history_storage_start(0);assert(latest==2);
    adv_history_sample(0,4200);assert(latest==3 && (points[3].mv&0x8000));adv_history_storage_tick();
    disk[24]^=1;reset();adv_history_storage_start(0);assert(latest==3 && !points[2].sequence);
    for(unsigned i=0;i<1500;i++) { adv_history_sample(i*60000LL,4000);adv_history_storage_tick(); }
    assert(disk_size==sizeof(disk));
    adv_history_snapshot(6,&chart);assert(chart.mv[179]);
    fail=-EIO;adv_history_sample(1500*60000LL,3900);adv_history_storage_tick();adv_history_snapshot(6,&chart);assert(chart.error==-EIO);
    fail=0;adv_history_storage_stop(-ENODEV);adv_history_sample(1501*60000LL,3900);assert(!k_msgq_num_used_get(&history_writes));
    reset();adv_history_storage_start(-ENODEV);adv_history_sample(0,4100);adv_history_snapshot(0,&chart);assert(chart.error==-ENODEV && chart.mv[179]);
    reset();ready=true;first=false;latest=16;
    for(unsigned i=1;i<=16;i++)points[i]=(struct point){i,(uint16_t)(4000-i*3)};
    adv_history_snapshot(0,&chart);assert(chart.eta==ADV_ETA_LEFT && chart.eta_minutes>0);
    unsigned expected=chart.eta_minutes;
    adv_history_snapshot(6,&chart);assert(chart.eta_minutes==expected);
    for(unsigned i=1;i<=16;i++)points[i].mv=3800+i*3;
    adv_history_snapshot(0,&chart);assert(chart.eta==ADV_ETA_FULL && chart.eta_minutes>0);
    for(unsigned i=1;i<=16;i++)points[i].mv=4080+i*3;
    adv_history_snapshot(0,&chart);assert(chart.eta==ADV_ETA_FULL && !chart.eta_minutes);
    points[12].mv|=BREAK;adv_history_snapshot(0,&chart);assert(!chart.eta_minutes);
    reset();adv_history_storage_start(-ENODEV);adv_history_sample(0,3900);
    adv_history_sample(5000,4200);adv_history_sample(60000,4200);
    assert(points[2].mv&BREAK);adv_history_snapshot(0,&chart);assert(!chart.eta_minutes);
    puts("PASS: minute averaging/EMA, zoom windows, bounded SD ring, reboot breaks, torn record rejection, RAM fallback");
}
