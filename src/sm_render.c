#include "sm_render.h"
#include "sm_game.h"
#include "sm_song.h"
#include "sm_library.h"
#include "sm_audio.h"
#include "sm_bg.h"
#include "sm_theme.h"
#include "sm_logo_data.h"
#include "font8x8.h"
#include <stdio.h>
#include <string.h>

static uint16_t diff_color(int diff)
{
    switch (diff) {
        case DIFFICULTY_BEGINNER: return COLOR_RGB565(40, 220, 255);
        case DIFFICULTY_EASY:     return COLOR_RGB565(40, 255, 80);
        case DIFFICULTY_MEDIUM:   return COLOR_RGB565(255, 220, 40);
        case DIFFICULTY_HARD:     return COLOR_RGB565(255, 50, 50);
        case DIFFICULTY_CHALLENGE:return COLOR_RGB565(200, 60, 255);
        default:                  return COLOR_RGB565(180, 180, 180);
    }
}

static const char *diff_name(int diff)
{
    switch (diff) {
        case DIFFICULTY_BEGINNER: return "BEGINNER";
        case DIFFICULTY_EASY:     return "EASY";
        case DIFFICULTY_MEDIUM:   return "MEDIUM";
        case DIFFICULTY_HARD:     return "HARD";
        case DIFFICULTY_CHALLENGE:return "CHALLENGE";
        default:                  return "EDIT";
    }
}

/* ---------------------------------------------------------------------------
 * 320x240 RGB565 renderer.
 *
 * Layout follows the PSPMania theme metrics quoted in the port's own bug
 * tracker (ReceptorArrowsYStandard=58 / ReceptorArrowsYReverse=210 on a
 * 272-tall PSP screen), rescaled to the GB300/SF2000 240-tall panel: the
 * judgment line sits low and the note field runs up the screen, which is what
 * keeps the score readout clear of the arrows.
 *
 * Chroma key: 0x0000 is fully transparent in the generated sprite tables, so
 * a sprite blit is a single compare-and-store with no alpha blend.
 * ------------------------------------------------------------------------- */

static int s_tipsy_phase;

void sm_render_init(void)
{
    sm_assets_init();
    s_tipsy_phase = 0;
}

/* ---------------------------- primitives --------------------------- */

void sm_render_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t col)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SM_SCREEN_WIDTH)  w = SM_SCREEN_WIDTH - x;
    if (y + h > SM_SCREEN_HEIGHT) h = SM_SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    for (int j = 0; j < h; j++) {
        uint16_t *row = fb + (y + j) * SM_SCREEN_WIDTH + x;
        for (int i = 0; i < w; i++) row[i] = col;
    }
}

void sm_render_rect_outline(uint16_t *fb, int x, int y, int w, int h, uint16_t col)
{
    sm_render_rect(fb, x, y, w, 1, col);
    sm_render_rect(fb, x, y + h - 1, w, 1, col);
    sm_render_rect(fb, x, y, 1, h, col);
    sm_render_rect(fb, x + w - 1, y, 1, h, col);
}

void sm_render_text(uint16_t *fb, int x, int y, const char *s, uint16_t col, int scale)
{
    if (scale < 1) scale = 1;
    int cx = x;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        const uint8_t *g = font8x8_basic[c - 32];
        for (int r = 0; r < 8; r++) {
            uint8_t bits = g[r];
            if (!bits) continue;
            for (int col_i = 0; col_i < 8; col_i++) {
                if (!(bits & (0x80 >> col_i))) continue;
                if (scale == 1) {
                    int px = cx + col_i;
                    int py = y + r;
                    if (px >= 0 && px < SM_SCREEN_WIDTH && py >= 0 && py < SM_SCREEN_HEIGHT)
                        fb[py * SM_SCREEN_WIDTH + px] = col;
                } else {
                    sm_render_rect(fb, cx + col_i * scale, y + r * scale,
                                   scale, scale, col);
                }
            }
        }
        cx += 8 * scale;
        if (cx >= SM_SCREEN_WIDTH) break;
    }
}

void sm_render_sprite(uint16_t *fb, int x, int y, const sm_sprite_t *sp, int frame,
                      uint16_t key)
{
    if (!sp || frame < 0 || frame >= sp->num_frames) return;
    const uint16_t *src = sp->frames[frame];
    if (!src) return;

    for (int j = 0; j < sp->h; j++) {
        int py = y + j;
        if (py < 0 || py >= SM_SCREEN_HEIGHT) continue;
        uint16_t *row = fb + py * SM_SCREEN_WIDTH;
        for (int i = 0; i < sp->w; i++) {
            int px = x + i;
            if (px < 0 || px >= SM_SCREEN_WIDTH) continue;
            uint16_t v = src[j * sp->w + i];
            if (v != key) row[px] = v;
        }
    }
}

