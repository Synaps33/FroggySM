#include "libretro.h"
#include "sm_types.h"
#include "sm_game.h"
#include "sm_song.h"
#include "sm_library.h"
#include "sm_render.h"
#include "sm_audio.h"
#include <string.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------------
 * libretro front end for the GB300 / SF2000 core.
 *
 * The song clock lives entirely in the audio subsystem: sm_audio_render emits
 * exactly 735 stereo frames per run() and advances the song position once per
 * emitted frame, so the chart and the music cannot drift apart no matter how
 * long the frame took to draw.
 * ------------------------------------------------------------------------- */

static retro_video_refresh_t       s_video_cb;
static retro_audio_sample_batch_t  s_audio_batch_cb;
static retro_environment_t         s_environ_cb;
static retro_input_poll_t          s_input_poll_cb;
static retro_input_state_t         s_input_state_cb;

static uint16_t s_framebuffer[SM_SCREEN_WIDTH * SM_SCREEN_HEIGHT];
static int16_t  s_audio_buffer[SM_AUDIO_FRAMES * 2];

static int s_started;

void retro_set_environment(retro_environment_t cb)
{
    s_environ_cb = cb;
    if (s_environ_cb) {
        enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
        s_environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
    }
}

void retro_set_video_refresh(retro_video_refresh_t cb) { s_video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { s_audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { s_input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { s_input_state_cb = cb; }

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name     = "FroggySM";
    info->library_version  = "1.0";
    info->need_fullpath    = true;
    info->valid_extensions = "sm|gba|bin|*";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof(*info));
    info->geometry.base_width   = SM_SCREEN_WIDTH;
    info->geometry.base_height  = SM_SCREEN_HEIGHT;
    info->geometry.max_width    = SM_SCREEN_WIDTH;
    info->geometry.max_height   = SM_SCREEN_HEIGHT;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps            = 60.0;
    info->timing.sample_rate    = SM_AUDIO_SAMPLE_RATE;
}

/* Let a host override the songs folder, mainly for the headless runner:
 * SM_SONGS_PATH=/some/dir ./test_runner */
static const char *songs_root_override(void)
{
    const char *env = getenv("SM_SONGS_PATH");
    if (env && env[0]) return env;
    return NULL;
}

static void scan_songs(void)
{
    const char *root = songs_root_override();
    if (root) {
        sm_library_scan(root);
        return;
    }
    sm_library_scan(NULL);
}

void retro_init(void)
{
    sm_render_init();
    sm_audio_init();
    sm_game_init();
    s_started = 0;
}

void retro_deinit(void)
{
    sm_audio_shutdown();
    sm_library_free();
}

bool retro_load_game(const struct retro_game_info *info)
{
    (void)info;
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
    if (s_environ_cb)
        s_environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

    scan_songs();

    sm_game_init();
    s_started = 1;
    return true;
}

void retro_unload_game(void)
{
    sm_audio_stop_music();
    sm_library_free();
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t n)
{
    (void)type; (void)info; (void)n;
    return false;
}

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

void retro_reset(void)
{
    sm_audio_stop_music();
    sm_game_reset();
}

void retro_run(void)
{
    if (!s_started) return;

    if (s_input_poll_cb) s_input_poll_cb();

    uint16_t joy = 0;
    if (s_input_state_cb) {
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))   joy |= (1 << 0);
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))   joy |= (1 << 1);
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))     joy |= (1 << 2);
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))  joy |= (1 << 3);
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT)) joy |= (1 << 4);
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))  joy |= (1 << 5);
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L))      joy |= (1 << 6);
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R))      joy |= (1 << 7);
    }
    g_game.joypad = joy;

    /*
     * Lane order is dance-single's Left/Down/Up/Right
     * (GameManager.cpp:1232-1235).  The face buttons mirror the D-pad so the
     * game is playable on the GB300's smaller pad as well.
     */
    uint8_t prev[SM_NUM_LANES];
    for (int i = 0; i < SM_NUM_LANES; i++) prev[i] = sm_btn[i];

    if (s_input_state_cb) {
        sm_btn[0] = (joy & (1 << 0)) ||
                    s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
        sm_btn[1] = (joy & (1 << 1)) ||
                    s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
        sm_btn[2] = (joy & (1 << 2)) ||
                    s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
        sm_btn[3] = (joy & (1 << 3)) ||
                    s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
        sm_btn_a_pressed = s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) != 0;
        sm_btn_b_pressed = s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) != 0;
    }

    sm_start_pressed = (joy & (1 << 5)) != 0;
    sm_sel_pressed   = (joy & (1 << 4)) != 0;
    sm_btn_l_pressed = (joy & (1 << 6)) != 0;
    sm_btn_r_pressed = (joy & (1 << 7)) != 0;

    for (int i = 0; i < SM_NUM_LANES; i++)
        g_game.btn_prev[i] = prev[i];

    sm_game_update();

    sm_render_frame(s_framebuffer);
    if (s_video_cb)
        s_video_cb(s_framebuffer, SM_SCREEN_WIDTH, SM_SCREEN_HEIGHT,
                   SM_SCREEN_WIDTH * sizeof(uint16_t));

    int audio_frames = (g_game.frame_count & 1) ? 368 : 367;
    sm_audio_render(s_audio_buffer, audio_frames);
    if (s_audio_batch_cb)
        s_audio_batch_cb(s_audio_buffer, audio_frames);
}

/* The playfield is rebuilt from the chart and the audio clock every frame, so
 * a snapshot is the stats block plus the song pointers it refers to. */
size_t retro_serialize_size(void) { return sizeof(sm_game_t); }

bool retro_serialize(void *data, size_t size)
{
    if (size < sizeof(sm_game_t)) return false;
    memcpy(data, &g_game, sizeof(sm_game_t));
    return true;
}

bool retro_unserialize(const void *data, size_t size)
{
    if (size < sizeof(sm_game_t)) return false;
    memcpy(&g_game, data, sizeof(sm_game_t));
    return true;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    (void)index; (void)enabled; (void)code;
}
void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port; (void)device;
}