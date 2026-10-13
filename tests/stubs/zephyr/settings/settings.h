#pragma once
#include <stddef.h>
typedef int (*settings_read_cb)(void *,void *,size_t);
#define SETTINGS_STATIC_HANDLER_DEFINE(...)
int settings_save_one(const char *,const void *,size_t);
#define SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(...)
