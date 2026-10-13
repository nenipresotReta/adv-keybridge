#pragma once
struct log_backend { int unused; };
union log_msg_generic { int log; };
struct log_backend_api {
 void (*process)(const struct log_backend *,union log_msg_generic *);
 void (*panic)(const struct log_backend *);
 void (*dropped)(const struct log_backend *,uint32_t);
};
#define LOG_BACKEND_DEFINE(name,api,autostart) static const void *name __attribute__((unused))=&api