static inline uint16_t blend_rgb565(uint16_t bg, uint16_t fg, uint8_t a)
{
    if (a == 0) return bg;
    if (a >= 250) return fg;
    uint32_t br = (bg >> 11) & 0x1F;
    uint32_t bg_c = (bg >> 5)  & 0x3F;
    uint32_t bb =  bg        & 0x1F;
    uint32_t fr = (fg >> 11) & 0x1F;
    uint32_t fg_c = (fg >> 5)  & 0x3F;
    uint32_t fb_c =  fg        & 0x1F;
    uint32_t inv = 256 - a;
    uint32_t r = (fr * a + br * inv + 128) >> 8;
    uint32_t g = (fg_c * a + bg_c * inv + 128) >> 8;
    uint32_t b = (fb_c * a + bb * inv + 128) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void sm_render_logo(uint16_t *fb, int x, int y)
{
    for (int j = 0; j < SM_LOGO_H; j++) {
        int py = y + j;
        if (py < 0 || py >= SM_SCREEN_HEIGHT) continue;
        uint16_t *row = fb + py * SM_SCREEN_WIDTH;
        for (int i = 0; i < SM_LOGO_W; i++) {
            int px = x + i;
            if (px < 0 || px >= SM_SCREEN_WIDTH) continue;
            int idx = j * SM_LOGO_W + i;
            uint8_t a = sm_logo_alpha[idx];
            if (a == 0) continue;
            uint16_t fg = sm_logo_rgb565[idx];
            if (a >= 250) {
                row[px] = fg;
            } else {
                row[px] = blend_rgb565(row[px], fg, a);
            }
        }
    }
}

/* ----------------------- drawing helpers --------------------------- */

/* Lane centre X.  dance-single is centred on the screen; the column spacing is
 * widened from the PSP build's DANCE_COL_SPACING=36 so four 36px arrows fill
 * the 320px panel without touching the edges
 * (GameManager.cpp:1232-1235 gives the relative offsets -1.5/-0.5/+0.5/+1.5). */
static int lane_x(int lane)
{
    static const float rel[4] = { -1.5f, -0.5f, 0.5f, 1.5f };
    int centre = SM_SCREEN_WIDTH / 2;
    return centre + (int)(rel[lane] * (float)SM_COL_SPACING) - SM_ARROW_SIZE / 2;
}

static void draw_gradient(uint16_t *fb, uint16_t top, uint16_t bot)
{
    for (int y = 0; y < SM_SCREEN_HEIGHT; y++) {
        int t = (y * 255) / (SM_SCREEN_HEIGHT - 1);
        int r = (((top >> 11) & 0x1F) * (255 - t) + ((bot >> 11) & 0x1F) * t) >> 8;
        int g = (((top >> 5)  & 0x3F) * (255 - t) + ((bot >> 5)  & 0x3F) * t) >> 8;
        int b = (( top        & 0x1F) * (255 - t) + ( bot        & 0x1F) * t) >> 8;
        uint16_t c = COLOR_RGB565(r, g, b);
        uint16_t *row = fb + y * SM_SCREEN_WIDTH;
        for (int x = 0; x < SM_SCREEN_WIDTH; x++) row[x] = c;
    }
}

/* ------------------------------ HUD -------------------------------- */

static void draw_score(uint16_t *fb)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%09d", g_game.score);
    sm_render_text(fb, SM_SCREEN_WIDTH / 2 - 4 * 9, 4, buf, COLOR_WHITE, 1);
}

static void draw_judgment(uint16_t *fb)
{
    if (g_game.judge_flash <= 0) return;
    const char *name = sm_judge_short[g_game.last_judge];
    int w = (int)strlen(name) * 8;
    int x = SM_SCREEN_WIDTH / 2 - w / 2;
    int y = 150 - (18 - g_game.judge_flash);

    uint16_t col = COLOR_WHITE;
    switch (g_game.last_judge) {
        case TNS_MARVELOUS: col = COLOR_MATRIX_GRN; break;
        case TNS_PERFECT:   col = COLOR_YELLOW;    break;
        case TNS_GREAT:     col = COLOR_CYAN;      break;
        case TNS_GOOD:      col = COLOR_AMBER;     break;
        case TNS_BOO:
        case TNS_MISS:      col = COLOR_RED;       break;
        case TNS_HIT_MINE:  col = COLOR_RED;       break;
        default: break;
    }
    sm_render_text(fb, x, y, name, col, 1);
}

static void draw_combo(uint16_t *fb)
{
    if (g_game.combo < 2) return;
    char buf[24];
    snprintf(buf, sizeof(buf), "%d", g_game.combo);
    sm_render_text(fb, SM_SCREEN_WIDTH / 2 - (int)strlen(buf) * 2, 164, buf, COLOR_WHITE, 2);
    sm_render_text(fb, SM_SCREEN_WIDTH / 2 - 12, 180, "COMBO", COLOR_GREY, 1);
}

/* --------------------------- gameplay ----------------------------- */

