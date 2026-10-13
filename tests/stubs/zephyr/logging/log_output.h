#pragma once
#define LOG_OUTPUT_FLAG_LEVEL 1
#define LOG_OUTPUT_FLAG_TIMESTAMP 2
#define LOG_OUTPUT_FLAG_FORMAT_TIMESTAMP 4
#define LOG_OUTPUT_FLAG_CRLF_LFONLY 8
struct log_output { int (*write)(uint8_t *,size_t,void *);uint8_t *buffer;size_t size; };
#define LOG_OUTPUT_DEFINE(name,fn,buffer,size) static struct log_output name={fn,buffer,size}
static inline void log_output_msg_process(const struct log_output *o,int *m,unsigned f) { (void)o;(void)m;(void)f; }
static inline void log_output_flush(const struct log_output *o) { (void)o; }
