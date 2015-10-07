#ifndef LOG_H
#define LOG_H

#if LOGLEVEL >= 2
void log_debug(const char *format, ...);
#else
#define log_debug(...)
#endif

#if LOGLEVEL >= 1
void log_info(const char *format, ...);
#else
#define log_info(...)
#endif

void log_fatal(const char *format, ...);

#endif