static void draw_note_field(uint16_t *fb)
{
    sm_steps_t *st = sm_active_steps;
    sm_lib_song_t *lib = sm_library_get(g_game.song_idx);
    if (!st || !lib) return;

    int tracks = st->num_tracks;

    /* Beat window currently on screen. */
    float beats_visible = (float)SM_NOTE_FIELD_HEIGHT / ((float)SM_ARROW_SIZE * g_game.scroll_speed);
    if (beats_visible < 1.0f) beats_visible = 1.0f;
    float beat_hi = g_game.song_beat + beats_visible + 1.0f;
    float beat_lo = g_game.song_beat - 1.0f;

    int row_lo = sm_beat_to_row(beat_lo);
    int row_hi = sm_beat_to_row(beat_hi);
    if (row_lo < 0) row_lo = 0;
    if (row_hi > st->num_rows) row_hi = st->num_rows;

/* Draw holds bodies first so the note sprites overlay them. */
for (int i = 0; i < st->num_holds; i++) {
        sm_hold_t *h = &st->holds[i];
        /* A hold stays visible while any part of it is on screen: a long hold's
         * head scrolls off the top long before its tail arrives, and keying the
         * body off the head made the whole thing vanish. */
        if (h->end_row < row_lo || h->start_row > row_hi) continue;

        /* find the lane by looking for the hold head note */
        int lane = -1;
        for (int c = 0; c < tracks; c++) {
            sm_note_t *n = &st->notes[h->start_row * tracks + c];
            if (n->type == NT_HOLD_HEAD && n->hold_index == i) { lane = c; break; }
        }
        if (lane < 0) continue;

        int y_head = (int)sm_arrow_y(lane, sm_row_to_beat(h->start_row));
        int y_tail = (int)sm_arrow_y(lane, sm_row_to_beat(h->end_row));
        if (y_head > SM_RECEPTOR_Y) y_head = SM_RECEPTOR_Y;
        if (y_tail < SM_NOTE_FIELD_TOP - 64) y_tail = SM_NOTE_FIELD_TOP - 64;

        int x = lane_x(lane);
        int half = SM_ARROW_SIZE / 2;
        uint16_t body, edge;
        if      (h->score == HNS_OK) { body = COLOR_MATRIX_GRN; edge = COLOR_RGB565(0, 90, 40); }
        else if (h->score == HNS_NG) { body = COLOR_RED;       edge = COLOR_RGB565(90, 0, 0); }
        else                        { body = COLOR_RGB565(120, 120, 200); edge = COLOR_RGB565(60, 60, 130); }

        /* An active hold grows to meet the receptor; otherwise the body spans
         * exactly head-to-tail. */
        int bot = y_head;
        if (h->score == HNS_NONE && g_game.active_hold[lane] == i)
            bot = SM_RECEPTOR_Y;

        int top = y_tail;
        if (bot - top < 0) top = bot;

        sm_render_rect(fb, x + half - 5, top, 10, bot - top, edge);
        sm_render_rect(fb, x + half - 3, top,  6, bot - top, body);

        /* bottom cap at the tail end */
        sm_render_sprite(fb, x, y_tail, &sm_spr_holdcap[lane], 0, 0x0000);
    }

    /* Tap notes and mines. */
    for (int r = row_lo; r < row_hi; r++) {
        float y = sm_arrow_y(0, sm_row_to_beat(r));
        if (y < SM_NOTE_FIELD_TOP - SM_ARROW_SIZE) continue;
        if (y > SM_RECEPTOR_Y + SM_ARROW_SIZE) continue;
        int iy = (int)y;

        for (int c = 0; c < tracks && c < SM_NUM_LANES; c++) {
            sm_note_t *n = &st->notes[r * tracks + c];
            if (n->type == NT_EMPTY || n->type == NT_HOLD_TAIL) continue;
            if (n->score == TNS_MISS) continue;       /* sinks away once missed */

            int x = lane_x(c);
            if (n->type == NT_MINE) {
                /* mines are a red X on a dim plate */
                sm_render_rect(fb, x + 6, iy + 6, SM_ARROW_SIZE - 12,
                               SM_ARROW_SIZE - 12, COLOR_RGB565(120, 20, 20));
                sm_render_rect(fb, x + 10, iy + 10, SM_ARROW_SIZE - 20,
                               4, COLOR_YELLOW);
                continue;
            }
            if (n->score != TNS_NONE) {
                /* hit: flash the explosion sprite */
                sm_render_sprite(fb, x - 2, iy - 2, &sm_spr_boom[c], 0, 0x0000);
                continue;
            }
            /* A hold head has its own sprite so the player can tell at a
             * glance which notes want to be held. */
            if (n->type == NT_HOLD_HEAD)
                sm_render_sprite(fb, x, iy, &sm_spr_holdhead[c], 0, 0x0000);
            else
                sm_render_sprite(fb, x, iy, &sm_spr_tap[c], 0, 0x0000);
        }
    }

    /* Receptors sit on top of everything. */
    for (int lane = 0; lane < SM_NUM_LANES; lane++) {
        int x = lane_x(lane);
        int y = SM_RECEPTOR_Y;
        if (g_game.btn_down[lane] || g_game.active_hold[lane] >= 0)
            sm_render_sprite(fb, x, y, &sm_spr_reclit[lane], 0, 0x0000);
        else
            sm_render_sprite(fb, x, y, &sm_spr_receptor[lane], 0, 0x0000);
    }
}

