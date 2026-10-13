/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zmk/ble.h>
#include <zmk/workqueue.h>
#include "adv_pages.h"
#include "adv_screen.h"

/* The high byte is the visible page ID; low byte is its enabled mask. */
static atomic_t snapshot=31, last_error, saved_mask=31;
K_MUTEX_DEFINE(pages_lock);

void adv_pages_get(struct adv_pages_state *s) {
    unsigned value=(unsigned)atomic_get(&snapshot);
    *s=(struct adv_pages_state){.enabled=value&31,.current=(value>>8)&7};
}
extern void adv_ble_policy_refresh(void);
unsigned adv_pages_enabled_mask(void) { return (unsigned)atomic_get(&snapshot)&31; }
int adv_pages_error(void) { return atomic_get(&last_error); }
static void save(struct k_work *work) {
    ARG_UNUSED(work);
    uint8_t mask=(uint8_t)atomic_get(&saved_mask);
    int err=settings_save_one("adv_pages/enabled",&mask,1);
    if(err)atomic_set(&last_error,err);
}
K_WORK_DELAYABLE_DEFINE(save_pages,save);
static int load(const char *name,size_t len,settings_read_cb read,void *arg) {
    if(strcmp(name,"enabled"))return -ENOENT;
    uint8_t mask;
    if(len!=1)return -EINVAL;
    int n=read(arg,&mask,1);
    if(n<0)return n;
    if(n!=1 || (mask&~63) || !(mask&31) || adv_pages_count(mask)>5)return -EINVAL;
    mask &= 31; /* This build exposes Bluetooth pages only. */
    atomic_set(&snapshot,mask);atomic_set(&saved_mask,mask);
    return 0;
}
static int restored(void) {
    struct adv_pages_state s;adv_pages_get(&s);
    unsigned profile=zmk_ble_active_profile_index();
    s.current=(s.enabled&(1u<<profile))?profile:(unsigned)adv_pages_at(s.enabled,0);
    atomic_set(&snapshot,s.enabled|(s.current<<8));
    int err=zmk_ble_prof_select(s.current);
    adv_ble_policy_refresh();
    return err;
}
SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(adv_pages_preferences,"adv_pages",NULL,load,restored,NULL,90);

static int apply(struct adv_pages_state next) {
    unsigned previous=(unsigned)atomic_get(&snapshot);
    atomic_set(&snapshot,next.enabled|(next.current<<8));
    int err=zmk_ble_prof_select(next.current);
    if(err) { atomic_set(&snapshot,previous);return err; }
    adv_ble_policy_refresh();
    adv_screen_activity();
    return 0;
}
int adv_pages_select(unsigned position) {
    if(k_mutex_lock(&pages_lock,K_NO_WAIT))return -EBUSY;
    struct adv_pages_state s;adv_pages_get(&s);
    int id=adv_pages_at(s.enabled,position);
    int err=id<0?0:apply((struct adv_pages_state){s.enabled,(uint8_t)id});
    atomic_set(&last_error,err);k_mutex_unlock(&pages_lock);return err;
}
int adv_pages_next(void) {
    struct adv_pages_state s;adv_pages_get(&s);
    unsigned count=adv_pages_count(s.enabled);
    for(unsigned i=0;i<count;i++)if(adv_pages_at(s.enabled,i)==s.current)
        return adv_pages_select((i+1)%count);
    return adv_pages_select(0);
}
int adv_pages_toggle(unsigned id) {
    if(k_mutex_lock(&pages_lock,K_NO_WAIT))return -EBUSY;
    struct adv_pages_state s;adv_pages_get(&s);
    int err=adv_pages_toggle_state(&s,id);
    if(!err)err=apply(s);
    if(!err) {
        atomic_set(&saved_mask,s.enabled);
        k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(),&save_pages,K_SECONDS(2));
    }
    atomic_set(&last_error,err);k_mutex_unlock(&pages_lock);return err;
}
