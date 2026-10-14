#pragma once
typedef int atomic_t;
typedef int atomic_val_t;
static inline int atomic_or(atomic_t *a,int n) { int old=*a;*a|=n;return old; }
static inline int atomic_and(atomic_t *a,int n) { int old=*a;*a&=n;return old; }
static inline int atomic_get(const atomic_t *a) { return *a; }
static inline int atomic_set(atomic_t *a,int v) { int old=*a;*a=v;return old; }
static inline void atomic_clear(atomic_t *a) { *a=0; }
static inline int atomic_inc(atomic_t *a) { return (*a)++; }
static inline int atomic_cas(atomic_t *a,int old,int next) {
    if(*a!=old)return 0;
    *a=next;return 1;
}

static inline int atomic_add(atomic_t *a,int n) { int old=*a;*a+=n;return old; }