static void draw_life_bar(uint16_t *fb, const sm_theme_t *th)
{
    int bar_x = 48;
    int bar_y = 6;
    int bar_w = 224;
    int bar_h = 7;

    sm_render_rect_outline(fb, bar_x - 1, bar_y - 1, bar_w + 2, bar_h + 2, COLOR_WHITE);
    sm_render_rect(fb, bar_x, bar_y, bar_w, bar_h, COLOR_BLACK);

    float life = g_game.life;
    if (life < 0.0f) life = 0.0f;
    if (life > 1.0f) life = 1.0f;

    int fill_w = (int)(life * (float)bar_w);
    int is_danger = (life <= 0.20f);
    int danger_flash = (g_game.frame_count / 8) & 1;

    for (int x = 0; x < fill_w; x++) {
        if ((x % 6) == 5) continue; /* DDR MAX2 segmented blocks */
        uint16_t c;
        if (is_danger) {
            c = danger_flash ? th->life_low : COLOR_WHITE;
        } else {
            float pct = (float)x / (float)bar_w;
            if (pct < 0.35f) {
                c = th->life_low;
            } else if (pct < 0.70f) {
                c = th->life_mid;
            } else {
                c = th->life_full;
            }
        }
        for (int y = 0; y < bar_h; y++) {
            fb[(bar_y + y) * SM_SCREEN_WIDTH + (bar_x + x)] = c;
        }
    }

    if (is_danger && danger_flash) {
        sm_render_text(fb, bar_x + bar_w / 2 - 24, bar_y + 10, "DANGER!", th->life_low, 1);
    }
}

static void draw_play(uint16_t *fb)
{
    const sm_theme_t *th = sm_theme_current();

    /* 1. Fullscreen song background or gradient */
    if (sm_bg_has_image()) {
        sm_bg_render_fullscreen(fb, 35);
    } else {
        draw_gradient(fb, th->header_bg, COLOR_RGB565(0, 0, 16));
    }

    /* 2. Top bar: song title & difficulty */
    sm_lib_song_t *lib = sm_library_get(g_game.song_idx);
    if (lib) {
        char title_buf[32];
        snprintf(title_buf, sizeof(title_buf), "%.24s", lib->song.title);
        sm_render_text(fb, 4, 16, title_buf, th->header_accent, 1);

        if (g_game.steps_idx >= 0 && g_game.steps_idx < lib->song.num_diffs) {
            sm_diff_t *d = &lib->song.diffs[g_game.steps_idx];
            char dstr[24];
            snprintf(dstr, sizeof(dstr), "%s %d", diff_name(d->difficulty), d->meter);
            uint16_t dcol = diff_color(d->difficulty);
            int dw = (int)strlen(dstr) * 8 + 6;
            int dx = SM_SCREEN_WIDTH - dw - 4;
            sm_render_rect(fb, dx, 14, dw, 11, dcol);
            sm_render_text(fb, dx + 3, 16, dstr, COLOR_BLACK, 1);
        }
    }

    /* 3. Life Bar */
    draw_life_bar(fb, th);

    /* 4. Note Field */
    draw_note_field(fb);

    /* 5. Judgment line */
    sm_render_rect(fb, lane_x(0) - 8, SM_RECEPTOR_Y + SM_ARROW_SIZE - 1,
                   lane_x(3) + SM_ARROW_SIZE - lane_x(0) + 16, 1,
                   th->header_accent);

    /* 6. Feedback */
    draw_judgment(fb);
    draw_combo(fb);
    draw_score(fb);

    /* 7. Pause overlay modal */
    if (g_game.paused) {
        for (int y = 60; y < 170; y++) {
            for (int x = 40; x < 280; x++) {
                uint16_t p = fb[y * SM_SCREEN_WIDTH + x];
                fb[y * SM_SCREEN_WIDTH + x] = (p >> 1) & 0x7BEF;
            }
        }
        sm_render_rect(fb, 44, 65, 232, 100, COLOR_BLACK);
        sm_render_rect_outline(fb, 44, 65, 232, 100, th->header_accent);
        sm_render_rect_outline(fb, 46, 67, 228, 96, th->panel_border);

        sm_render_text(fb, 112, 78, "PAUSED", th->header_accent, 2);
        sm_render_text(fb, 60, 110, "START : RESUME SONG", COLOR_WHITE, 1);
        sm_render_text(fb, 60, 126, "SELECT: EXIT TO WHEEL", COLOR_GREY, 1);
    }
}

/* ---------------------------- select ------------------------------ */

