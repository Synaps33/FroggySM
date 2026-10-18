#include "sm_game.h"
#include "sm_song.h"
#include "sm_library.h"
#include "sm_audio.h"
#include "sm_bg.h"
#include "sm_theme.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------------
 * Gameplay engine
 *
 * Ported from mafu9/PSPMania (StepMania 3.9):
 *   Player.cpp:636-899            Step() -- note search + judgment windows
 *   Player.cpp:984-1046           UpdateTapNotesMissedOlderThan
 *   Player.cpp:1135-1245          HandleTapRowScore -- combo + score per row
 *   Player.cpp:322-430            hold scoring
 *   ScoreKeeperMAX2.cpp:94-304    score accumulation
 *   StageStats.cpp:144-229        grades
 *   ArrowEffects.cpp:22-136       scroll position
 *
 * Lane order is dance-single's: Left, Down, Up, Right
 * (GameManager.cpp:1232-1235).
 * ------------------------------------------------------------------------- */

sm_game_t g_game;

sm_steps_t *sm_active_steps = NULL;

uint8_t sm_btn[SM_NUM_LANES];
uint8_t sm_btn_prev[SM_NUM_LANES];
int     sm_sel_pressed;
int     sm_start_pressed;
int     sm_btn_l_pressed;
int     sm_btn_r_pressed;
int     sm_btn_a_pressed;
int     sm_btn_b_pressed;
static int s_btn_a_prev;
static int s_btn_b_prev;

const char *const sm_judge_name[NUM_TAP_NOTE_SCORES] = {
    "None", "Mine", "Miss", "Boo", "Good", "Great", "Perfect", "Marvelous"
};
const char *const sm_judge_short[NUM_TAP_NOTE_SCORES] = {
    "-", "MINE", "MISS", "BOO", "GOOD", "GREAT", "PERFECT", "MARVELOUS"
};
const char *const sm_difficulty_name[NUM_DIFFICULTIES] = {
    "Beginner", "Easy", "Medium", "Hard", "Challenge", "Edit"
};

/* ----------------------------- helpers ------------------------------ */

static float fabsf_(float v) { return v < 0.0f ? -v : v; }
static int   absi_(int v)    { return v < 0 ? -v : v; }

static const sm_song_t *cur_song(void)
{
    sm_lib_song_t *lib = sm_library_get(g_game.song_idx);
    return lib ? &lib->song : NULL;
}

/*
 * ArrowEffects.cpp:22-101 -- screen offset of a note.
 *
 *   beat spacing:  (noteBeat - songBeat) * ARROW_SPACING * scrollSpeed
 *   time spacing:  (noteSeconds - musicSeconds) * (scrollBPM/60) * ARROW_SPACING
 *
 * ARROW_SPACING is ARROW_SIZE (36) on the PSP build (ArrowEffects.cpp:13).
 * Notes above the judgment line get a negative offset; the renderer turns that
 * into a screen Y.
 */
float sm_arrow_y(int lane, float note_beat)
{
    float offset;

    if (g_game.time_spacing) {
        const sm_song_t *s = cur_song();
        float note_sec = s ? sm_time_from_beat(&s->timing, note_beat) : 0.0f;
        float bps      = g_game.scroll_bpm / 60.0f;
        offset = (note_sec - g_game.music_seconds) * bps * (float)SM_ARROW_SIZE;
    } else {
        offset = (note_beat - g_game.song_beat) * (float)SM_ARROW_SIZE;
    }

    /* accelerations (PlayerOptions.cpp:17-37; all default to 0 on a fresh
     * install, but the PlayerOptions screen can turn them on) */
    if (g_game.boost != 0.0f && offset > 0.0f) {
        float h = (float)SM_SCREEN_HEIGHT;
        float y = offset * 1.5f / ((offset + h / 1.2f) / h);
        offset += g_game.boost * (y - offset);
    }
    if (g_game.wave != 0.0f) {
        offset += g_game.wave * 20.0f * sm_sinf(offset / 38.0f);
    }

    offset *= g_game.scroll_speed;

    /* Arrows past the judgment line sink below the receptors, as in
     * ArrowEffects.cpp:48-49 which returns early for a negative offset. */
    (void)lane;
    return SM_RECEPTOR_Y - offset;
}

/* --------------------------- score (MAX2) --------------------------- */

/* ScoreKeeperMAX2.cpp:243-251 -- per-judgment multiplier */
static int score_multiplier(int tns)
{
    switch (tns) {
        case TNS_MARVELOUS: return 10;
        case TNS_PERFECT:   return 9;     /* ShowMarvelous() is on by default */
        case TNS_GREAT:     return 5;
        default:            return 0;
    }
}

