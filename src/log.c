#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

void log_prefix(const char *prefix, const char *format, va_list ap)
{
    printf("%s: ", prefix);
    vprintf(format, ap);
    printf("\n");
}

#if LOGLEVEL >= 2
void log_debug(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_prefix("DEBUG", format, args);
    va_end(args);
}
#endif

#if LOGLEVEL >= 1
void log_info(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_prefix("INFO", format, args);
    va_end(args);
}
#endif

void log_fatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_prefix("FATAL", format, args);
    va_end(args);
    exit(1);
}
