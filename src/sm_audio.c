/* minimp3 is public domain (CC0).  Single translation unit: declare
 * MINIMP3_IMPLEMENTATION once, here, so only sm_audio.c emits the decoder. */
#define MINIMP3_IMPLEMENTATION
#include "mp3_mini.h"
#include "sm_audio.h"
#include "sm_game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------------
 * Audio subsystem
 *
 * Music:  MPEG-1/2 Layer III frames are pulled straight off the SD card and
 *         decoded by the CC0 minimp3 core in mp3_mini.h.  PSPMania used the
 *         PSP's MediaEngine hardware codec instead, so no software decoder
 *         existed to inherit.
 *
 * Timing: the song clock is the count of samples actually handed to the
 *         frontend, exactly like the original's sound-engine sample position
 *         (ScreenGameplay.cpp:1173-1182).  Nothing in the judgment path ever
 *         consults a wall clock.
 *
 * Sounds: hit sounds are pre-decoded mono PCM; they are mixed in at render
 *         time so they stay sample-locked to the beat.
 * ------------------------------------------------------------------------- */

typedef struct {
    int16_t *pcm;          /* mono, SM_AUDIO_SAMPLE_RATE */
    int      num_samples;
    int      pos;
} sm_sound_t;

#define SM_AUDIO_RING_SIZE   32768

typedef struct {
    /* music */
    mp3dec_t         dec;
    int16_t          ring[SM_AUDIO_RING_SIZE * 2];
    int              ring_read;
    int              ring_write;
    int              ring_count;
    uint32_t         resample_acc;    /* 16.16 fixed-point phase */
    int              music_channels;
    int              music_hz;
    float            music_rate;
    uint32_t         samples_played;  /* in output units (SM_AUDIO_SAMPLE_RATE) */
    int              music_playing;
    int              music_paused;
    int              music_eof;
    uint64_t         bytes_read;
    int              frames_decoded;
    int              decode_load;

    /* hit sounds */
    sm_sound_t       hit[SM_HITSOUND_SLOTS];
    int              hit_next;
    int16_t          menu[SM_MENU_MAX_SAMPLES];
    int              menu_len;
    int              menu_pos;

    int              sfx_volume;      /* 0..256 */
    int              music_volume;    /* 0..256 */
    int              initialised;
} sm_audio_t;

static sm_audio_t g_audio;

#define SM_MP3_MAX_RESYNC   262144

static int s_timing_start = 0;

static uint8_t *s_mp3_data = NULL;
static size_t   s_mp3_size = 0;
static size_t   s_mp3_pos  = 0;

int sm_audio_file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* Slurp a whole file.  Used for the small WAV hit sounds only. */
static uint8_t *read_whole_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    *out_size = got;
    return buf;
}

/* ------------------------------- WAV -------------------------------- */
/*
 * PSPMania's own ReadMe recommends WAV for sound effects precisely because it
 * needs no decoding; Themes/default/Sounds/_common holds the WAVs the demo ships.
 */

