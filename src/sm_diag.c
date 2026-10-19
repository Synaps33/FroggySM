#include "sm_diag.h"

#ifdef SM_DEVICE
#include <stdio.h>
#include <stdarg.h>

void sm_diag(const char *fmt, ...)
{
    static int enabled = -1;
    if (enabled < 0) {
        FILE *flag = fopen("/mnt/sda1/bios/enable_xlog.txt", "rb");
        enabled = flag ? 1 : 0;
        if (flag) fclose(flag);
    }
    if (!enabled) return;

    FILE *f = fopen("/mnt/sda1/froggysm_diag.log", "a");
    if (!f) return;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);

    fputc('\n', f);
    fclose(f);
}
#else
void sm_diag(const char *fmt, ...)
{
    (void)fmt;
}
#endif
