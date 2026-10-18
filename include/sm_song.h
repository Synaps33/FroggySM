#ifndef SM_SONG_H
#define SM_SONG_H

#include "sm_types.h"

#define SM_MAX_PARAM_LEN   16384
/* Note-grid ceiling.  A 4:48 marathon at 200 BPM runs 1600 beats = 38400
 * rows; 40000 covers the demo library (the longest chart, Fury of the Storm,
 * needs 32664) and costs 40000*4*6 = 960KB for one difficulty -- and only
 * one difficulty is ever resident. */
#define SM_MAX_ROWS        40000

/* Ownership: sm_song_parse takes the buffer and frees it in sm_song_free. */
int  sm_song_parse(char *text, size_t len, sm_song_t *song);
void sm_song_free(sm_song_t *song);

/* Decode one difficulty into a note grid.  Returns 0 on failure. */
int  sm_song_load_steps(const sm_song_t *song, int diff_idx, sm_steps_t *st);
void sm_steps_free(sm_steps_t *st);

/* Convenience wrapper: loads song + difficulty straight from a file. */
int  sm_song_load_file(const char *path, sm_song_t *song);

/* Note grid helpers (NoteTypes.cpp / NoteType.h) */
int   sm_beat_to_row(float beat);
float sm_row_to_beat(int row);

/* Small math helpers; the device core links -lc only, so no libm. */
float sm_sinf(float x);
float sm_cosf(float x);
float sm_scale(float value, float in_min, float in_max, float out_min, float out_max);
float sm_clampf(float v, float lo, float hi);

#endif /* SM_SONG_H */