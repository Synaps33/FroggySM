#ifndef SM_GAME_H
#define SM_GAME_H

#include "sm_types.h"

/* Screens -- mirrors the ScreenManager flow of the original, trimmed to what
 * a four-lane handheld build needs. */
enum {
    SCREEN_BOOT = 0,
    SCREEN_TITLE,
    SCREEN_SELECT,
    SCREEN_PLAY,
    SCREEN_RESULT,
    SCREEN_COUNT
};

/* Judgment display names (GameConstantsAndTypes / Judgment.cpp) */
extern const char *const sm_judge_name[NUM_TAP_NOTE_SCORES];
extern const char *const sm_judge_short[NUM_TAP_NOTE_SCORES];
extern const char *const sm_difficulty_name[NUM_DIFFICULTIES];

typedef struct {
    int screen;

    /* song pointer into the loaded library */
    int      song_idx;
    int      steps_idx;

    /* timing -- GameState.cpp:472-480 */
    float    music_seconds;
    float    song_beat;
    float    cur_bps;
    int      freeze;

    /* player options -- PlayerOptions.cpp:17-37 */
    float    scroll_speed;
    int      time_spacing;
    float    scroll_bpm;
    float    wave;            /* EFFECT_WAVE  */
    float    expand;           /* EFFECT_EXPAND */
    float    boost;            /* EFFECT_BOOST  */
    float    tipsy;            /* EFFECT_TIPSY  */

    /* per-column button state */
    uint8_t  btn_down[SM_NUM_LANES];
    uint8_t  btn_prev[SM_NUM_LANES];

    /* hold tracking: index into steps->holds, or -1 */
    int8_t   active_hold[SM_NUM_LANES];

    /* stage stats -- StageStats.h */
    int      score;
    int      combo;
    int      max_combo;
    int      tns[NUM_TAP_NOTE_SCORES];
    int      hns[NUM_HOLD_NOTE_SCORES];
    int      num_taps_and_holds;     /* N */
    int      taps_hit;               /* n */
    int      max_possible_points;    /* meter * 10000000 */
    int      point_bonus;
    int      score_remainder;
    int      pro_timing_error;
    int      rows_judged;
    int      last_row_judged;

    /* last judgment feedback */
    int      last_judge;         /* TNS_* */
    int      last_judge_lane;
    int      judge_flash;        /* frames remaining */
    float    judge_error_ms;

    /* song select */
    int      sel_row;
    int      sel_diff;
    int      sel_group;          /* active group / album index (-1 for all) */
    int      sel_scroll;
    int      banner_anim;

    /* boot */
    int      boot_frames;

    /* visual & theme */
    float    life;               /* 0.0 .. 1.0 (DDR MAX2 life bar) */
    int      theme;              /* sm_theme_id_t */
    int      theme_popup_timer;  /* toast message timer */
    float    preview_timer;
    int      preview_song_idx;

    /* misc */
    uint32_t joypad;
    int      paused;
    int      start_prev;
    int      sel_prev;
    int      btn_l_prev;
    int      btn_r_prev;
    int      song_ended;
    float    result_timer;
    int      frame_count;
} sm_game_t;

extern sm_game_t g_game;

void sm_game_init(void);
void sm_game_reset(void);
void sm_game_update(void);

void sm_game_start_song(int song_idx, int steps_idx);
void sm_game_song_finished(void);

/* Lane/button state, written by the libretro front end. */
extern uint8_t sm_btn[SM_NUM_LANES];
extern uint8_t sm_btn_prev[SM_NUM_LANES];
extern int     sm_sel_pressed;
extern int     sm_start_pressed;
extern int     sm_btn_l_pressed;
extern int     sm_btn_r_pressed;
extern int     sm_btn_a_pressed;
extern int     sm_btn_b_pressed;

/* The decoded difficulty currently being played (NULL outside gameplay). */
extern sm_steps_t *sm_active_steps;

/* helpers shared with renderer / hackerlog */
float sm_arrow_y(int lane, float note_beat);
int   sm_calc_grade(void);
float sm_calc_percent(void);
int   sm_calc_score(void);
const char *sm_current_song_name(void);

#endif /* SM_GAME_H */