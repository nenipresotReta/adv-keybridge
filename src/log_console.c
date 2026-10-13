/* SPDX-License-Identifier: MIT
 * Read-only log protocol on Zephyr shell. No file/USB-register access here.
 */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/shell/shell.h>
#include "adv_sd_log.h"

static bool number(const char *s,uint32_t *value) {
    if (!s || !*s) return false;
    uint32_t n=0;
    for (;*s;s++) {
        if (*s<'0' || *s>'9' || n>(UINT32_MAX-(unsigned)(*s-'0'))/10) return false;
        n=n*10+(unsigned)(*s-'0');
    }
    *value=n;return true;
}
static int command(const struct shell *shell,size_t argc,char **argv) {
    uint32_t index,offset=0;
    bool read=argc==4 && !strcmp(argv[1],"read");
    if ((!read && (argc!=3 || strcmp(argv[1],"next"))) || !number(argv[2],&index) ||
        (read && !number(argv[3],&offset))) {
        shell_print(shell,"@ADVLOG ERROR -22");return -EINVAL;
    }
    struct adv_log_reply reply;
    int error=adv_sd_log_fetch(read?ADV_LOG_READ:ADV_LOG_NEXT,index,offset,&reply);
    if (error) { shell_print(shell,"@ADVLOG ERROR %d",error);return error; }
    if (!read) {
        shell_print(shell,"@ADVLOG FILE %u %u",reply.file,reply.size);return 0;
    }
    static const char digits[]="0123456789abcdef";
    char hex[ADV_LOG_CHUNK*2+1];uint32_t checksum=2166136261u;
    for (unsigned i=0;i<reply.length;i++) {
        hex[2*i]=digits[reply.data[i]>>4];hex[2*i+1]=digits[reply.data[i]&15];
        checksum=(checksum^reply.data[i])*16777619u;
    }
    hex[2*reply.length]=0;
    shell_print(shell,"@ADVLOG DATA %u %u %u %u %08x %s",reply.file,reply.offset,
                reply.size,reply.length,checksum,hex);
    return 0;
}
SHELL_CMD_ARG_REGISTER(advlog,NULL,"Read SD logs: next INDEX | read INDEX OFFSET",command,3,1);
