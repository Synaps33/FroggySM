/*
 * Headless libretro front end used to validate the core on a workstation.
 *
 * It plays the real demo library out of FroggySM/Songs: scans it, plays a
 * chart with an automated input pattern, and asserts the things that actually
 * decide whether the port is correct -- that charts decode, that music streams,
 * that judgments land in the right windows, that scoring adds up, and that the
 * frame and audio streams stay well formed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "libretro.h"
#include "sm_game.h"
#include "sm_song.h"
#include "sm_library.h"
#include "sm_render.h"
#include "sm_audio.h"
#include "sm_bg.h"
#include "sm_theme.h"

/* sm_song_parse() takes ownership of the text buffer and strips comments in
 * place, so a synthetic chart has to be handed over as a writable malloc'd
 * copy rather than a string literal. */
static char *chart_dup(const char *src)
{
    size_t n = strlen(src);
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, src, n + 1);
    return p;
}

static uint16_t s_fb[320 * 240];
static int      s_frames;
static uint64_t s_audio_samples;
static int64_t  s_audio_peak;
static int64_t  s_audio_energy;
static int      s_pixel_format_ok = 1;

static uint16_t s_buttons;      /* simulated pad state */

#define BTN_LEFT   (1u << 0)
#define BTN_DOWN   (1u << 1)
#define BTN_UP     (1u << 2)
#define BTN_RIGHT  (1u << 3)
#define BTN_SELECT (1u << 4)
#define BTN_START  (1u << 5)
#define BTN_L      (1u << 6)
#define BTN_R      (1u << 7)

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data) return;
    const uint8_t *src = (const uint8_t *)data;
    for (unsigned y = 0; y < h && y < 240; y++)
        memcpy(&s_fb[y * 320], src + y * pitch, (w < 320 ? w : 320) * 2);
    s_frames++;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
    if (!data) return 0;
    s_audio_samples += frames * 2;
    for (size_t i = 0; i < frames * 2; i++) {
        int v = data[i] < 0 ? -data[i] : data[i];
        if (v > s_audio_peak) s_audio_peak = v;
        s_audio_energy += v;
    }
    return frames;
}

static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static void poll_cb(void) {}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)port; (void)index;
    if (device != RETRO_DEVICE_JOYPAD) return 0;
    switch (id) {
        case RETRO_DEVICE_ID_JOYPAD_LEFT:   return (s_buttons & BTN_LEFT)  ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_DOWN:   return (s_buttons & BTN_DOWN)  ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_UP:     return (s_buttons & BTN_UP)    ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return (s_buttons & BTN_RIGHT) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_SELECT: return (s_buttons & BTN_SELECT)? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_START:  return (s_buttons & BTN_START) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_L:      return (s_buttons & BTN_L)     ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_R:      return (s_buttons & BTN_R)     ? 1 : 0;
        default: return 0;
    }
}

static bool environ_cb(unsigned cmd, void *data)
{
    if (cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) {
        enum retro_pixel_format f = *(enum retro_pixel_format *)data;
        if (f != RETRO_PIXEL_FORMAT_RGB565) s_pixel_format_ok = 0;
    }
    return true;
}

static void save_ppm(const char *name, const uint16_t *fb)
{
    FILE *f = fopen(name, "wb");
    if (!f) { fprintf(stderr, "  [WARN] cannot write %s\n", name); return; }
    fprintf(f, "P6\n320 240\n255\n");
    for (int i = 0; i < 320 * 240; i++) {
        uint16_t p = fb[i];
        fputc(((p >> 11) & 0x1F) * 255 / 31, f);
        fputc(((p >> 5) & 0x3F) * 255 / 63, f);
        fputc((p & 0x1F) * 255 / 31, f);
    }
    fclose(f);
    printf("  saved %s\n", name);
}

static int g_fail;
static void check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) g_fail++;
}

static void run_frames(int n)
{
    for (int i = 0; i < n; i++) retro_run();
}