/* ScoreKeeperMAX2.cpp:172-196 -- GetScore(p, B, S, n) = p * n * B / S */
static int sc_get_score(int p, int B, long long S, int n)
{
    if (S <= 0) return 0;
    return (int)((long long)p * (long long)n * (long long)B / S);
}

/* ScoreKeeperMAX2.cpp:94-170 -- N and the maximum possible score */
static void setup_scoring(void)
{
    g_game.max_possible_points = 0;
    g_game.num_taps_and_holds = 0;

    sm_steps_t *st = sm_active_steps;
    if (!st) return;

    int meter = st->meter;
    if (meter < 1)  meter = 1;
    if (meter > 10) meter = 10;

    g_game.max_possible_points = meter * 10000000;

    /* m_iNumTapsAndHolds = rows with a tap or hold head + the holds
     * themselves, so each hold counts twice (ScoreKeeperMAX2.cpp:163). */
    int rows_with = 0;
    for (int r = 0; r < st->num_rows; r++)
        for (int c = 0; c < st->num_tracks; c++) {
            uint8_t t = st->notes[r * st->num_tracks + c].type;
            if (t == NT_TAP || t == NT_HOLD_HEAD || t == NT_MINE) { rows_with++; break; }
        }

    g_game.num_taps_and_holds = rows_with + st->num_holds;
}

/* ScoreKeeperMAX2.cpp:198-304 -- AddScore */
static void add_row_score(int tns)
{
    int p = score_multiplier(tns);
    g_game.taps_hit++;

    int  N = g_game.num_taps_and_holds;
    long long S = (long long)N * (N + 1) / 2;
    int  B = g_game.max_possible_points / 10;

    g_game.score += sc_get_score(p, B, S, g_game.taps_hit);
    g_game.point_bonus -= sc_get_score(10, B, S, g_game.taps_hit);

    /* Every note hit at Perfect or better recovers the deficit accumulated
     * from earlier Great/Good/Boo/Miss. */
    if (N > 0 && g_game.taps_hit == N && tns >= TNS_PERFECT) {
        g_game.score += g_game.point_bonus;
        g_game.point_bonus = 0;
    }

    g_game.tns[tns]++;
}

/* Player.cpp:1135-1245 -- HandleTapRowScore: one score per completed row */
static void finalize_row(int best_tns, int taps_in_row)
{
    /* ComboIsPerRow is false for MAX2 outside Oni mode, so a row is worth
     * every tap it contained (ScoreKeeperMAX2.cpp:316-396). */
    if (best_tns >= TNS_GREAT) {
        g_game.combo += (taps_in_row > 0) ? taps_in_row : 1;
        if (g_game.combo > g_game.max_combo)
            g_game.max_combo = g_game.combo;
    } else {
        g_game.combo = 0;
    }

    add_row_score(best_tns);
    g_game.rows_judged++;
}

/*
 * NoteDataWithScoring.cpp:209-234 -- LastTapNoteScore.
 * Mines and empties are skipped; a miss short-circuits; otherwise the track
 * whose offset was smallest (stored negated, so largest) wins.
 */
static int last_tap_score_row(sm_steps_t *st, int row, int *out_taps)
{
    int tracks = st->num_tracks;
    int taps = 0;
    int best_score = -1;
    int16_t best_off = -32768;

    for (int c = 0; c < tracks; c++) {
        sm_note_t *n = &st->notes[row * tracks + c];
        if (n->type == NT_EMPTY) continue;
        if (n->type == NT_MINE) continue;          /* mines are not taps */
        if (n->type == NT_HOLD_TAIL) continue;     /* marker, never scorable */

        taps++;
        if (n->score == TNS_NONE) continue;        /* still pending */

        if (n->score == TNS_MISS) return TNS_MISS; /* short-circuit */
        if (best_score < 0 || n->offset_ms > best_off) {
            best_score = n->score;
            best_off   = n->offset_ms;
        }
    }

    *out_taps = taps;
    return best_score < 0 ? TNS_NONE : best_score;
}

/* IsRowCompletelyJudged -- NoteDataWithScoring.cpp:201-204 */
static int row_fully_judged(sm_steps_t *st, int row)
{
    int tracks = st->num_tracks;
    for (int c = 0; c < tracks; c++) {
        sm_note_t *n = &st->notes[row * tracks + c];
        if (n->type == NT_EMPTY || n->type == NT_HOLD_TAIL) continue;
        if (n->score == TNS_NONE) return 0;
    }
    return 1;
}

/* ---------------------------- judgment ------------------------------ */