static int decode_wav_mono(const uint8_t *data, size_t size,
                           int16_t **out_pcm, int *out_frames)
{
    if (size < 44) return 0;

    /* RIFF....WAVE */
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0)
        return 0;

    int fmt_ok = 0, data_ok = 0;
    int channels = 1, bits = 16, rate = SM_AUDIO_SAMPLE_RATE;
    size_t pos = 12;
    size_t data_off = 0, data_len = 0;

    while (pos + 8 <= size) {
        const uint8_t *id = data + pos;
        uint32_t clen = (uint32_t)data[pos + 4] | ((uint32_t)data[pos + 5] << 8) |
                        ((uint32_t)data[pos + 6] << 16) | ((uint32_t)data[pos + 7] << 24);
        size_t body = pos + 8;

        if (memcmp(id, "fmt ", 4) == 0 && body + 16 <= size) {
            uint32_t fmt = (uint32_t)data[body] | ((uint32_t)data[body + 1] << 8) |
                           ((uint32_t)data[body + 2] << 16) | ((uint32_t)data[body + 3] << 24);
            channels   = data[body + 2] | (data[body + 3] << 8);
            rate       = data[body + 4] | (data[body + 5] << 8) |
                         (data[body + 6] << 16) | (data[body + 7] << 24);
            bits       = data[body + 14] | (data[body + 15] << 8);
            if (fmt == 0xFFFE && body + 26 <= size)
                channels = data[body + 24] | (data[body + 25] << 8);
            fmt_ok = 1;
        } else if (memcmp(id, "data", 4) == 0) {
            data_off = body;
            data_len = clen;
            if (data_off + data_len > size) data_len = size - data_off;
            data_ok = 1;
            break;
        }
        pos = body + clen + (clen & 1);       /* chunks are word aligned */
    }

    if (!fmt_ok || !data_ok || channels < 1 || channels > 2) return 0;
    if (bits != 8 && bits != 16) return 0;

    int bytes_per_sample = bits / 8;
    int frame_bytes = bytes_per_sample * channels;
    int frames = (int)(data_len / (size_t)frame_bytes);
    if (frames <= 0) return 0;

    int16_t *pcm = (int16_t *)malloc(sizeof(int16_t) * (size_t)frames);
    if (!pcm) return 0;

    const uint8_t *src = data + data_off;
    for (int i = 0; i < frames; i++) {
        int sum = 0;
        for (int c = 0; c < channels; c++) {
            const uint8_t *s = src + (size_t)i * frame_bytes + (size_t)c * bytes_per_sample;
            int v;
            if (bits == 16)
                v = (int16_t)((uint16_t)s[0] | ((uint16_t)s[1] << 8));
            else
                v = ((int)s[0] - 128) << 8;
            sum += v;
        }
        pcm[i] = (int16_t)(sum / channels);
    }

    *out_pcm   = pcm;
    *out_frames = frames;
    (void)rate;
    return 1;
}

void sm_audio_get_stats(sm_audio_stats_t *out)
{
    if (!out) return;
    out->music_playing   = g_audio.music_playing && (!g_audio.music_eof || g_audio.ring_count > 0);
    out->music_hz        = g_audio.music_hz;
    out->music_channels  = g_audio.music_channels;
    out->music_eof       = g_audio.music_eof;
    out->music_seconds   = sm_audio_music_seconds();
    out->bytes_read      = g_audio.bytes_read;
    out->decode_load     = g_audio.decode_load;
    out->frames_decoded  = g_audio.frames_decoded;
    int playing = 0;
    for (int i = 0; i < SM_HITSOUND_SLOTS; i++)
        if (g_audio.hit[i].pcm && g_audio.hit[i].pos < g_audio.hit[i].num_samples)
            playing++;
    out->sfx_playing = playing;
}

void sm_audio_set_volumes(int music_pct, int sfx_pct)
{
    if (music_pct < 0) music_pct = 0;
    if (music_pct > 256) music_pct = 256;
    if (sfx_pct < 0) sfx_pct = 0;
    if (sfx_pct > 256) sfx_pct = 256;
    g_audio.music_volume = music_pct;
    g_audio.sfx_volume   = sfx_pct;
}

/* --------------------------- public init --------------------------- */

void sm_audio_init(void)
{
    memset(&g_audio, 0, sizeof(g_audio));
    mp3dec_init(&g_audio.dec);
    g_audio.sfx_volume   = 200;
    g_audio.music_volume = 230;
    g_audio.music_rate   = 1.0f;
    g_audio.initialised  = 1;
    s_timing_start = 0;
}

void sm_audio_shutdown(void)
{
    sm_audio_stop_music();
    for (int i = 0; i < SM_HITSOUND_SLOTS; i++) {
        free(g_audio.hit[i].pcm);
        g_audio.hit[i].pcm = NULL;
    }
    g_audio.initialised = 0;
}

/* ------------------------------ music ------------------------------- */

static void feed_ring(int max_frames);

void sm_audio_stop_music(void)
{
    if (s_mp3_data) {
        free(s_mp3_data);
        s_mp3_data = NULL;
    }
    s_mp3_size = 0;
    s_mp3_pos  = 0;
    g_audio.music_playing = 0;
    g_audio.music_paused = 0;
    g_audio.music_eof = 0;
    g_audio.ring_read = 0;
    g_audio.ring_write = 0;
    g_audio.ring_count = 0;
    g_audio.resample_acc = 0;
    g_audio.samples_played = 0;
    g_audio.music_channels = 0;
    g_audio.bytes_read = 0;
    mp3dec_init(&g_audio.dec);
}

