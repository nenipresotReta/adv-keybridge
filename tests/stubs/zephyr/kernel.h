#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#define ARG_UNUSED(x) (void)(x)
#define K_NO_WAIT 0
#define K_MSEC(n) (n)
#define K_SECONDS(n) ((n)*1000)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
void k_msleep(int32_t);
#define K_THREAD_DEFINE(name, stack, entry, a, b, c, priority, flags, delay) \
    static void (*const name)(void *, void *, void *) __attribute__((unused)) = entry
uint32_t k_cycle_get_32(void);
#define k_us_to_cyc_ceil32(us) ((uint32_t)(us))
struct k_msgq { unsigned size, capacity, head, count; uint8_t *data; };
#define K_MSGQ_DEFINE(name, size, capacity, alignment) \
    static uint8_t name##_data[(size)*(capacity)]; \
    struct k_msgq name={(size),(capacity),0,0,name##_data}
static inline int k_msgq_put(struct k_msgq *q,const void *p,int wait) {
    assert(wait==K_NO_WAIT);
    if(q->count==q->capacity)return -ENOMSG;
    memcpy(q->data+((q->head+q->count)%q->capacity)*q->size,p,q->size);q->count++;
    return 0;
}
static inline int k_msgq_get(struct k_msgq *q,void *p,int wait) {
#ifdef TEST_MSGQ_WAIT
    if(wait) TEST_MSGQ_WAIT(q,wait);
#else
    assert(wait==K_NO_WAIT);
#endif
    if(!q->count)return -ENOMSG;
    memcpy(p,q->data+q->head*q->size,q->size);q->head=(q->head+1)%q->capacity;q->count--;
    return 0;
}
static inline void k_msgq_purge(struct k_msgq *q) { q->head=q->count=0; }
static inline unsigned k_msgq_num_used_get(struct k_msgq *q) { return q->count; }
struct k_work { void (*handler)(struct k_work *); bool pending; };
struct k_work_delayable { struct k_work work; };
struct k_work_q { unsigned unused; };
#define K_WORK_DELAYABLE_DEFINE(name,handler) struct k_work_delayable name={{handler,false}}
int k_work_reschedule_for_queue(struct k_work_q *,struct k_work_delayable *,int);
extern struct k_work *test_work;
#define K_WORK_DEFINE(name, handler) struct k_work name={handler,false}
static inline int k_work_submit(struct k_work *w) { w->pending=true;test_work=w;return 1; }
#ifdef TEST_CLOCK
extern int64_t test_clock;
static inline int64_t k_uptime_get(void) { return test_clock; }
#else
static inline int64_t k_uptime_get(void) { return 100; }
#endif
struct k_sem { unsigned count; };
#define K_SEM_DEFINE(name,initial,limit) struct k_sem name={initial}
static inline void k_sem_give(struct k_sem *s) { s->count=1; }
static inline int k_sem_take(struct k_sem *s,int timeout) { (void)timeout;if(!s->count)return -EAGAIN;s->count=0;return 0; }

struct k_mutex { bool locked; };
#define K_MUTEX_DEFINE(name) struct k_mutex name
static inline int k_mutex_lock(struct k_mutex *m,int wait) { (void)wait;if(m->locked)return -EBUSY;m->locked=true;return 0; }
static inline void k_mutex_unlock(struct k_mutex *m) { m->locked=false; }