static void judge_note(sm_note_t *n, int lane, float offset_sec)
{
    /* Player.cpp:730-737 -- window classification */
    float a = fabsf_(offset_sec);
    int tns;
    if      (a <= SM_WIN_MARVELOUS) tns = TNS_MARVELOUS;
    else if (a <= SM_WIN_PERFECT)   tns = TNS_PERFECT;
    else if (a <= SM_WIN_GREAT)     tns = TNS_GREAT;
    else if (a <= SM_WIN_GOOD)      tns = TNS_GOOD;
    else if (a <= SM_WIN_BOO)       tns = TNS_BOO;
    else                            tns = TNS_MISS;

    n->score = (uint8_t)tns;
    /* Player.cpp:855 stores the offset negated */
    int ms = (int)(offset_sec * 1000.0f);
    n->offset_ms = (int16_t)(-ms);

    g_game.last_judge = tns;
    g_game.last_judge_lane = lane;
    g_game.judge_flash = 18;
    g_game.judge_error_ms = offset_sec * 1000.0f;

    /* pro timing (Player.cpp:837-843) */
    int err = absi_(ms);
    g_game.pro_timing_error += (tns == TNS_MARVELOUS || tns == TNS_PERFECT) ? err : -err;

    /* DDR MAX2 life delta */
    switch (tns) {
        case TNS_MARVELOUS: g_game.life += 0.02f; break;
        case TNS_PERFECT:   g_game.life += 0.015f; break;
        case TNS_GREAT:     g_game.life += 0.008f; break;
        case TNS_GOOD:      break;
        case TNS_BOO:       g_game.life -= 0.04f; break;
        case TNS_MISS:      g_game.life -= 0.08f; break;
    }
    if (g_game.life > 1.0f) g_game.life = 1.0f;
    if (g_game.life < 0.0f) g_game.life = 0.0f;
}

/*
 * Player.cpp:636-899 -- Step().  Find the closest unjudged note in `lane`
 * and judge it.  Returns 1 if something was hit.
 */
static int hit_lane(int lane)
{
    sm_steps_t *st = sm_active_steps;
    if (!st) return 0;

    const sm_song_t *song = cur_song();
    if (!song) return 0;

    int tracks = st->num_tracks;
    float now  = g_game.music_seconds;

    /* Mines are checked first and win outright (Player.cpp:694-729): the
     * judge window is SM_WIN_MINE. */
    {
        int row_now = sm_beat_to_row(g_game.song_beat);
        for (int dr = -2; dr <= 2; dr++) {
            int r = row_now + dr;
            if (r < 0 || r >= st->num_rows) continue;
            sm_note_t *m = &st->notes[r * tracks + lane];
            if (m->type != NT_MINE || m->score != TNS_NONE) continue;

            float t = sm_time_from_beat(&song->timing, sm_row_to_beat(r));
            if (fabsf_(t - now) <= SM_WIN_MINE) {
                m->score = TNS_HIT_MINE;
                m->offset_ms = (int16_t)(-(int)((t - now) * 1000.0f));
                g_game.last_judge = TNS_HIT_MINE;
                g_game.last_judge_lane = lane;
                g_game.judge_flash = 18;
                g_game.combo = 0;
                g_game.life -= 0.16f;
                if (g_game.life < 0.0f) g_game.life = 0.0f;
                sm_audio_play_hit(lane);
                return 1;
            }
        }
    }

    /* Closest note in this lane.  StepMania searches a beat either way
     * (Player.cpp:594-633); a doubled tap cannot share a row in one lane. */
    int centre = sm_beat_to_row(g_game.song_beat);
    int best_row = -1;
    float best_abs = 0.0f;

    for (int dr = 0; dr <= SM_ROWS_PER_BEAT + 1; dr++) {
        int candidates[2] = { centre + dr, centre - dr };
        for (int k = 0; k < 2; k++) {
            int r = candidates[k];
            if (r < 0 || r >= st->num_rows) continue;
            sm_note_t *n = &st->notes[r * tracks + lane];
            if (n->type == NT_EMPTY || n->type == NT_MINE) continue;
            if (n->type == NT_HOLD_TAIL) continue;   /* marker, not a target */
            if (n->score != TNS_NONE) continue;

            float t = sm_time_from_beat(&song->timing, sm_row_to_beat(r));
            float off = t - now;
            float a = fabsf_(off);

            /* Outside the Boo window there is nothing to hit here. */
            if (a > SM_WIN_BOO) continue;

            if (best_row < 0 || a < best_abs) {
                best_row  = r;
                best_abs  = a;
            }
            /* A perfect-window hit short-circuits the outward scan. */
            if (a <= SM_WIN_MARVELOUS) { k = 2; }
        }
        if (best_row >= 0 && best_abs <= SM_WIN_MARVELOUS) break;
    }

    if (best_row < 0)
        return 0;

    sm_note_t *n = &st->notes[best_row * tracks + lane];
    float t = sm_time_from_beat(&song->timing, sm_row_to_beat(best_row));
    judge_note(n, lane, t - now);

    /* Hold heads arm the hold (Player.cpp:322-430) */
    if (n->type == NT_HOLD_HEAD && n->hold_index >= 0)
        g_game.active_hold[lane] = n->hold_index;

    sm_audio_play_hit(lane);
    return 1;
}

