#ifndef SM_LIBRARY_H
#define SM_LIBRARY_H

#include "sm_types.h"

#define SM_MAXPATH      256
#define SM_MAX_SONGS    64
#define SM_MAX_GROUPS   16
#define SM_MAX_DEPTH    4

/*
 * One entry of the song wheel.
 *
 * PSPMania scans Songs/<group>/<song>/ at boot (SongManager.cpp).  The same
 * layout is used here, read from the SD card so the port can carry the whole
 * demo library rather than a fixed set baked into the ROM.
 */
typedef struct {
    sm_song_t song;
    char      dir[SM_MAXPATH];
    char      music_path[SM_MAXPATH];
    char      bg_path[SM_MAXPATH];
    char      banner_path[SM_MAXPATH];
} sm_lib_song_t;

typedef struct {
    char name[48];
    int  song_indices[SM_MAX_SONGS];
    int  num_songs;
} sm_lib_group_t;

extern sm_lib_song_t  g_library[SM_MAX_SONGS];
extern int            g_library_count;
extern sm_lib_group_t g_groups[SM_MAX_GROUPS];
extern int            g_group_count;

/* Walk `root` recursively, loading every .sm chart found.  Returns the
 * number of songs loaded. */
int  sm_library_scan(const char *root);

sm_lib_song_t *sm_library_get(int idx);
void sm_library_free(void);

/* Resolve #MUSIC against the song directory (Song.cpp:1175-1228 GetSongAssetPath) */
void sm_library_resolve_music(sm_lib_song_t *lib);

/* NotesLoader.cpp:13-28 -- fall back to splitting the folder name on the
 * first " -", " ~", " (" or " [" when the chart has no #TITLE. */
void sm_library_title_from_dir(const char *dir, char *out, size_t out_size);

#endif /* SM_LIBRARY_H */