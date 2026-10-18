#ifndef SM_AUDIO_H
#define SM_AUDIO_H

#include "sm_types.h"

/*
 * Music playback for a rhythm game.
 *
 * The engine is driven from the decoded-sample counter rather than a wall
 * clock, which is what ScreenGameplay::UpdateSongPosition does with the sound
 * engine's sample position (ScreenGameplay.cpp:1173-1182).  Judgments are
 * therefore reproducible and free of timer jitter.
 *
 * MP3 frames are pulled off the SD card and decoded by the CC0 minimp3 core.
 * PSPMania offloaded this to the PSP's MediaEngine hardware, so there was no
 * software decoder to inherit; minimp3 is a single-header public-domain
 * decoder that needs nothing but stdint/stdlib/string.
 *
 * The decoder state lives in sm_audio.c: minimp3 is header-only with external
 * linkage, so it must be included from exactly one translation unit.
 */

#define SM_HITSOUND_SLOTS   8
#define SM_MENU_MAX_SAMPLES 65536

/* Telemetry for the HackerLog overlay. */
typedef struct {
    int      music_playing;
    int      music_hz;
    int      music_channels;
    int      music_eof;
    double   music_seconds;
    uint64_t bytes_read;
    int      decode_load;          /* permille of the per-tick decode budget */
    int      frames_decoded;       /* MP3 frames pulled this tick */
    int      sfx_playing;
} sm_audio_stats_t;

void sm_audio_init(void);
void sm_audio_shutdown(void);

/* Opens `path` and starts decoding from the beginning.  Returns 1 on success. */
int  sm_audio_open_music(const char *path);
void sm_audio_stop_music(void);
void sm_audio_pause_music(int pause);
int  sm_audio_is_paused(void);

/* Music position in seconds, derived from decoded samples. */
float sm_audio_music_seconds(void);
int   sm_audio_music_playing(void);
void  sm_audio_get_format(int *out_rate, int *out_channels);
void  sm_audio_set_target_rate(float rate);

void sm_audio_get_stats(sm_audio_stats_t *out);
void sm_audio_set_volumes(int music_pct, int sfx_pct);

/* Renders exactly `frames` stereo sample pairs into `out`. */
void sm_audio_render(int16_t *out, int frames);

int  sm_audio_load_hit(const char *path);
int  sm_audio_load_menu(const char *path);
void sm_audio_play_hit(int variant);
void sm_audio_play_menu(void);

int  sm_audio_file_exists(const char *path);

#endif /* SM_AUDIO_H */