/* ------------------------------ holds ------------------------------ */

/*
 * Player.cpp:322-430 -- hold scoring.
 *
 * A hold is armed when its head is judged and lives until either the tail row
 * goes by with the button still down (HNS_OK) or the button comes up first
 * (HNS_NG).  Both outcomes are one-shot: active_hold is cleared immediately so
 * a lane cannot bank the same hold twice.
 */
static void score_hold(int lane, int hi, int tns)
{
    sm_steps_t *st = sm_active_steps;
    if (!st) return;

    sm_hold_t *h = &st->holds[hi];
    if (h->score != HNS_NONE) return;

    h->score = (uint8_t)tns;
    g_game.hns[tns]++;
    g_game.active_hold[lane] = -1;
}

static void update_holds(void)
{
    sm_steps_t *st = sm_active_steps;
    if (!st) return;

    const sm_song_t *song = cur_song();
    if (!song) return;

    for (int lane = 0; lane < SM_NUM_LANES; lane++) {
        int hi = g_game.active_hold[lane];
        if (hi < 0) continue;

        sm_hold_t *h = &st->holds[hi];
        if (h->score != HNS_NONE) {     /* already banked elsewhere */
            g_game.active_hold[lane] = -1;
            continue;
        }

        float end_sec = sm_time_from_beat(&song->timing, sm_row_to_beat(h->end_row));

        /* Held to the tail -> OK */
        if (g_game.music_seconds >= end_sec) {
            score_hold(lane, hi, HNS_OK);
            g_game.life += 0.02f;
            if (g_game.life > 1.0f) g_game.life = 1.0f;
            /* HandleHoldScore adds a full Marvelous row (ScoreKeeperMAX2:399) */
            add_row_score(TNS_MARVELOUS);
            continue;
        }

        /* Released early -> NG */
        if (!g_game.btn_down[lane]) {
            score_hold(lane, hi, HNS_NG);
            g_game.life -= 0.05f;
            if (g_game.life < 0.0f) g_game.life = 0.0f;
        }
    }
}

/* --------------------------- row scoring --------------------------- */

static void score_finished_rows(void)
{
    sm_steps_t *st = sm_active_steps;
    if (!st) return;

    /* Walk forward from the last row we closed; rows can be completed out of
     * order only when the player is ahead of the chart. */
    for (int r = g_game.last_row_judged; r < st->num_rows; r++) {
        if (!row_fully_judged(st, r))
            break;
        int taps = 0;
        int best = last_tap_score_row(st, r, &taps);
        if (taps == 0 && best == TNS_NONE) {
            g_game.last_row_judged = r + 1;
            continue;
        }
        finalize_row(best, taps);
        g_game.last_row_judged = r + 1;
    }
}

/*
 * Player.cpp:984-1046 -- UpdateTapNotesMissedOlderThan.
 * Anything still unjudged once it is older than the Boo window is a miss.
 */
static void update_misses(void)
{
    sm_steps_t *st = sm_active_steps;
    if (!st) return;

    const sm_song_t *song = cur_song();
    if (!song) return;

    /* Everything older than the Boo window is gone
     * (Player.cpp:1281-1284 GetMaxStepDistanceSeconds). */
    float cut_beat = g_game.song_beat - g_game.cur_bps * SM_WIN_BOO;
    int cutoff_row = sm_beat_to_row(cut_beat);

    /* Stay clear of the row being actively played. */
    int centre = sm_beat_to_row(g_game.song_beat);
    int limit  = centre - 1;
    if (limit >= cutoff_row) limit = cutoff_row - 1;
    if (limit >= st->num_rows) limit = st->num_rows - 1;

    for (int r = 0; r <= limit; r++) {
        int tracks = st->num_tracks;
        for (int c = 0; c < tracks; c++) {
            sm_note_t *n = &st->notes[r * tracks + c];
            if (n->type == NT_EMPTY || n->score != TNS_NONE) continue;

            if (n->type == NT_MINE) {
                /* A mine nobody stepped on is simply gone */
                n->score = TNS_MISS;
                continue;
            }

            n->score = TNS_MISS;
            n->offset_ms = 0;
            g_game.last_judge = TNS_MISS;
            g_game.judge_flash = 14;
            g_game.pro_timing_error += 100;
            /* A head nobody ever hit leaves the hold unresolved.  Bank it as NG
             * through the same one-shot path the release path uses, otherwise
             * the hold shows up red on screen but never reaches the tally and
             * the grade. */
            if (n->type == NT_HOLD_HEAD && n->hold_index >= 0)
                score_hold(c, n->hold_index, HNS_NG);
        }
    }
}