int main(void)
{
    printf("=====================================================\n");
    printf("  FroggySM GB300 / SF2000 HEADLESS TEST RUNNER      \n");
    printf("  StepMania 3.9 port (mafu9/PSPMania) -> MIPS32 BREW  \n");
    printf("=====================================================\n\n");

    retro_set_environment(environ_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(poll_cb);
    retro_set_input_state(input_state_cb);

    printf("[1] Core init\n");
    retro_init();
    check(s_pixel_format_ok, "RGB565 pixel format accepted");

    struct retro_game_info gi = { "FroggySM", NULL, 0, NULL };
    printf("[2] Loading game and scanning the song library\n");
    retro_load_game(&gi);
    run_frames(2);

    printf("\n[3] Song library (%d songs found)\n", g_library_count);
    check(g_library_count > 0, "at least one .sm chart was discovered");
    for (int i = 0; i < g_library_count; i++) {
        sm_lib_song_t *s = sm_library_get(i);
        printf("      [%d] %-26s %-20s steps=%d music=%s\n", i,
               s->song.title, s->song.artist, s->song.num_diffs,
               s->song.has_music ? "yes" : "NO");
    }
    if (g_library_count == 0) {
        printf("\n  no songs on this machine; set SM_SONGS_PATH to the\n");
        printf("  FroggySM Songs folder. Stopping after static checks.\n");
        return 1;
    }

    printf("\n[4] Chart decode across every difficulty\n");
    {
        int total_diffs = 0, total_notes = 0, failed = 0;
        for (int i = 0; i < g_library_count; i++) {
            sm_lib_song_t *s = sm_library_get(i);
            for (int d = 0; d < s->song.num_diffs; d++) {
                sm_steps_t st;
                if (!sm_song_load_steps(&s->song, d, &st)) { failed++; continue; }
                int notes = 0;
                for (int r = 0; r < st.num_rows; r++)
                    for (int c = 0; c < st.num_tracks; c++)
                        if (st.notes[r * st.num_tracks + c].type != NT_EMPTY) notes++;
                notes += st.num_holds;
                if (notes <= 0) failed++;
                total_notes += notes;
                total_diffs++;
                sm_steps_free(&st);
            }
        }
        printf("      %d difficulties, %d notes total\n", total_diffs, total_notes);
        check(failed == 0, "every difficulty decoded with notes");
        check(total_diffs > 0, "difficulties are present");
    }

    /*
     * Hold notes get their own synthetic chart.  The demo library is full of
     * charts without holds, so a regression in Convert2sAnd3sToHoldNotes --
     * which used to run the terminator search straight past the '3' and eat
     * the next real note -- would otherwise go unnoticed.  The chart puts a tap
     * immediately after a hold tail in the same column, so a parser that grabs
     * "the next note" instead of "the next 3" loses the tap and stretches the
     * hold onto it, and both failures are visible below.
     */
    printf("\n[4b] Hold note parsing\n");
    {
        static const char hold_chart[] =
            "#TITLE:HoldProbe;\r\n"
            "#ARTIST:Nobody;\r\n"
            "#BPMS:120;\r\n"
            "#NOTES:\r\n"
            "     dance-single:\r\n"
            "     Tester:\r\n"
            "     Beginner:\r\n"
            "     1:\r\n"
            "     1.000,1.000,1.000,1.000,1.000,1.000,1.000,1.000,1.000,1.000:\r\n"
            "0000\r\n0000\r\n0000\r\n0000\r\n,\r\n"   /* rows   0.. 95 */
            "0000\r\n0000\r\n0020\r\n0000\r\n,\r\n"   /* hold head  row 144 */
            "0000\r\n0000\r\n0000\r\n0030\r\n,\r\n"   /* hold tail  row 264 */
            "0000\r\n0010\r\n0000\r\n0000\r\n:\r\n"   /* tap after   row 312 */
            "     ;\r\n";

        char *htext = chart_dup(hold_chart);
        sm_song_t probe;
        check(htext != NULL && sm_song_parse(htext, strlen(htext), &probe) > 0,
              "synthetic hold chart parses");
        check(probe.num_diffs == 1, "hold chart exposes one difficulty");

        sm_steps_t st;
        check(sm_song_load_steps(&probe, 0, &st), "hold chart decodes to a grid");
        check(st.num_holds == 1, "the 2/3 pair becomes exactly one hold");

        if (st.num_holds == 1) {
            printf("      hold[0] rows %d -> %d (length %d)\n",
                   st.holds[0].start_row, st.holds[0].end_row,
                   st.holds[0].end_row - st.holds[0].start_row);
            check(st.holds[0].end_row > st.holds[0].start_row,
                  "hold tail sits below the head");
            /* Must stop at the '3', not run on to the tap on the next row. */
            check(st.holds[0].end_row < 312,
                  "hold ends at its tail marker instead of swallowing the next note");
        }

        /* The tap in the same column as the hold must survive. */
        int taps_col2 = 0;
        int heads_col2 = 0;
        for (int r = 0; r < st.num_rows; r++) {
            sm_note_t *n = &st.notes[r * st.num_tracks + 2];
            if (n->type == NT_TAP) taps_col2++;
            if (n->type == NT_HOLD_HEAD) heads_col2++;
        }
        printf("      lane 3 (index 2): %d hold head, %d tap after the tail\n",
               heads_col2, taps_col2);
        check(heads_col2 == 1, "the hold head survived parsing");
        check(taps_col2 == 1, "the note after a hold tail is not deleted");

        /* No marker may leak into the scored grid. */
        int stray_tails = 0;
        for (int i = 0; i < st.num_rows * st.num_tracks; i++)
            if (st.notes[i].type == NT_HOLD_TAIL) stray_tails++;
        check(stray_tails == 0, "hold tail markers are consumed, not left in the grid");

        /* An unterminated head must not swallow the rest of the column. */
        static const char bad_chart[] =
            "#TITLE:BadHold;\r\n"
            "#ARTIST:Nobody;\r\n"
            "#BPMS:120;\r\n"
            "#NOTES:\r\n"
            "     dance-single:\r\n"
            "     Tester:\r\n"
            "     Beginner:\r\n"
            "     1:\r\n"
            "     1.000,1.000,1.000,1.000,1.000,1.000,1.000,1.000,1.000,1.000:\r\n"
            "0000\r\n0000\r\n0000\r\n0000\r\n,\r\n"
            "0000\r\n0020\r\n0000\r\n0000\r\n,\r\n"   /* head with no '3' */
            "0000\r\n0010\r\n0000\r\n0000\r\n:\r\n"
            "     ;\r\n";
        char *btext = chart_dup(bad_chart);
        sm_song_t bad;
        if (btext && sm_song_parse(btext, strlen(btext), &bad) > 0) {
            sm_steps_t bst;
            if (sm_song_load_steps(&bad, 0, &bst)) {
                int bad_taps = 0;
                for (int r = 0; r < bst.num_rows; r++)
                    if (bst.notes[r * bst.num_tracks + 2].type == NT_TAP) bad_taps++;
                check(bst.num_holds == 0, "an unterminated hold head is dropped");
                check(bad_taps == 1, "an unterminated hold does not eat the next note");
                sm_steps_free(&bst);
            }
            sm_song_free(&bad);
        }

        sm_steps_free(&st);
        sm_song_free(&probe);
    }

    printf("\n[5] Boot screen (HackerLog CRT)\n");
    g_game.screen = SCREEN_BOOT;
    g_game.boot_frames = 0;
    run_frames(40);
    save_ppm("/home/Sajnaps/pspmania_boot_frame.ppm", s_fb);

    printf("\n[6] Song select\n");
    run_frames(90);                 /* finish the boot sequence */
    check(g_game.screen == SCREEN_SELECT, "boot hands over to song select");
    save_ppm("/home/Sajnaps/pspmania_select_frame.ppm", s_fb);

    /* Test official StepMania theme */
    printf("      themes loaded: %d\n", sm_theme_count());
    check(sm_theme_count() == 1, "only official StepMania theme active");
    const sm_theme_t *th = sm_theme_current();
    check(strstr(th->name, "StepMania") != NULL, "official StepMania theme selected");

    /* Check step counting and banner resolution */
    sm_lib_song_t *cur_s = sm_library_get(g_game.song_idx);
    check(cur_s != NULL && cur_s->song.num_diffs > 0, "song has difficulties");
    check(cur_s->song.diffs[0].num_steps > 0, "difficulty has steps counted");
    printf("      song 0 diff 0 steps: %d\n", cur_s->song.diffs[0].num_steps);

    printf("\n[7] Starting a song\n");
    s_buttons = BTN_START;
    run_frames(2);
    s_buttons = 0;
    run_frames(2);
    check(g_game.screen == SCREEN_PLAY, "START enters gameplay");
    check(sm_active_steps != NULL, "a difficulty grid is resident");
    extern int sm_bg_has_image(void);
    check(sm_bg_has_image(), "song background image loaded successfully");

    printf("\n[8] Music streaming\n");
    run_frames(90);                 /* 1.5s of audio */
    {
        extern int sm_audio_music_playing(void);
        extern float sm_audio_music_seconds(void);
        printf("      music %s at %.3fs\n",
               sm_audio_music_playing() ? "playing" : "stopped",
               (double)sm_audio_music_seconds());
        check(s_audio_peak > 2000, "decoded music has real amplitude");
        check(sm_audio_music_seconds() > 1.0f, "song clock advanced past 1s");
        check(g_game.song_beat > 0.0f, "song clock maps onto beats");
    }

    printf("\n[9] Gameplay frames\n");
    run_frames(240);
    save_ppm("/home/Sajnaps/pspmania_game_frame.ppm", s_fb);
    check(g_game.frame_count > 300, "frame counter advanced");

    printf("\n[9b] Pause / Resume test\n");
    s_buttons = BTN_START;
    run_frames(2);
    s_buttons = 0;
    run_frames(10);
    check(g_game.paused == 1, "START pauses the game");
    extern int sm_audio_is_paused(void);
    check(sm_audio_is_paused() == 1, "music audio stream paused");
    float paused_sec = sm_audio_music_seconds();
    run_frames(30);
    check(sm_audio_music_seconds() == paused_sec, "music clock halted during pause");

    /* Resume */
    s_buttons = BTN_START;
    run_frames(2);
    s_buttons = 0;
    run_frames(10);
    check(g_game.paused == 0, "START resumes gameplay");
    check(sm_audio_is_paused() == 0, "music audio stream resumed");

    printf("\n[10] Clean gameplay render\n");
    run_frames(6);
    save_ppm("/home/Sajnaps/pspmania_play_frame.ppm", s_fb);
    check(g_game.screen == SCREEN_PLAY, "gameplay renders cleanly without hackerlog");

    printf("\n[11] Judgment accuracy (auto-play the chart)\n");
    {
        /* Re-derive the pad state straight from the chart: press every lane
         * whose nearest unjudged note is about to cross the judgment line.
         * Pressing all lanes matters -- a jump (two notes in one row) only
         * scores both if both are hit, and a missed note resets the combo. */
        sm_steps_t *st = sm_active_steps;
        sm_lib_song_t *lib = sm_library_get(g_game.song_idx);
        int hits_before = g_game.tns[TNS_MARVELOUS] + g_game.tns[TNS_PERFECT] +
                          g_game.tns[TNS_GREAT] + g_game.tns[TNS_GOOD] +
                          g_game.tns[TNS_BOO] + g_game.tns[TNS_MISS];

        for (int f = 0; f < 3600 && g_game.screen == SCREEN_PLAY; f++) {
            s_buttons = 0;
            if (st && lib) {
                float now = g_game.music_seconds;
                for (int lane = 0; lane < 4; lane++) {
                    /* Keep the lane held for as long as a hold is running in
                     * it, otherwise every hold in the chart is released on the
                     * frame after its head and banks an NG. */
                    if (g_game.active_hold[lane] >= 0) {
                        s_buttons |= (1u << lane);
                        continue;
                    }
                    /* nearest unjudged note in this lane */
                    int best_row = -1;
                    float best_dt = 1e9f;
                    for (int r = 0; r < st->num_rows; r++) {
                        sm_note_t *n = &st->notes[r * st->num_tracks + lane];
                        if (n->type == NT_EMPTY || n->score != TNS_NONE) continue;
                        float t = sm_time_from_beat(&lib->song.timing, sm_row_to_beat(r));
                        float dt = t - now;
                        if (dt < -0.05f) continue;   /* already past */
                        if (dt < best_dt) { best_dt = dt; best_row = r; }
                    }
                    if (best_row >= 0 && best_dt < 0.030f)
                        s_buttons |= (1u << lane);
                }
            }
            retro_run();
        }
        int hits_after = g_game.tns[TNS_MARVELOUS] + g_game.tns[TNS_PERFECT] +
                         g_game.tns[TNS_GREAT] + g_game.tns[TNS_GOOD] +
                         g_game.tns[TNS_BOO] + g_game.tns[TNS_MISS];

        printf("      judged rows=%d  notes=%d  taps_hit=%d/%d\n",
               g_game.rows_judged,
               hits_after, g_game.taps_hit, g_game.num_taps_and_holds);
        printf("      MARV=%d PERF=%d GREAT=%d GOOD=%d BOO=%d MISS=%d\n",
               g_game.tns[TNS_MARVELOUS], g_game.tns[TNS_PERFECT],
               g_game.tns[TNS_GREAT], g_game.tns[TNS_GOOD],
               g_game.tns[TNS_BOO], g_game.tns[TNS_MISS]);
        printf("      holds in chart=%d  HNS_OK=%d HNS_NG=%d\n",
               st ? st->num_holds : 0, g_game.hns[HNS_OK], g_game.hns[HNS_NG]);
        printf("      score=%d / %d  combo=%d max=%d  acc=%.2f%%\n",
               g_game.score, g_game.max_possible_points, g_game.combo,
               g_game.max_combo, (double)sm_calc_percent());
        save_ppm("/home/Sajnaps/pspmania_auto_frame.ppm", s_fb);

        check(hits_after > hits_before, "auto-play produced judgments");
        check(g_game.tns[TNS_MARVELOUS] + g_game.tns[TNS_PERFECT] > 0,
              "auto-play lands inside the Perfect/Marvelous windows");
        check(g_game.score > 0, "score accumulated");
        check(g_game.combo > 0, "combo counted up");
        /* ComboIsPerRow is false for ScoreKeeperMAX2 outside Oni mode, so a row is
         * worth every tap it contained: a jump adds 2.  Combo therefore counts
         * taps in successful rows, not rows, and a completed hold tail adds a
         * note score without adding combo. */
        check(g_game.max_combo <= g_game.taps_hit,
              "max combo never exceeds the number of notes hit");
        check(g_game.taps_hit <= g_game.num_taps_and_holds,
              "taps hit never exceeds the chart's note count");
        check(g_game.score <= g_game.max_possible_points,
              "score never exceeds the maximum possible score");

        if (st && st->num_holds > 0) {
            int banked = g_game.hns[HNS_OK] + g_game.hns[HNS_NG];
            check(g_game.hns[HNS_OK] > 0,
                  "auto-play completes holds to HNS_OK while holding the lane");
            check(banked <= st->num_holds,
                  "each hold is banked at most once");
            check(g_game.hns[HNS_OK] + g_game.hns[HNS_NG] ==
                  banked, "hold tally is internally consistent");
        }
    }

    printf("\n[12] Results screen\n");
    g_game.song_ended = 1;
    g_game.screen = SCREEN_RESULT;
    run_frames(4);
    save_ppm("/home/Sajnaps/pspmania_result_frame.ppm", s_fb);
    check(g_game.screen == SCREEN_RESULT, "results screen renders");

    printf("\n[13] Audio/video stream integrity\n");
    printf("      frames=%d  samples=%llu  peak=%lld  energy=%lld\n",
           s_frames, (unsigned long long)s_audio_samples,
           (long long)s_audio_peak, (long long)s_audio_energy);
    check(s_frames > 300, "video frames delivered");
    check(s_audio_samples > 0, "audio samples delivered");
    check(s_audio_peak <= 32767, "no audio clipping past full scale");

    printf("\n[14] Teardown\n");
    retro_unload_game();
    retro_deinit();

    printf("\n=====================================================\n");
    if (g_fail == 0)
        printf("  ALL CHECKS PASSED\n");
    else
        printf("  %d CHECK(S) FAILED\n", g_fail);
    printf("=====================================================\n");
    return g_fail ? 1 : 0;
}