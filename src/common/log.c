#include "common/log.h"

#include <stdarg.h>

/* Persisted log file. On a retail PS3 the debug console is invisible, so we
 * mirror every line to a file in the app's USRDIR (FTP-retrievable). We try
 * the installed title dir first, then the CWD, then a USB stick. */
static FILE *g_log = NULL;

static FILE *open_log(void)
{
    if (g_log)
        return g_log;

    static const char *paths[] = {
        "/dev_hdd0/game/MLGHT0000/USRDIR/moonlight.log",
        "moonlight.log",
        "mass0:/moonlight.log",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        g_log = fopen(paths[i], "w"); /* fresh log each launch */
        if (g_log) {
            fprintf(g_log, "moonlight-ps3: logging to %s\n", paths[i]);
            fflush(g_log);
            break;
        }
    }
    return g_log;
}

void ml_logf(const char *tag, const char *fmt, ...)
{
    /* Debug console / emulator stdout. */
    va_list ap;
    va_start(ap, fmt);
    fputs(tag, stdout);
    vfprintf(stdout, fmt, ap);
    va_end(ap);

    /* Persisted file (best-effort). */
    FILE *f = open_log();
    if (f) {
        va_list ap2;
        va_start(ap2, fmt);
        fputs(tag, f);
        vfprintf(f, fmt, ap2);
        va_end(ap2);
        fflush(f);
    }
}