/* --------------------------- results ------------------------------- */

/* StageStats.cpp:144-229 -- GetGrade */
int sm_calc_grade(void)
{
    static const float thresholds[7] = {
        SM_GRADE_T1, SM_GRADE_T2, SM_GRADE_T3, SM_GRADE_T4,
        SM_GRADE_T5, SM_GRADE_T6, SM_GRADE_T7
    };
    static const int grade_w[NUM_TAP_NOTE_SCORES] = {
        0, SM_GRADE_W_HIT_MINE, SM_GRADE_W_MISS, SM_GRADE_W_BOO,
        SM_GRADE_W_GOOD, SM_GRADE_W_GREAT, SM_GRADE_W_PERFECT, SM_GRADE_W_MARVELOUS
    };
    static const int hold_w[NUM_HOLD_NOTE_SCORES] = {
        0, SM_GRADE_W_NG, SM_GRADE_W_OK
    };

    float actual = 0.0f, possible = 0.0f;
    for (int t = 0; t < NUM_TAP_NOTE_SCORES; t++) {
        actual   += (float)g_game.tns[t] * (float)grade_w[t];
        possible += (float)g_game.tns[t] * (float)SM_GRADE_W_MARVELOUS;
    }
    for (int h = 0; h < NUM_HOLD_NOTE_SCORES; h++) {
        actual   += (float)g_game.hns[h] * (float)hold_w[h];
        possible += (float)g_game.hns[h] * (float)SM_GRADE_W_OK;
    }

    float pct = (possible == 0.0f) ? 0.0f : actual / possible;

    int grade = 6;                       /* Tier07 */
    for (int g = 0; g < 7; g++) {
        if (pct >= thresholds[g] - 0.00001f) { grade = g; break; }
    }

    /* PrefsManager.cpp:143 -- GradeTier02IsAllPerfects splits the two 1.00
     * tiers that the weighted average cannot separate. */
    if (g_game.tns[TNS_MISS] == 0 && g_game.tns[TNS_BOO] == 0 &&
        g_game.tns[TNS_GOOD] == 0 && g_game.tns[TNS_HIT_MINE] == 0 &&
        g_game.tns[TNS_GREAT] == 0) {
        if (g_game.tns[TNS_MARVELOUS] > 0) grade = 0;         /* Tier01 */
        else if (g_game.tns[TNS_PERFECT] > 0) grade = 1;     /* Tier02 */
        else if (grade > 2) grade = 2;
    }
    return grade;
}

/* ScoreDisplayPercentage.cpp -- accuracy from the percent weights */
float sm_calc_percent(void)
{
    static const int w[NUM_TAP_NOTE_SCORES] = {
        0, SM_PCT_W_HIT_MINE, SM_PCT_W_MISS, SM_PCT_W_BOO,
        SM_PCT_W_GOOD, SM_PCT_W_GREAT, SM_PCT_W_PERFECT, SM_PCT_W_MARVELOUS
    };
    static const int hw[NUM_HOLD_NOTE_SCORES] = { 0, SM_PCT_W_NG, SM_PCT_W_OK };

    int actual = 0, possible = 0;
    for (int t = 0; t < NUM_TAP_NOTE_SCORES; t++) {
        actual   += g_game.tns[t] * w[t];
        possible += g_game.tns[t] * SM_PCT_W_MARVELOUS;
    }
    for (int h = 0; h < NUM_HOLD_NOTE_SCORES; h++) {
        actual   += g_game.hns[h] * hw[h];
        possible += g_game.hns[h] * SM_PCT_W_OK;
    }
    if (possible == 0) return 0.0f;
    return 100.0f * (float)actual / (float)possible;
}

int sm_calc_score(void) { return g_game.score; }

const char *sm_current_song_name(void)
{
    const sm_song_t *s = cur_song();
    return s ? s->title : "-- no song --";
}

/* --------------------------- lifecycle ----------------------------- */

