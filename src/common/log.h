#ifndef LOG_H
#define LOG_H

#include <stdio.h>

/* Logs go to stdout (visible in a debugger / RPCS3 console) AND to a file on
 * the PS3 filesystem so they can be retrieved without a debugger. The file
 * backend is implemented in log.c (ml_logf). */
void ml_logf(const char *tag, const char *fmt, ...);

#define LOGI(...) ml_logf("[I] ", __VA_ARGS__)
#define LOGW(...) ml_logf("[W] ", __VA_ARGS__)
#define LOGE(...) ml_logf("[E] ", __VA_ARGS__)

#endif /* LOG_H */