void sm_audio_pause_music(int pause)
{
    g_audio.music_paused = pause ? 1 : 0;
}

int sm_audio_is_paused(void)
{
    return g_audio.music_paused;
}

int sm_audio_open_music(const char *path)
{
    sm_audio_stop_music();

    s_mp3_data = read_whole_file(path, &s_mp3_size);
    if (!s_mp3_data || s_mp3_size == 0) {
        if (s_mp3_data) { free(s_mp3_data); s_mp3_data = NULL; }
        return 0;
    }

    s_mp3_pos = 0;
    mp3dec_init(&g_audio.dec);
    g_audio.ring_read = 0;
    g_audio.ring_write = 0;
    g_audio.ring_count = 0;
    g_audio.resample_acc = 0;
    g_audio.samples_played = 0;
    g_audio.bytes_read = s_mp3_size;
    g_audio.music_eof = 0;
    g_audio.music_playing = 1;

    /* Pre-buffer 16 frames (~18432 samples = ~0.42 sec) while opening */
    feed_ring(16);
    return 1;
}

int sm_audio_music_playing(void)
{
    return g_audio.music_playing && (!g_audio.music_eof || g_audio.ring_count > 0);
}

float sm_audio_music_seconds(void)
{
    return (float)g_audio.samples_played / (float)SM_AUDIO_SAMPLE_RATE;
}

void sm_audio_get_format(int *out_rate, int *out_channels)
{
    if (out_rate)     *out_rate     = g_audio.music_hz;
    if (out_channels) *out_channels = g_audio.music_channels;
}

void sm_audio_set_target_rate(float rate)
{
    g_audio.music_rate = rate;
}

static void feed_ring(int max_frames)
{
    if (!s_mp3_data || g_audio.music_eof)
        return;

    int decoded = 0;
    int resyncs = 0;

    /* Top up the ring buffer while there is space and within per-tick budget */
    while (decoded < max_frames && g_audio.ring_count <= SM_AUDIO_RING_SIZE - MINIMP3_MAX_SAMPLES_PER_FRAME) {
        int avail = (int)(s_mp3_size - s_mp3_pos);
        if (avail < 16) {
            g_audio.music_eof = 1;
            break;
        }

        const uint8_t *p = s_mp3_data + s_mp3_pos;
        mp3dec_frame_info_t info;
        memset(&info, 0, sizeof(info));
        int16_t frame_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];

        int samples = mp3dec_decode_frame(&g_audio.dec, p, avail,
                                          frame_pcm, &info);

        if (samples > 0 && info.frame_bytes > 0) {
            s_mp3_pos += (size_t)info.frame_bytes;
            g_audio.music_channels = info.channels;
            g_audio.music_hz       = info.hz;
            g_audio.frames_decoded++;
            decoded++;

            int ch = info.channels;
            for (int s = 0; s < samples; s++) {
                int16_t left, right;
                if (ch >= 2) {
                    left  = frame_pcm[s * ch];
                    right = frame_pcm[s * ch + 1];
                } else {
                    left = right = frame_pcm[s];
                }
                int w = g_audio.ring_write * 2;
                g_audio.ring[w]     = left;
                g_audio.ring[w + 1] = right;
                g_audio.ring_write = (g_audio.ring_write + 1) % SM_AUDIO_RING_SIZE;
            }
            g_audio.ring_count += samples;
            resyncs = 0;
        } else {
            s_mp3_pos++;
            if (++resyncs > SM_MP3_MAX_RESYNC) {
                g_audio.music_eof = 1;
                break;
            }
        }
    }
}

/* ---------------------------- hit sounds --------------------------- */

int sm_audio_load_hit(const char *path)
{
    size_t size = 0;
    uint8_t *data = read_whole_file(path, &size);
    if (!data) return 0;

    int16_t *pcm = NULL;
    int frames = 0;
    int ok = decode_wav_mono(data, size, &pcm, &frames);
    free(data);
    if (!ok) return 0;

    sm_sound_t *s = &g_audio.hit[g_audio.hit_next % SM_HITSOUND_SLOTS];
    g_audio.hit_next++;
    free(s->pcm);
    s->pcm = pcm;
    s->num_samples = frames;
    s->pos = 0;
    return 1;
}