static void reset_stats(void)
{
    g_game.score = 0;
    g_game.combo = 0;
    g_game.max_combo = 0;
    memset(g_game.tns, 0, sizeof(g_game.tns));
    memset(g_game.hns, 0, sizeof(g_game.hns));
    g_game.num_taps_and_holds = 0;
    g_game.taps_hit = 0;
    g_game.max_possible_points = 0;
    g_game.point_bonus = 0;
    g_game.score_remainder = 0;
    g_game.pro_timing_error = 0;
    g_game.rows_judged = 0;
    g_game.last_row_judged = 0;
    g_game.last_judge = TNS_NONE;
    g_game.judge_flash = 0;
    g_game.judge_error_ms = 0.0f;
    g_game.song_ended = 0;
    for (int i = 0; i < SM_NUM_LANES; i++) {
        g_game.active_hold[i] = -1;
        g_game.btn_down[i] = 0;
        g_game.btn_prev[i] = 0;
    }
}

void sm_game_init(void)
{
    memset(&g_game, 0, sizeof(g_game));
    /* PlayerOptions.cpp:17-37 */
    g_game.scroll_speed = 1.0f;
    g_game.time_spacing = 0;
    g_game.scroll_bpm   = 200.0f;
    g_game.wave = 0.0f;
    g_game.expand = 0.0f;
    g_game.boost = 0.0f;
    g_game.tipsy = 0.0f;
    g_game.screen = SCREEN_BOOT;
    g_game.boot_frames = 0;
    g_game.music_seconds = 0.0f;
    g_game.song_beat = 0.0f;
    g_game.cur_bps = 10.0f;
    g_game.freeze = 0;
    g_game.song_idx = 0;
    g_game.steps_idx = 0;
    g_game.sel_group = -1; /* All songs by default */
    g_game.life = 0.5f;
    g_game.theme = 0;
    g_game.preview_song_idx = -1;
    g_game.preview_timer = 0.0f;
    sm_bg_init();
    sm_theme_init();
    reset_stats();
    sm_active_steps = NULL;
}

void sm_game_reset(void)
{
    sm_active_steps = NULL;
    g_game.screen = SCREEN_SELECT;
    g_game.music_seconds = 0.0f;
    g_game.song_beat = 0.0f;
    reset_stats();
}

void sm_game_start_song(int song_idx, int steps_idx)
{
    sm_lib_song_t *lib = sm_library_get(song_idx);
    if (!lib) return;
    if (steps_idx < 0 || steps_idx >= lib->song.num_diffs) return;

    /* One difficulty grid is resident at a time; see sm_song.h. */
    static sm_steps_t steps;
    if (sm_active_steps && sm_active_steps != &steps)
        sm_steps_free(sm_active_steps);
    if (!sm_song_load_steps(&lib->song, steps_idx, &steps))
        return;

    sm_active_steps = &steps;

    g_game.song_idx  = song_idx;
    g_game.steps_idx = steps_idx;
    g_game.screen    = SCREEN_PLAY;
    g_game.life      = 0.5f;
    g_game.paused    = 0;
    sm_audio_pause_music(0);
    reset_stats();
    setup_scoring();

    /* Load background image for gameplay */
    sm_bg_load_for_song(lib->dir, lib->song.bg_file, lib->song.banner_file);

    g_game.music_seconds = -0.35f;   /* brief count-in before beat 0 */
    g_game.song_beat = 0.0f;

    if (lib->song.has_music && lib->music_path[0])
        sm_audio_open_music(lib->music_path);
    else
        sm_audio_stop_music();
}

void sm_game_song_finished(void)
{
    g_game.song_ended = 1;
    g_game.screen = SCREEN_RESULT;
    g_game.result_timer = 0.0f;
    sm_audio_stop_music();
}

/* ----------------------------- update ------------------------------ */