static void draw_select(uint16_t *fb)
{
    const sm_theme_t *th = sm_theme_current();

    if (sm_bg_has_image()) {
        sm_bg_render_fullscreen(fb, 25);
    } else {
        draw_gradient(fb, th->header_bg, COLOR_RGB565(18, 6, 12));
    }

    /* 1. Top Header Capsule (modeled on stepmania.png) */
    sm_render_rect(fb, 6, 4, 308, 20, th->header_bg);
    sm_render_rect_outline(fb, 6, 4, 308, 20, th->panel_border);
    sm_render_logo(fb, 10, 5);
    sm_render_text(fb, 108, 10, "SELECT MUSIC", th->header_accent, 1);

    /* Event Mode badge on top right */
    sm_render_rect(fb, 222, 6, 88, 15, th->panel_bg);
    sm_render_rect_outline(fb, 222, 6, 88, 15, th->sel_border);
    sm_render_text(fb, 228, 10, "EVENT MODE", COLOR_WHITE, 1);

    if (g_library_count == 0) {
        sm_render_rect(fb, 16, 40, 288, 110, th->panel_bg);
        sm_render_rect_outline(fb, 16, 40, 288, 110, th->header_accent);
        sm_render_text(fb, 28, 54, "NO SONGS FOUND", COLOR_RED, 1);
        sm_render_text(fb, 28, 72, "COPY THE FROGGYSM SONGS", COLOR_AMBER, 1);
        sm_render_text(fb, 28, 84, "FOLDER TO THE SD CARD:", COLOR_GREY, 1);
        sm_render_text(fb, 28, 96, "/ROMS/froggysm/Songs", COLOR_WHITE, 1);
        sm_render_text(fb, 28, 114, "START = RESCAN", COLOR_CYAN, 1);

        sm_render_rect(fb, 6, 214, 308, 22, th->panel_bg);
        sm_render_rect_outline(fb, 6, 214, 308, 22, th->panel_border);
        sm_render_text(fb, 16, 220, "START: RESCAN SONGS", th->header_accent, 1);
        return;
    }

    int sel = g_game.song_idx;
    sm_lib_song_t *lib = sm_library_get(sel);

    /* 2. Left Panel: Song Information, Banner, BPM, and Difficulty Meters */
    int lx = 6, ly = 26, lw = 146, lh = 184;
    sm_render_rect(fb, lx, ly, lw, lh, th->panel_bg);
    sm_render_rect_outline(fb, lx, ly, lw, lh, th->panel_border);

    if (lib) {
        /* A. Banner preview at top-left */
        int bx = lx + 5, by = ly + 4, bw = 136, bh = 44;
        if (sm_banner_has_image()) {
            sm_banner_render(fb, bx, by, bw, bh);
            sm_render_rect_outline(fb, bx - 1, by - 1, bw + 2, bh + 2, th->panel_border);
        } else {
            sm_render_rect(fb, bx, by, bw, bh, COLOR_RGB565(22, 7, 12));
            sm_render_rect_outline(fb, bx - 1, by - 1, bw + 2, bh + 2, th->panel_border);
            sm_render_text(fb, bx + 24, by + 12, "StepMania", th->header_accent, 1);
            sm_render_text(fb, bx + 42, by + 24, "[BANNER]", COLOR_GREY, 1);
        }

        /* B. Title & Artist */
        char buf[32];
        snprintf(buf, sizeof(buf), "%.16s", lib->song.title);
        sm_render_text(fb, lx + 6, ly + 50, buf, COLOR_WHITE, 1);

        snprintf(buf, sizeof(buf), "%.16s", lib->song.artist);
        sm_render_text(fb, lx + 6, ly + 60, buf, COLOR_CYAN, 1);

        /* C. BPM Range & Steps */
        float min_bpm = 99999.0f, max_bpm = 0.0f;
        for (int b = 0; b < lib->song.timing.num_bpm; b++) {
            float v = lib->song.timing.bpm[b].bpm;
            if (v < min_bpm) min_bpm = v;
            if (v > max_bpm) max_bpm = v;
        }
        if (lib->song.timing.num_bpm == 0) { min_bpm = max_bpm = 120.0f; }

        char bpm_buf[20];
        if ((int)(min_bpm + 0.5f) == (int)(max_bpm + 0.5f)) {
            snprintf(bpm_buf, sizeof(bpm_buf), "BPM %d", (int)(min_bpm + 0.5f));
        } else {
            snprintf(bpm_buf, sizeof(bpm_buf), "%d-%d", (int)(min_bpm + 0.5f), (int)(max_bpm + 0.5f));
        }
        sm_render_text(fb, lx + 6, ly + 72, bpm_buf, COLOR_RGB565(60, 240, 60), 1);

        sm_diff_t *cur_d = (g_game.sel_diff >= 0 && g_game.sel_diff < lib->song.num_diffs)
                           ? &lib->song.diffs[g_game.sel_diff] : NULL;
        char steps_buf[16];
        if (cur_d) {
            snprintf(steps_buf, sizeof(steps_buf), "STP %d", cur_d->num_steps);
        } else {
            snprintf(steps_buf, sizeof(steps_buf), "STP ---");
        }
        sm_render_text(fb, lx + 74, ly + 72, steps_buf, COLOR_YELLOW, 1);

        /* D. StepMania Difficulty Rows (in full letters) */
        static const struct {
            int diff_id;
            const char *label;
            uint16_t color;
        } tiers[5] = {
            { DIFFICULTY_BEGINNER,  "BEGINNER",  COLOR_RGB565(40, 220, 255) },
            { DIFFICULTY_EASY,      "EASY",      COLOR_RGB565(40, 255, 80) },
            { DIFFICULTY_MEDIUM,    "MEDIUM",    COLOR_RGB565(255, 220, 40) },
            { DIFFICULTY_HARD,      "HARD",      COLOR_RGB565(255, 60, 60) },
            { DIFFICULTY_CHALLENGE, "CHALLENGE", COLOR_RGB565(200, 60, 255) }
        };

        int dy_start = ly + 85;
        for (int t = 0; t < 5; t++) {
            int ry = dy_start + t * 19;
            const sm_diff_t *d = NULL;
            int d_idx = -1;
            for (int i = 0; i < lib->song.num_diffs; i++) {
                if (lib->song.diffs[i].difficulty == tiers[t].diff_id) {
                    d = &lib->song.diffs[i];
                    d_idx = i;
                    break;
                }
            }

            int is_active = (d_idx >= 0 && d_idx == g_game.sel_diff);
            if (is_active) {
                sm_render_rect(fb, lx + 4, ry - 1, lw - 8, 17, th->sel_bg);
                sm_render_rect_outline(fb, lx + 4, ry - 1, lw - 8, 17, th->sel_border);
                sm_render_text(fb, lx + 6, ry + 4, ">", COLOR_WHITE, 1);
            } else {
                sm_render_rect(fb, lx + 4, ry - 1, lw - 8, 17, COLOR_RGB565(32, 10, 16));
                sm_render_rect_outline(fb, lx + 4, ry - 1, lw - 8, 17, COLOR_RGB565(64, 22, 32));
            }

            if (d) {
                sm_render_text(fb, lx + 16, ry + 4, tiers[t].label, is_active ? COLOR_WHITE : tiers[t].color, 1);
                char mstr[8];
                snprintf(mstr, sizeof(mstr), "%2d", d->meter);
                int tag_x = lx + lw - 26;
                sm_render_rect(fb, tag_x - 2, ry + 2, 20, 11, tiers[t].color);
                sm_render_text(fb, tag_x, ry + 4, mstr, COLOR_BLACK, 1);
            } else {
                sm_render_text(fb, lx + 16, ry + 4, tiers[t].label, COLOR_GREY, 1);
                sm_render_text(fb, lx + lw - 24, ry + 4, "--", COLOR_GREY, 1);
            }
        }
    }

    /* 3. Right Panel: The Music Wheel */
    int rx = 156, ry = 26, rw = 158, rh = 184;
    sm_render_rect(fb, rx, ry, rw, rh, COLOR_RGB565(26, 8, 14));
    sm_render_rect_outline(fb, rx, ry, rw, rh, th->panel_border);

    /* Group / Album header tab at top of music wheel */
    char grp_banner[32];
    if (g_game.sel_group >= 0 && g_game.sel_group < g_group_count) {
        snprintf(grp_banner, sizeof(grp_banner), "[ %s ]", g_groups[g_game.sel_group].name);
    } else {
        snprintf(grp_banner, sizeof(grp_banner), "[ ALL SONGS ]");
    }
    sm_render_rect(fb, rx + 2, ry + 2, rw - 4, 12, COLOR_RGB565(40, 20, 60));
    sm_render_text(fb, rx + 6, ry + 4, grp_banner, COLOR_YELLOW, 1);

    /* Collect songs matching current group filter */
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

    int cur_pos = 0;
    for (int i = 0; i < visible_count; i++) {
        if (visible_songs[i] == sel) {
            cur_pos = i;
            break;
        }
    }

    static const int slot_offsets[7] = { -3, -2, -1, 0, 1, 2, 3 };
    static const int slot_y[7]       = { 30, 53, 76, 100, 128, 151, 174 };
    static const int slot_h[7]       = { 20, 20, 20,  25,  20,  20,  20 };

    if (cur_pos > 0)
        sm_render_text(fb, rx + rw / 2 - 4, ry + 14, "^", th->header_accent, 1);
    if (cur_pos < visible_count - 1)
        sm_render_text(fb, rx + rw / 2 - 4, ry + rh - 9, "v", th->header_accent, 1);

    for (int s = 0; s < 7; s++) {
        int target_pos = cur_pos + slot_offsets[s];
        if (target_pos < 0 || target_pos >= visible_count) continue;

        sm_lib_song_t *s_song = sm_library_get(visible_songs[target_pos]);
        if (!s_song) continue;

        int iy = slot_y[s];
        int ih = slot_h[s];

        if (slot_offsets[s] == 0) {
            int ix = rx - 6;
            int iw = rw + 6;
            sm_render_rect(fb, ix, iy, iw, ih, th->sel_bg);
            sm_render_rect_outline(fb, ix, iy, iw, ih, th->sel_border);
            sm_render_rect(fb, ix + 1, iy + 1, iw - 2, 1, COLOR_RGB565(255, 170, 180));

            char title_buf[32];
            snprintf(title_buf, sizeof(title_buf), "> %.17s", s_song->song.title);
            sm_render_text(fb, ix + 4, iy + 4, title_buf, COLOR_WHITE, 1);

            char sub_buf[24];
            snprintf(sub_buf, sizeof(sub_buf), "%.18s", s_song->song.artist);
            sm_render_text(fb, ix + 12, iy + 14, sub_buf, COLOR_RGB565(240, 215, 180), 1);
        } else {
            int ix = rx + 3;
            int iw = rw - 6;
            sm_render_rect(fb, ix, iy, iw, ih, COLOR_RGB565(48, 16, 24));
            sm_render_rect_outline(fb, ix, iy, iw, ih, COLOR_RGB565(88, 30, 42));

            char title_buf[24];
            snprintf(title_buf, sizeof(title_buf), "%.15s", s_song->song.title);
            sm_render_text(fb, ix + 6, iy + 6, title_buf, COLOR_RGB565(210, 195, 200), 1);
        }
    }

    /* 4. Bottom Helper Bar */
    int bot_y = 214, bot_h = 22;
    sm_render_rect(fb, 6, bot_y, 308, bot_h, th->header_bg);
    sm_render_rect_outline(fb, 6, bot_y, 308, bot_h, th->panel_border);
    sm_render_text(fb, 12, bot_y + 4, "A/START: PLAY   B: CANCEL   UP/DN: SONG", COLOR_WHITE, 1);
    sm_render_text(fb, 12, bot_y + 13, "L/R: DIFFICULTY   SELECT: FOLDER", th->header_accent, 1);
}

