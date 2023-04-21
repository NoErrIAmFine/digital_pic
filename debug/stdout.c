#include <stdio.h>
#include <string.h>

#include "debug_manager.h"

static int stdout_print(const char *str)
{
    printf("%s",str);
    return strlen(str);
}

static struct debuger_struct stdout_debuger = {
    .name = "stdout",
    .print = stdout_print,
    .is_initialized = 1,
    .is_enable = 1,
};

int stdout_init(void)
{
    return register_debuger(&stdout_debuger);
}