static void update_play(void)
{
    const sm_song_t *song = cur_song();
    sm_steps_t *st = sm_active_steps;
    if (!song || !st) return;

    /* --- song clock, driven by decoded samples --- */
    g_game.music_seconds = sm_audio_music_seconds();

    float bps = g_game.cur_bps;
    float beat = sm_beat_from_time(&song->timing, g_game.music_seconds, &bps);
    g_game.cur_bps = bps;
    g_game.song_beat = beat;

    /* --- lane edges ---
     *
     * hit_lane is re-run every frame the button is down, not only on the
     * rising edge (Player.cpp:1013-1024 runs Step() for each held track).
     * That is what lets a note that scrolls into the Boo window while the
     * player is already holding the lane still register, and it is the only
     * way a hold gets armed when the player presses early and keeps holding
     * through the head.  hit_lane only ever judges an unjudged note inside the
     * window, so re-running it cannot double-hit anything.
     */
    for (int lane = 0; lane < SM_NUM_LANES; lane++) {
        int down = sm_btn[lane];
        g_game.btn_down[lane] = (uint8_t)down;
        if (down)
            hit_lane(lane);
    }

    update_holds();
    update_misses();
    score_finished_rows();

    /* --- end of song --- */
    int last_row = st->num_rows - 1;
    float end_sec = sm_time_from_beat(&song->timing, sm_row_to_beat(last_row));
    if (g_game.music_seconds > end_sec + 1.5f)
        sm_game_song_finished();
    if (!sm_audio_music_playing() && g_game.music_seconds > 1.0f) {
        /* the stream ran out (a short file, or a missing #MUSIC) */
        if (g_game.music_seconds > end_sec + 0.5f)
            sm_game_song_finished();
    }
}

static void update_select(void)
{
    if (g_library_count == 0)
        return;

    int prev_song = g_game.song_idx;

    static int s_nav_repeat = 0;
    static int s_nav_dir = -1;

    /* Directions for select menu:
     * 0 = Up (song prev)
     * 1 = Down (song next)
     * 2 = Left (diff prev)
     * 3 = Right (diff next)
     */
    uint32_t joy = g_game.joypad;
    int up    = (joy & (1 << 2)) != 0;
    int down  = (joy & (1 << 1)) != 0;
    int left  = (joy & (1 << 0)) != 0 || sm_btn_l_pressed;
    int right = (joy & (1 << 3)) != 0 || sm_btn_r_pressed;

    int cur_dir = -1;
    if (up)         cur_dir = 0;
    else if (down)  cur_dir = 1;
    else if (left)  cur_dir = 2;
    else if (right) cur_dir = 3;

    int trigger[4] = {0};
    if (cur_dir >= 0) {
        if (cur_dir != s_nav_dir) {
            s_nav_dir = cur_dir;
            s_nav_repeat = 0;
            trigger[cur_dir] = 1;
        } else {
            s_nav_repeat++;
            if (s_nav_repeat >= 18 && ((s_nav_repeat - 18) % 6 == 0)) {
                trigger[cur_dir] = 1;
            }
        }
    } else {
        s_nav_dir = -1;
        s_nav_repeat = 0;
    }

    /* Folder / Album filtering */
    int visible_songs[SM_MAX_SONGS];
    int visible_count = 0;

    if (g_game.sel_group >= 0 && g_game.sel_group < g_group_count) {
        sm_lib_group_t *grp = &g_groups[g_game.sel_group];
        visible_count = grp->num_songs;
        for (int i = 0; i < visible_count; i++)
            visible_songs[i] = grp->song_indices[i];
    } else {
        visible_count = g_library_count;
        for (int i = 0; i < visible_count; i++)
            visible_songs[i] = i;
    }

    if (visible_count == 0) return;

    /* Current position within visible filtered list */
    int cur_vis_pos = 0;
    for (int i = 0; i < visible_count; i++) {
        if (visible_songs[i] == g_game.song_idx) {
            cur_vis_pos = i;
            break;
        }
    }

    /* Switch folder / album using SELECT */
    if (sm_sel_pressed && !g_game.sel_prev && g_group_count > 1) {
        g_game.sel_group++;
        if (g_game.sel_group >= g_group_count)
            g_game.sel_group = -1; /* Loop back to ALL */

        if (g_game.sel_group >= 0) {
            sm_lib_group_t *grp = &g_groups[g_game.sel_group];
            if (grp->num_songs > 0)
                g_game.song_idx = grp->song_indices[0];
        } else {
            g_game.song_idx = 0;
        }
        sm_audio_play_menu();
    }

    if (trigger[0]) {          /* up */
        cur_vis_pos--;
        if (cur_vis_pos < 0) cur_vis_pos = visible_count - 1;
        g_game.song_idx = visible_songs[cur_vis_pos];
    }
    if (trigger[1]) {          /* down */
        cur_vis_pos++;
        if (cur_vis_pos >= visible_count) cur_vis_pos = 0;
        g_game.song_idx = visible_songs[cur_vis_pos];
    }

    sm_lib_song_t *lib = sm_library_get(g_game.song_idx);
    int diffs = lib ? lib->song.num_diffs : 0;

    if (g_game.song_idx != prev_song) {
        if (g_game.sel_diff >= diffs)
            g_game.sel_diff = diffs > 0 ? diffs - 1 : 0;
        sm_audio_stop_music();
        g_game.preview_song_idx = -1;
        g_game.preview_timer = 0.0f;

        if (lib && lib->banner_path[0])
            sm_banner_load_file(lib->banner_path);
        else
            sm_banner_clear();
        sm_audio_play_menu();
    }

    int prev_diff = g_game.sel_diff;
    if (trigger[3] && diffs) {  /* right */
        g_game.sel_diff = (g_game.sel_diff + 1) % diffs;
    }
    if (trigger[2] && diffs) {  /* left */
        g_game.sel_diff = (g_game.sel_diff + diffs - 1) % diffs;
    }
    if (g_game.sel_diff != prev_diff) {
        sm_audio_play_menu();
    }

    /* Song preview after brief hover */
    g_game.preview_timer += 1.0f / 60.0f;
    if (g_game.preview_timer >= 0.4f && g_game.preview_song_idx != g_game.song_idx) {
        if (lib && lib->song.has_music && lib->music_path[0]) {
            sm_audio_open_music(lib->music_path);
            g_game.preview_song_idx = g_game.song_idx;
        }
    }

    int play_confirm = (sm_start_pressed && !g_game.start_prev) ||
                       (sm_btn_a_pressed && !s_btn_a_prev);
    if (play_confirm && diffs) {
        g_game.sel_scroll = 0;
        sm_audio_stop_music();
        sm_game_start_song(g_game.song_idx, g_game.sel_diff);
    }

    if (sm_btn_b_pressed && !s_btn_b_prev) {
        sm_audio_stop_music();
        g_game.preview_song_idx = -1;
        g_game.preview_timer = 0.0f;
    }

    s_btn_a_prev = sm_btn_a_pressed;
    s_btn_b_prev = sm_btn_b_pressed;

    for (int i = 0; i < SM_NUM_LANES; i++)
        g_game.btn_prev[i] = sm_btn[i];
}