/* ---------------------------- result ------------------------------ */

static const char *grade_name(int g)
{
    static const char *names[7] = { "AAAA", "AAA", "AA", "A", "B", "C", "D" };
    return (g >= 0 && g < 7) ? names[g] : "E";
}

/* ---------------------------- graphical grade ---------------------- */

static void draw_graphic_grade(uint16_t *fb, int x, int y, int grade)
{
    /* Background badge */
    sm_render_rect(fb, x, y, 104, 76, COLOR_RGB565(12, 6, 16));
    sm_render_rect_outline(fb, x, y, 104, 76, COLOR_RGB565(70, 50, 90));
    sm_render_rect_outline(fb, x + 2, y + 2, 100, 72, COLOR_RGB565(30, 20, 40));

    sm_render_text(fb, x + 34, y + 6, "GRADE", COLOR_RGB565(180, 180, 180), 1);

    uint16_t primary_col = COLOR_WHITE;
    uint16_t shadow_col  = COLOR_RGB565(60, 40, 0);

    switch (grade) {
    case 0: /* AAAA - Marvelous Gold */
        primary_col = COLOR_RGB565(255, 240, 80);
        shadow_col  = COLOR_RGB565(180, 120, 0);
        break;
    case 1: /* AAA - Bright Gold */
        primary_col = COLOR_RGB565(255, 215, 0);
        shadow_col  = COLOR_RGB565(160, 100, 0);
        break;
    case 2: /* AA - Emerald Green */
        primary_col = COLOR_RGB565(80, 255, 120);
        shadow_col  = COLOR_RGB565(0, 100, 40);
        break;
    case 3: /* A - Cyber Cyan */
        primary_col = COLOR_RGB565(80, 220, 255);
        shadow_col  = COLOR_RGB565(0, 80, 120);
        break;
    case 4: /* B - Neon Purple / Violet */
        primary_col = COLOR_RGB565(210, 120, 255);
        shadow_col  = COLOR_RGB565(90, 20, 130);
        break;
    case 5: /* C - Amber / Orange */
        primary_col = COLOR_RGB565(255, 160, 40);
        shadow_col  = COLOR_RGB565(120, 50, 0);
        break;
    case 6: /* D - Crimson Red */
        primary_col = COLOR_RGB565(255, 70, 70);
        shadow_col  = COLOR_RGB565(120, 10, 10);
        break;
    default: /* E - Failed / Grey */
        primary_col = COLOR_RGB565(160, 160, 160);
        shadow_col  = COLOR_RGB565(60, 60, 60);
        break;
    }

    const char *str = grade_name(grade);
    int len = (int)strlen(str);

    /* Render styled 3D / embossed letters */
    if (len == 4) { /* AAAA */
        int text_x = x + 12;
        int text_y = y + 26;
        sm_render_text(fb, text_x + 2, text_y + 2, str, shadow_col, 2);
        sm_render_text(fb, text_x,     text_y,     str, primary_col, 2);
        sm_render_rect(fb, x + 8, y + 64, 88, 2, primary_col);
    } else if (len == 3) { /* AAA */
        int text_x = x + 16;
        int text_y = y + 24;
        sm_render_text(fb, text_x + 2, text_y + 2, str, shadow_col, 3);
        sm_render_text(fb, text_x,     text_y,     str, primary_col, 3);
        sm_render_rect(fb, x + 12, y + 64, 80, 2, primary_col);
    } else if (len == 2) { /* AA */
        int text_x = x + 28;
        int text_y = y + 22;
        sm_render_text(fb, text_x + 2, text_y + 2, str, shadow_col, 3);
        sm_render_text(fb, text_x,     text_y,     str, primary_col, 3);
        sm_render_rect(fb, x + 20, y + 64, 64, 2, primary_col);
    } else { /* Single letter: A, B, C, D, E */
        int text_x = x + 38;
        int text_y = y + 20;
        sm_render_text(fb, text_x + 3, text_y + 3, str, shadow_col, 4);
        sm_render_text(fb, text_x,     text_y,     str, primary_col, 4);
        sm_render_rect(fb, x + 28, y + 64, 48, 2, primary_col);
    }
}