void sm_audio_play_hit(int variant)
{
    (void)variant;
    for (int i = 0; i < SM_HITSOUND_SLOTS; i++) {
        sm_sound_t *s = &g_audio.hit[i];
        if (s->pcm && s->pos >= s->num_samples) {
            s->pos = 0;
            return;
        }
    }
}

int sm_audio_load_menu(const char *path)
{
    size_t size = 0;
    uint8_t *data = read_whole_file(path, &size);
    if (!data) return 0;
    int16_t *pcm = NULL;
    int frames = 0;
    int ok = decode_wav_mono(data, size, &pcm, &frames);
    if (ok) {
        if (frames > SM_MENU_MAX_SAMPLES) { free(pcm); ok = 0; }
        else { memcpy(g_audio.menu, pcm, sizeof(int16_t) * (size_t)frames); g_audio.menu_len = frames; free(pcm); }
    }
    free(data);
    g_audio.menu_pos = 0;
    return ok;
}

void sm_audio_play_menu(void)
{
    if (g_audio.menu_len > 0)
        g_audio.menu_pos = 0;
}

/* ----------------------------- rendering --------------------------- */

void sm_audio_render(int16_t *out, int frames)
{
    int have_music = sm_audio_music_playing() && !g_audio.music_paused;

    g_audio.frames_decoded = 0;

    if (have_music) {
        feed_ring(1);
    }

    int source_hz = (g_audio.music_hz > 0) ? g_audio.music_hz : SM_AUDIO_SAMPLE_RATE;
    uint32_t ratio_fp = ((uint32_t)source_hz << 16) / SM_AUDIO_SAMPLE_RATE;

    for (int i = 0; i < frames; i++) {
        int music_l = 0, music_r = 0;

        /* --- music --- */
        if (have_music && g_audio.ring_count > 0) {
            int r = g_audio.ring_read * 2;
            int16_t ml = g_audio.ring[r];
            int16_t mr = g_audio.ring[r + 1];

            /* Linear interpolation if there's a subsequent sample in buffer */
            if (g_audio.ring_count > 1) {
                int next_r = ((g_audio.ring_read + 1) % SM_AUDIO_RING_SIZE) * 2;
                int frac = (g_audio.resample_acc & 0xFFFF) >> 8; /* 0..255 */
                ml += ((g_audio.ring[next_r] - ml) * frac) >> 8;
                mr += ((g_audio.ring[next_r + 1] - mr) * frac) >> 8;
            }

            music_l = (ml * g_audio.music_volume) >> 8;
            music_r = (mr * g_audio.music_volume) >> 8;

            g_audio.resample_acc += ratio_fp;
            while (g_audio.resample_acc >= 65536 && g_audio.ring_count > 0) {
                g_audio.resample_acc -= 65536;
                g_audio.ring_read = (g_audio.ring_read + 1) % SM_AUDIO_RING_SIZE;
                g_audio.ring_count--;
            }

            g_audio.samples_played++;
        } else if (have_music && g_audio.music_eof && g_audio.ring_count <= 0) {
            g_audio.music_playing = 0;
            have_music = 0;
        }

        /* --- hit sounds --- */
        int sfx_l = 0, sfx_r = 0;
        for (int k = 0; k < SM_HITSOUND_SLOTS; k++) {
            sm_sound_t *s = &g_audio.hit[k];
            if (!s->pcm || s->pos >= s->num_samples) continue;
            int v = s->pcm[s->pos++];
            sfx_l += v;
            sfx_r += v;
        }

        int l = music_l + (sfx_l * g_audio.sfx_volume) / 256;
        int r = music_r + (sfx_r * g_audio.sfx_volume) / 256;

        if (l >  32767) l =  32767;
        if (l < -32768) l = -32768;
        if (r >  32767) r =  32767;
        if (r < -32768) r = -32768;

        out[i * 2]     = (int16_t)l;
        out[i * 2 + 1] = (int16_t)r;
    }

    g_audio.decode_load = (g_audio.frames_decoded * 1000) / 2;
}