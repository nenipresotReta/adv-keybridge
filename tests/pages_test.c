#include <assert.h>
#include <stdio.h>
#include "../src/pages.c"
static unsigned profile, scheduled;
static int select_error;
static struct k_work_q lowprio;
unsigned zmk_ble_active_profile_index(void) { return profile; }
int zmk_ble_prof_select(unsigned id) { assert(id<5);if(select_error)return select_error;profile=id;return 0; }
void adv_screen_activity(void) {}
struct k_work_q *zmk_workqueue_lowprio_work_q(void) { return &lowprio; }
int k_work_reschedule_for_queue(struct k_work_q *q,struct k_work_delayable *w,int delay) {
    assert(q==&lowprio && w==&save_pages && delay==2000);scheduled++;return 0;
}
int settings_save_one(const char *name,const void *p,size_t n) {
    assert(!strcmp(name,"adv_pages/enabled") && n==1 && *(const uint8_t*)p==saved_mask);return 0;
}
static int read_mask(void *arg,void *out,size_t n) { assert(n==1);*(uint8_t*)out=*(uint8_t*)arg;return 1; }
int main(void) {
    for(unsigned mask=1;mask<32;mask++)for(unsigned current=0;current<5;current++)for(unsigned id=0;id<6;id++) {
        if(!(mask&31) || adv_pages_count(mask)>5 || !(mask&(1u<<current)))continue;
        struct adv_pages_state s={mask,current},old=s;
        int err=adv_pages_toggle_state(&s,id);
        if(err)assert(s.enabled==old.enabled && s.current==old.current);
        else assert((s.enabled&31) && adv_pages_count(s.enabled)<=5 && (s.enabled&(1u<<s.current)));
    }
    assert(adv_pages_toggle(5)==-EINVAL);
    assert(!adv_pages_toggle(2) && !adv_pages_toggle(4));
    assert(!adv_pages_select(2) && profile==3);
    assert(!adv_pages_select(3) && profile==3);
    assert(!adv_pages_next() && profile==0);
    assert(!adv_pages_next() && profile==1);
    assert(!adv_pages_next() && profile==3);
    assert(!adv_pages_toggle(3) && profile==0);
    assert(!adv_pages_toggle(1));assert(adv_pages_toggle(0)==-EINVAL);
    select_error=-EIO;assert(adv_pages_select(0)==-EIO);select_error=0;
    uint8_t invalid=64;assert(load("enabled",1,read_mask,&invalid)==-EINVAL);
    uint8_t valid=6;assert(!load("enabled",1,read_mask,&valid));profile=4;assert(!restored() && profile==1);
    uint8_t legacy=35;assert(!load("enabled",1,read_mask,&legacy));
    struct adv_pages_state migrated;adv_pages_get(&migrated);assert(migrated.enabled==3);
    save(NULL);assert(scheduled);
    puts("PASS: production page limits, compact shortcuts, cycling, BLE failure, persistence");
}