static void draw_result(uint16_t *fb)
{
    const sm_theme_t *th = sm_theme_current();

    if (sm_bg_has_image()) {
        sm_bg_render_fullscreen(fb, 25);
    } else {
        draw_gradient(fb, COLOR_RGB565(24, 0, 24), COLOR_RGB565(4, 0, 8));
    }

    sm_render_rect(fb, 12, 10, 296, 220, th->panel_bg);
    sm_render_rect_outline(fb, 12, 10, 296, 220, th->header_accent);

    sm_render_text(fb, 96, 16, "EVALUATION", th->header_accent, 2);

    char buf[48];
    sm_render_text(fb, 24, 38, sm_current_song_name(), COLOR_WHITE, 1);

    /* Left side: judgments */
    int ty = 56;
    for (int t = TNS_MARVELOUS; t >= TNS_MISS; t--) {
        if (g_game.tns[t] == 0 && t != TNS_MARVELOUS && t != TNS_PERFECT) continue;
        snprintf(buf, sizeof(buf), "%-9s %4d", sm_judge_name[t], g_game.tns[t]);
        sm_render_text(fb, 24, ty, buf, COLOR_GREY, 1);
        ty += 12;
    }

    /* Right side: Graphical StepMania Grade badge */
    int grade = sm_calc_grade();
    draw_graphic_grade(fb, 194, 52, grade);

    /* Stats below */
    snprintf(buf, sizeof(buf), "SCORE: %09d", g_game.score);
    sm_render_text(fb, 24, 140, buf, COLOR_WHITE, 1);

    snprintf(buf, sizeof(buf), "ACCURACY: %.2f%%", (double)sm_calc_percent());
    sm_render_text(fb, 24, 154, buf, COLOR_CYAN, 1);

    snprintf(buf, sizeof(buf), "MAX COMBO: %d", g_game.max_combo);
    sm_render_text(fb, 24, 168, buf, th->header_accent, 1);

    sm_render_text(fb, 64, 204, "PRESS START TO CONTINUE", COLOR_WHITE, 1);
}

