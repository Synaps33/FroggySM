#ifndef SM_TYPES_H
#define SM_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------------- */
/*  PSPMania (StepMania 3.9) -> GB300 / SF2000 port                          */
/*  Constants below mirror the original PSPMania sources:                      */
/*    NoteTypes.h:84-88          ROWS_PER_BEAT                                */
/*    GameConstantsAndTypes.h    TNS_ / HNS_ enums                        */
/*    PrefsManager.cpp:80-143    judgment windows + grade weights              */
/*    ScoreKeeperMAX2.cpp        score accumulation                           */
/*    ArrowEffects.cpp:13        ARROW_SPACING = ARROW_SIZE                    */
/* ------------------------------------------------------------------------- */

#define SM_SCREEN_WIDTH        320
#define SM_SCREEN_HEIGHT       240

/* StepMania note grid (PSPMania build uses 24 rows/beat, half of PC's 48) */
#define SM_BEATS_PER_MEASURE   4
#define SM_ROWS_PER_BEAT       24
#define SM_ROWS_PER_MEASURE    (SM_ROWS_PER_BEAT * SM_BEATS_PER_MEASURE)

#define SM_MAX_STEPS           8
#define SM_TRACKS              4

/* Arrows.  PSPMania used ARROW_SIZE == ARROW_SPACING == 36 on a 480x272
 * screen.  The GB300/SF2000 panel is 320x240, so we keep the 36px arrow
 * sprite (README: "Arrow size is currently 36px") and widen the gap. */
#define SM_ARROW_SIZE          36
#define SM_COL_SPACING         52

#define SM_NOTE_FIELD_TOP      22
#define SM_RECEPTOR_Y          196
#define SM_NOTE_FIELD_HEIGHT   (SM_RECEPTOR_Y - SM_NOTE_FIELD_TOP)

#define SM_AUDIO_SAMPLE_RATE   22050
#define SM_AUDIO_FRAMES        368    /* 22050 / 60 = ~368 */
#define SM_NUM_LANES           4

/* ------------------------------------------------------------------ */
/* TapNoteScore -- GameConstantsAndTypes.h:164-174 (order is semantic) */
/* ------------------------------------------------------------------ */
enum {
    TNS_NONE = 0,
    TNS_HIT_MINE,
    TNS_MISS,
    TNS_BOO,
    TNS_GOOD,
    TNS_GREAT,
    TNS_PERFECT,
    TNS_MARVELOUS,
    NUM_TAP_NOTE_SCORES
};

enum { HNS_NONE = 0, HNS_NG, HNS_OK, NUM_HOLD_NOTE_SCORES };

/* Difficulty -- NoteTypes.h */
enum {
    DIFFICULTY_BEGINNER = 0,
    DIFFICULTY_EASY,
    DIFFICULTY_MEDIUM,
    DIFFICULTY_HARD,
    DIFFICULTY_CHALLENGE,
    DIFFICULTY_EDIT,
    NUM_DIFFICULTIES
};

enum { DANCE_SINGLE = 0, DANCE_DOUBLE, NUM_STEPS_TYPES };

/* ------------------------------------------------------------------ */
/* Judgment windows -- PrefsManager.cpp:80-87                           */
/* ------------------------------------------------------------------ */
#define SM_WIN_MARVELOUS        0.0225f
#define SM_WIN_PERFECT          0.045f
#define SM_WIN_GREAT            0.090f
#define SM_WIN_GOOD             0.135f
#define SM_WIN_BOO              0.180f
#define SM_WIN_OK               0.250f
#define SM_WIN_MINE             0.090f
#define SM_WIN_ATTACK           0.135f

/* Grade weights -- PrefsManager.cpp:125-133 */
#define SM_GRADE_W_MARVELOUS    2
#define SM_GRADE_W_PERFECT      2
#define SM_GRADE_W_GREAT        1
#define SM_GRADE_W_GOOD         0
#define SM_GRADE_W_BOO         (-4)
#define SM_GRADE_W_MISS        (-8)
#define SM_GRADE_W_HIT_MINE    (-8)
#define SM_GRADE_W_OK           6
#define SM_GRADE_W_NG           0

/* Percent / dance-point weights -- PrefsManager.cpp:116-124 */
#define SM_PCT_W_MARVELOUS      3
#define SM_PCT_W_PERFECT        2
#define SM_PCT_W_GREAT          1
#define SM_PCT_W_GOOD           0
#define SM_PCT_W_BOO            0
#define SM_PCT_W_MISS           0
#define SM_PCT_W_OK             3
#define SM_PCT_W_NG             0
#define SM_PCT_W_HIT_MINE     (-2)