void sm_game_update(void)
{
    g_game.frame_count++;
    if (g_game.judge_flash > 0) g_game.judge_flash--;

    switch (g_game.screen) {
    case SCREEN_BOOT:
        g_game.boot_frames++;
        if (g_game.boot_frames > 105) {
            g_game.screen = SCREEN_SELECT;
            sm_theme_apply(g_game.theme);
            sm_lib_song_t *lib = sm_library_get(g_game.song_idx);
            if (lib && lib->banner_path[0])
                sm_banner_load_file(lib->banner_path);
            else
                sm_banner_clear();
        }
        for (int i = 0; i < SM_NUM_LANES; i++) g_game.btn_prev[i] = sm_btn[i];
        break;

    case SCREEN_SELECT:
        update_select();
        if (g_game.sel_scroll > 0) g_game.sel_scroll--;
        break;

    case SCREEN_PLAY:
        if (sm_start_pressed && !g_game.start_prev) {
            g_game.paused = !g_game.paused;
            sm_audio_pause_music(g_game.paused);
        }
        if (g_game.paused) {
            if (sm_sel_pressed && !g_game.sel_prev) {
                g_game.paused = 0;
                sm_audio_stop_music();
                g_game.screen = SCREEN_SELECT;
                g_game.preview_timer = 0.0f;
                g_game.preview_song_idx = -1;
                sm_theme_apply(g_game.theme);
                sm_lib_song_t *lib = sm_library_get(g_game.song_idx);
                if (lib && lib->banner_path[0])
                    sm_banner_load_file(lib->banner_path);
                else
                    sm_banner_clear();
            }
        } else {
            update_play();
        }
        for (int i = 0; i < SM_NUM_LANES; i++) g_game.btn_prev[i] = sm_btn[i];
        break;

    case SCREEN_RESULT:
        g_game.result_timer += 1.0f / 60.0f;
        if (g_game.result_timer > 1.0f &&
            ((sm_start_pressed && !g_game.start_prev) || (sm_sel_pressed && !g_game.sel_prev))) {
            g_game.screen = SCREEN_SELECT;
            g_game.preview_timer = 0.0f;
            g_game.preview_song_idx = -1;
            sm_theme_apply(g_game.theme);
            sm_lib_song_t *lib = sm_library_get(g_game.song_idx);
            if (lib && lib->banner_path[0])
                sm_banner_load_file(lib->banner_path);
            else
                sm_banner_clear();
        }
        for (int i = 0; i < SM_NUM_LANES; i++) g_game.btn_prev[i] = sm_btn[i];
        break;

    default:
        break;
    }

    g_game.start_prev = sm_start_pressed;
    g_game.sel_prev   = sm_sel_pressed;
    g_game.btn_l_prev = sm_btn_l_pressed;
    g_game.btn_r_prev = sm_btn_r_pressed;
}