/* ---------------------------- loading ----------------------------- */

static void draw_loading(uint16_t *fb, int frame_step)
{
    draw_gradient(fb, COLOR_RGB565(20, 10, 35), COLOR_RGB565(8, 4, 12));

    int cx = SM_SCREEN_WIDTH / 2;
    int cy = SM_SCREEN_HEIGHT / 2;

    sm_render_logo(fb, cx - 44, cy - 40);

    sm_render_text(fb, cx - 48, cy - 10, "STEPMANIA 3.9", COLOR_WHITE, 1);
    sm_render_text(fb, cx - 36, cy + 6, "LOADING...", COLOR_CYAN, 1);

    int bar_w = 200;
    int bar_h = 8;
    int bar_x = (SM_SCREEN_WIDTH - bar_w) / 2;
    int bar_y = cy + 24;

    sm_render_rect(fb, bar_x, bar_y, bar_w, bar_h, COLOR_RGB565(30, 20, 40));
    sm_render_rect_outline(fb, bar_x - 1, bar_y - 1, bar_w + 2, bar_h + 2, COLOR_RGB565(80, 60, 120));

    int progress = (frame_step * bar_w) / 105;
    if (progress > bar_w) progress = bar_w;
    if (progress > 0) {
        sm_render_rect(fb, bar_x, bar_y, progress, bar_h, COLOR_RGB565(60, 180, 255));
    }
}

/* ---------------------------- dispatch ---------------------------- */

void sm_render_frame(uint16_t *fb)
{
    if (!fb) return;

    s_tipsy_phase++;

    switch (g_game.screen) {
    case SCREEN_BOOT:
        draw_loading(fb, g_game.boot_frames);
        break;
    case SCREEN_SELECT:
        draw_select(fb);
        break;
    case SCREEN_PLAY:
        draw_play(fb);
        break;
    case SCREEN_RESULT:
        draw_result(fb);
        break;
    default:
        break;
    }
}