#ifndef LOG_H
#define LOG_H

#include <stdio.h>

/* Minimal logging. PSL1GHT provides stdio; on HW these go to the debug console. */
#define LOGI(...) fprintf(stdout, "[I] " __VA_ARGS__)
#define LOGW(...) fprintf(stderr, "[W] " __VA_ARGS__)
#define LOGE(...) fprintf(stderr, "[E] " __VA_ARGS__)

#endif /* LOG_H */
