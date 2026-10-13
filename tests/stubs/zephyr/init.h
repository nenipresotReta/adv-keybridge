#pragma once
#define SYS_INIT(fn, level, priority) \
    static int (*const init_##fn)(void) __attribute__((unused)) = fn
