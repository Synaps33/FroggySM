#ifndef SM_DIRENT_H
#define SM_DIRENT_H

/*
 * Directory iteration shim.
 *
 * The GB300 / SF2000 core runs against the multicore tree's own dirent.h,
 * which supplies opendir/readdir/closedir and a `d_type` field using its
 * DTYPE_* constants (see include/dirent.h in sf2000_multicore).  The host
 * test runner uses POSIX.  The call shapes are identical, so only the header
 * differs; the DTYPE_* test macros below hide the value differences and fall
 * back to stat() on filesystems that do not report a type.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>

#ifdef SM_DEVICE
#include "dirent.h"            /* multicore tree */
#else
#include <dirent.h>
#endif

#ifndef SM_MAXPATH
#define SM_MAXPATH 256
#endif

#ifndef SM_DIRENT_IS_FILE
#ifdef SM_DEVICE
#define SM_DIRENT_IS_FILE(e)   DIRENT_ISFILE((e)->d_type)
#define SM_DIRENT_IS_DIR(e)    DIRENT_ISDIRECTORY((e)->d_type)
#else
#define SM_DIRENT_IS_FILE(e)   (((e)->d_type == DT_REG) || ((e)->d_type == 0))
#define SM_DIRENT_IS_DIR(e)    (((e)->d_type == DT_DIR) || ((e)->d_type == 0))
#endif
#endif

/*
 * Some SD cards and network mounts report DT_UNKNOWN for every entry.  Fall
 * back to a stat() in that case so the scanner still works.
 */
static inline int sm_entry_is_dir(const char *parent, const char *name, int hint)
{
    if (hint == DT_DIR) return 1;
    if (hint == DT_REG) return 0;

    char path[SM_MAXPATH * 2];
    int n = 0;
    const char *a = parent, *b = name;
    while (*a && n < (int)sizeof(path) - 2) path[n++] = *a++;
    if (n && path[n - 1] != '/') path[n++] = '/';
    while (*b && n < (int)sizeof(path) - 1) path[n++] = *b++;
    path[n] = '\0';

    DIR *d = opendir(path);
    if (d) {
        closedir(d);
        return 1;
    }
    return 0;
}

static inline int sm_entry_is_file(const char *parent, const char *name, int hint)
{
    if (hint == DT_DIR) return 0;
    if (hint == DT_REG) return 1;

    char path[SM_MAXPATH * 2];
    int n = 0;
    const char *a = parent, *b = name;
    while (*a && n < (int)sizeof(path) - 2) path[n++] = *a++;
    if (n && path[n - 1] != '/') path[n++] = '/';
    while (*b && n < (int)sizeof(path) - 1) path[n++] = *b++;
    path[n] = '\0';

    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

#endif /* SM_DIRENT_H */