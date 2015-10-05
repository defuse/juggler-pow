#include "log.h"

#include <stdio.h>
#include <stdlib.h>

void log_debug(const char *msg)
{
    printf("DEBUG: %s\n", msg);
}

void log_fatal(const char *msg)
{
    printf("FATAL: %s\n", msg);
    exit(1);
}
