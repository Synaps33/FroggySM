#include "sm_library.h"
#include "sm_song.h"
#include "sm_dirent.h"
#include "sm_diag.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ---------------------------------------------------------------------------
 * Song library -- recursive Songs/<group>/<song>/ scan
 *
 * PSPMania discovers songs the same way (SongManager.cpp), so a card holding
 * the stock layout works unchanged: any .sm one or two folders down, with its
 * #MUSIC reference resolved relative to the chart.
 * ------------------------------------------------------------------------- */

sm_lib_song_t  g_library[SM_MAX_SONGS];
int            g_library_count;
sm_lib_group_t g_groups[SM_MAX_GROUPS];
int            g_group_count;

static int s_log_count;

/* Default search roots, in priority order.  The GB300 mounts the SD card at
 * /mnt/sda1 (see include/file.h in the multicore tree).  The PSPMania paths
 * are kept as fallbacks so a card laid out for the old name still works. */
static const char *const s_default_roots[] = {
    "/mnt/sda1/ROMS/froggysm/Songs",
    "/mnt/sda1/ROMS/FroggySM/Songs",
    "/mnt/sda1/ROMS/PSPMania/Songs",
    "/mnt/sda1/Songs",
    NULL
};

void sm_library_title_from_dir(const char *dir, char *out, size_t out_size)
{
    /* take the last path component */
    const char *base = dir;
    for (const char *p = dir; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;

    /*
     * NotesLoader.cpp:13-28 splits on the first of " -", " ~", " (" or " ["
     * and keeps the leading part.  Scan forward only: an earlier version
     * rewound the cursor to restart the search and never terminated when the
     * very first character was one of the markers.
     */
    const char *cut = base;
    for (const char *p = base; *p; p++) {
        if (*p == '-' || *p == '~' || *p == '(' || *p == '[') {
            cut = p;
            break;
        }
    }
    while (cut > base && cut[-1] == ' ')
        cut--;

    size_t n = (size_t)(cut - base);
    if (n == 0)
        n = strlen(base);
    if (n >= out_size)
        n = out_size - 1;
    memcpy(out, base, n);
    out[n] = '\0';
}

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static void copy_str_b(char *dst, size_t n, const char *src)
{
    size_t i = 0;
    if (n == 0) return;
    for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

/*
 * Song.cpp:1175-1228 -- GetSongAssetPath
 *
 * A #MUSIC with no '/' in it is relative to the song folder.  A leading
 * "../" is resolved against the song folder and then collapsed; if it still
 * escapes, the original gives up and the song is treated as having no music.
 */
void sm_library_resolve_music(sm_lib_song_t *lib)
{
    lib->music_path[0] = '\0';
    lib->song.has_music = 0;

    const char *want = lib->song.music_file;
    if (!want || want[0] == 0)
        return;

    char candidate[SM_MAXPATH * 2];

    if (want[0] == '/' || strchr(want, ':')) {
        /* absolute */
        snprintf(candidate, sizeof(candidate), "%s", want);
    } else if (strncmp(want, "../", 3) == 0) {
        /* resolve one level up from the song folder, then collapse */
        const char *dir = lib->dir;
        const char *slash = strrchr(dir, '/');
        if (!slash) return;
        char parent[SM_MAXPATH];
        size_t n = (size_t)(slash - dir);
        if (n >= sizeof(parent)) n = sizeof(parent) - 1;
        memcpy(parent, dir, n);
        parent[n] = 0;
        snprintf(candidate, sizeof(candidate), "%s/%s", parent, want + 3);
    } else {
        snprintf(candidate, sizeof(candidate), "%s/%s", lib->dir, want);
    }

    if (file_exists(candidate)) {
        copy_str_b(lib->music_path, sizeof(lib->music_path), candidate);
        lib->song.has_music = 1;
    }
}

/*
 * Song.cpp:434-453 -- when #MUSIC is missing or points at nothing, fall back
 * to the first audio file in the folder (.mp3, .ogg, .wav in that order).
 */
static void resolve_music_fallback(sm_lib_song_t *lib)
{
    static const char *const exts[] = {
        ".mp3", ".ogg", ".wav", NULL
    };
    DIR *d = opendir(lib->dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && !lib->song.has_music) {
        if (!sm_entry_is_file(lib->dir, ent->d_name, ent->d_type)) continue;
        size_t nl = strlen(ent->d_name);
        for (int e = 0; exts[e]; e++) {
            size_t el = strlen(exts[e]);
            if (nl >= el && strcasecmp(ent->d_name + nl - el, exts[e]) == 0) {
                char temp[SM_MAXPATH * 2];
                snprintf(temp, sizeof(temp), "%s/%s", lib->dir, ent->d_name);
                copy_str_b(lib->music_path, sizeof(lib->music_path), temp);
                lib->song.has_music = 1;
                break;
            }
        }
    }
    closedir(d);
}

static void sm_library_resolve_graphics(sm_lib_song_t *lib)
{
    lib->bg_path[0] = '\0';
    lib->banner_path[0] = '\0';

    char candidate[SM_MAXPATH * 2];

    /* 1. Explicit background from #BACKGROUND */
    if (lib->song.bg_file[0]) {
        snprintf(candidate, sizeof(candidate), "%s/%s", lib->dir, lib->song.bg_file);
        if (file_exists(candidate))
            copy_str_b(lib->bg_path, sizeof(lib->bg_path), candidate);
    }

    /* 2. Explicit banner from #BANNER */
    if (lib->song.banner_file[0]) {
        snprintf(candidate, sizeof(candidate), "%s/%s", lib->dir, lib->song.banner_file);
        if (file_exists(candidate))
            copy_str_b(lib->banner_path, sizeof(lib->banner_path), candidate);
    }

    /* 3. If either missing, check directory for bg / banner files */
    if (!lib->bg_path[0] || !lib->banner_path[0]) {
        DIR *d = opendir(lib->dir);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                if (!sm_entry_is_file(lib->dir, ent->d_name, ent->d_type)) continue;
                size_t nl = strlen(ent->d_name);
                if (nl < 4) continue;

                int is_img = (strcasecmp(ent->d_name + nl - 4, ".jpg") == 0 ||
                              strcasecmp(ent->d_name + nl - 4, ".png") == 0 ||
                              (nl >= 5 && strcasecmp(ent->d_name + nl - 5, ".jpeg") == 0));
                if (!is_img) continue;

                char lower[128];
                size_t n = nl < 127 ? nl : 127;
                for (size_t i = 0; i < n; i++) {
                    char c = ent->d_name[i];
                    lower[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
                }
                lower[n] = 0;

                if (!lib->bg_path[0] && (strstr(lower, "bg") || strstr(lower, "background"))) {
                    snprintf(candidate, sizeof(candidate), "%s/%s", lib->dir, ent->d_name);
                    copy_str_b(lib->bg_path, sizeof(lib->bg_path), candidate);
                } else if (!lib->banner_path[0] && (strstr(lower, "banner") || strstr(lower, "bn"))) {
                    snprintf(candidate, sizeof(candidate), "%s/%s", lib->dir, ent->d_name);
                    copy_str_b(lib->banner_path, sizeof(lib->banner_path), candidate);
                }
            }
            closedir(d);
        }
    }
}

static int has_ext(const char *name, const char *ext)
{
    size_t nl = strlen(name), el = strlen(ext);
    if (nl < el) return 0;
    const char *p = name + nl - el;
    for (size_t i = 0; i < el; i++)
        if (toupper((unsigned char)p[i]) != toupper((unsigned char)ext[i]))
            return 0;
    return 1;
}

static void try_chart(const char *path, const char *dir)
{
    if (g_library_count >= SM_MAX_SONGS)
        return;

    sm_song_t song;
    if (!sm_song_load_file(path, &song))
        return;

    if (song.num_diffs == 0) {
        sm_song_free(&song);
        return;
    }

    /* NotesLoader.cpp:13-28 / Song.cpp:506-508 -- a chart with no #TITLE
     * borrows the folder name, up to the first " -", " ~", " (" or " [". */
    if (song.title[0] == 0 || strcmp(song.title, "Untitled") == 0) {
        char from_dir[64];
        sm_library_title_from_dir(dir, from_dir, sizeof(from_dir));
        if (from_dir[0])
            copy_str_b(song.title, sizeof(song.title), from_dir);
    }

    /* Extract group name (parent directory of song dir) */
    if (song.group[0] == 0) {
        const char *last_slash = NULL;
        const char *prev_slash = NULL;
        for (const char *p = dir; *p; p++) {
            if (*p == '/' || *p == '\\') {
                prev_slash = last_slash;
                last_slash = p;
            }
        }
        if (prev_slash && last_slash && last_slash > prev_slash + 1) {
            size_t glen = (size_t)(last_slash - prev_slash - 1);
            if (glen >= sizeof(song.group)) glen = sizeof(song.group) - 1;
            memcpy(song.group, prev_slash + 1, glen);
            song.group[glen] = 0;
        } else {
            copy_str_b(song.group, sizeof(song.group), "Songs");
        }
    }

    sm_lib_song_t *lib = &g_library[g_library_count];
    memset(lib, 0, sizeof(*lib));
    lib->song = song;
    copy_str_b(lib->dir, sizeof(lib->dir), dir);

    sm_library_resolve_music(lib);
    if (!lib->song.has_music)
        resolve_music_fallback(lib);
    sm_library_resolve_graphics(lib);

    g_library_count++;
}

static void scan_dir(const char *dir, int depth);

/*
 * Insertion sort so the song wheel order is stable across runs and cards.
 *
 * Deliberately not qsort(): the list is capped at SM_MAX_SONGS, so an
 * insertion sort is both faster here and one less libc dependency for a core
 * that links against a bare -lc.
 */
static void sort_paths(char paths[][SM_MAXPATH], int n)
{
    for (int i = 1; i < n; i++) {
        char key[SM_MAXPATH];
        memcpy(key, paths[i], SM_MAXPATH);
        int j = i - 1;
        while (j >= 0 && strcmp(paths[j], key) > 0) {
            memcpy(paths[j + 1], paths[j], SM_MAXPATH);
            j--;
        }
        memcpy(paths[j + 1], key, SM_MAXPATH);
    }
}

/*
 * Load every .sm in `dir`, then recurse into its subdirectories.
 *
 * Doing exactly one level per call matters: an earlier version both loaded a
 * subdirectory's charts on the way down *and* scanned that same folder again
 * when the recursion landed in it, so every song appeared twice in the wheel.
 */
static void scan_dir(const char *dir, int depth)
{
    if (depth > SM_MAX_DEPTH || g_library_count >= SM_MAX_SONGS)
        return;

    DIR *d = opendir(dir);
    if (!d)
        return;

    /* --- charts directly in this folder --- */
    struct dirent *e;
    while ((e = readdir(d)) != NULL && g_library_count < SM_MAX_SONGS) {
        if (e->d_name[0] == '.' && (e->d_name[1] == '.' || e->d_name[1] == '\0'))
            continue;
        if (!has_ext(e->d_name, ".sm"))
            continue;
        if (!sm_entry_is_file(dir, e->d_name, e->d_type))
            continue;

        char chart[SM_MAXPATH];
        snprintf(chart, sizeof(chart), "%s/%s", dir, e->d_name);
        try_chart(chart, dir);
    }
    closedir(d);

    /* --- collect subdirectories, then descend in sorted order --- */
    char subdirs[SM_MAX_SONGS][SM_MAXPATH];
    int  num_sub = 0;

    d = opendir(dir);
    if (!d) return;
    while ((e = readdir(d)) != NULL && num_sub < SM_MAX_SONGS) {
        if (e->d_name[0] == '.')
            continue;
        if (!sm_entry_is_dir(dir, e->d_name, e->d_type))
            continue;
        snprintf(subdirs[num_sub], SM_MAXPATH, "%s/%s", dir, e->d_name);
        num_sub++;
    }
    closedir(d);

    sort_paths(subdirs, num_sub);

    for (int i = 0; i < num_sub && g_library_count < SM_MAX_SONGS; i++)
        scan_dir(subdirs[i], depth + 1);
}

static void rebuild_groups(void)
{
    g_group_count = 0;
    memset(g_groups, 0, sizeof(g_groups));

    for (int i = 0; i < g_library_count; i++) {
        const char *grp_name = g_library[i].song.group;
        if (!grp_name || !grp_name[0]) grp_name = "Other";

        int g_idx = -1;
        for (int g = 0; g < g_group_count; g++) {
            if (strcmp(g_groups[g].name, grp_name) == 0) {
                g_idx = g;
                break;
            }
        }

        if (g_idx < 0 && g_group_count < SM_MAX_GROUPS) {
            g_idx = g_group_count++;
            copy_str_b(g_groups[g_idx].name, sizeof(g_groups[g_idx].name), grp_name);
        }

        if (g_idx >= 0 && g_groups[g_idx].num_songs < SM_MAX_SONGS) {
            g_groups[g_idx].song_indices[g_groups[g_idx].num_songs++] = i;
        }
    }
}

int sm_library_scan(const char *root)
{
    g_library_count = 0;
    g_group_count = 0;
    s_log_count = 0;

    const char *r = root;
    if (r) {
        sm_diag("scan: root %s", r);
        scan_dir(r, 0);
    } else {
        for (int i = 0; s_default_roots[i]; i++) {
            DIR *d = opendir(s_default_roots[i]);
            sm_diag("scan: try %s %s", s_default_roots[i], d ? "ok" : "missing");
            if (!d) continue;
            closedir(d);
            scan_dir(s_default_roots[i], 0);
            sm_diag("scan: after %s songs=%d", s_default_roots[i], g_library_count);
            if (g_library_count > 0) break;
        }
    }
    rebuild_groups();
    sm_diag("scan: done songs=%d groups=%d", g_library_count, g_group_count);
    return g_library_count;
}

sm_lib_song_t *sm_library_get(int idx)
{
    if (idx < 0 || idx >= g_library_count) return NULL;
    return &g_library[idx];
}

void sm_library_free(void)
{
    for (int i = 0; i < g_library_count; i++)
        sm_song_free(&g_library[i].song);
    g_library_count = 0;
}