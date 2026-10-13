#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "../src/log_console.c"
static char output[512];static unsigned calls;
void shell_print(const struct shell *s,const char *fmt,...) { (void)s;va_list a;va_start(a,fmt);vsnprintf(output,sizeof(output),fmt,a);va_end(a); }
int adv_sd_log_fetch(enum adv_log_operation op,uint32_t n,uint32_t off,struct adv_log_reply *r) {
 calls++;*r=(struct adv_log_reply){.file=n,.offset=off,.size=3,.length=3,.data={'a','b','c'}};assert(op==ADV_LOG_NEXT||op==ADV_LOG_READ);return 0;
}
int main(void) {
 uint32_t v;assert(!number("-1",&v)&&!number("4294967296",&v)&&!number("1x",&v));
 assert(number("4294967295",&v)&&v==UINT32_MAX);
 char *next[]={"advlog","next","0"};assert(!command(NULL,3,next));assert(!strcmp(output,"@ADVLOG FILE 0 3"));
 char *read[]={"advlog","read","0","0"};assert(!command(NULL,4,read));assert(!strcmp(output,"@ADVLOG DATA 0 0 3 3 1a47e90b 616263"));
 char *bad[]={"advlog","read","../file","0"};assert(command(NULL,4,bad)==-EINVAL && calls==2);
 puts("PASS: production export command bounds, read-only grammar, frame and checksum");
}