/* Grade thresholds -- PrefsManager.cpp:135-143 */
#define SM_GRADE_T1             1.00f
#define SM_GRADE_T2             1.00f
#define SM_GRADE_T3             0.93f
#define SM_GRADE_T4             0.80f
#define SM_GRADE_T5             0.65f
#define SM_GRADE_T6             0.45f
#define SM_GRADE_T7             (-99999.0f)

/* --------------------------------------------------------------- */
/* Per-note / per-hold storage                                       */
/* --------------------------------------------------------------- */
enum {
    NT_EMPTY = 0,
    NT_TAP,
    NT_HOLD_HEAD,
    NT_HOLD_TAIL,   /* '3' in the note grid; consumed by convert_holds() */
    NT_MINE
};

typedef struct {
    int16_t offset_ms;  /* pro-timing, negated as in Player.cpp:855 */
    uint8_t type;       /* NT_*            */
    uint8_t score;      /* TNS_*           */
    int16_t hold_index; /* index into steps->holds, or -1 */
} sm_note_t;

typedef struct {
    int16_t start_row;
    int16_t end_row;
    uint8_t score;      /* HNS_*           */
} sm_hold_t;

/* --------------------------------------------------------------- */
/* TimingData -- TimingData.h:6-20                                   */
/* --------------------------------------------------------------- */
typedef struct {
    float start_beat;
    float bpm;
} sm_bpm_seg_t;

typedef struct {
    float start_beat;
    float seconds;
} sm_stop_seg_t;

typedef struct {
    sm_bpm_seg_t  bpm[32];
    int           num_bpm;
    sm_stop_seg_t stop[32];
    int           num_stop;
    float         beat0_offset;   /* #OFFSET */
} sm_timing_t;

float sm_bpm_at_beat(const sm_timing_t *t, float beat);
/* TimingData.cpp:158-195 */
float sm_time_from_beat(const sm_timing_t *t, float beat);
/* TimingData.cpp:97-155 */
float sm_beat_from_time(const sm_timing_t *t, float seconds, float *out_bps);

/* --------------------------------------------------------------- */
/* Steps -- a single difficulty of a chart                          */
/* --------------------------------------------------------------- */
typedef struct {
    char     description[24];
    char     author[24];
    char     difficulty_name[16];
    int      difficulty;
    int      meter;
    int      num_tracks;
    int      num_rows;          /* allocated note rows */
    int      num_holds;
    sm_note_t     *notes;       /* num_rows * num_tracks */
    sm_hold_t     *holds;
    float     radar[10];
} sm_steps_t;

/* --------------------------------------------------------------- */
/* Song -- Song.h                                                    */
/* --------------------------------------------------------------- */
#define SM_MAX_STEPS_PER_SONG 8

/*
 * A difficulty *header*.  PSPMania caches every difficulty of every song at
 * once, which on a 32MB handheld means megabytes of note grids nobody is
 * looking at.  The header is all the song wheel and the rating display need,
 * so the grid is decoded only when the song is actually started.
 *
 * note_off/note_len locate the payload inside sm_song_t::text.
 */
typedef struct {
    char      description[24];
    char      author[24];
    char      difficulty_name[16];
    int       difficulty;
    int       meter;
    float     radar[10];
    uint32_t  note_off;
    uint32_t  note_len;
    int       num_steps;
} sm_diff_t;

typedef struct {
    char      title[64];
    char      subtitle[64];
    char      artist[64];
    char      credit[64];
    char      group[48];
    char      music_file[80];   /* #MUSIC, resolved against the song folder */
    char      bg_file[80];      /* #BACKGROUND */
    char      banner_file[80];  /* #BANNER */

    sm_timing_t timing;
    float    music_length;      /* seconds */
    int      has_music;

    /* owned, mutable copy of the chart text (comment-stripped on load) */
    char  *text;
    size_t text_len;

    sm_diff_t diffs[SM_MAX_STEPS_PER_SONG];
    int       num_diffs;
} sm_song_t;

/* Themes */
typedef enum {
    THEME_PSPMANIA = 0,    /* StepMania 3.9 / PSPMania Metallic Blue */
    THEME_CYBER_MAX2,      /* DDR MAX2 Cyber Neon Green */
    THEME_ARCADE_DARK,     /* StepMania 5 Dark / Cyberpunk Amber */
    THEME_COUNT
} sm_theme_id_t;

#endif /* SM_TYPES_H */