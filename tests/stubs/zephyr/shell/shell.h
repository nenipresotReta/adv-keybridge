#pragma once
#include <stdbool.h>
#include <stddef.h>
struct shell { int unused; };
void shell_print(const struct shell *,const char *,...);
#define SHELL_CMD_ARG_REGISTER(name,sub,help,handler,required,optional) static const void *name __attribute__((unused))=handler
