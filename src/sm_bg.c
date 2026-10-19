#include "sm_bg.h"
#include "sm_dirent.h"
#include "sm_diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define BG_WIDTH  320
#define BG_HEIGHT 240

static uint16_t s_bg_pixels[BG_WIDTH * BG_HEIGHT];
static int      s_has_bg = 0;
static char     s_current_path[SM_MAXPATH];

void sm_bg_init(void)
{
    s_has_bg = 0;
    s_current_path[0] = 0;
    memset(s_bg_pixels, 0, sizeof(s_bg_pixels));
}

void sm_bg_clear(void)
{
    s_has_bg = 0;
    s_current_path[0] = 0;
}

int sm_bg_has_image(void)
{
    return s_has_bg;
}

static int load_image_file(const char *path);

int sm_bg_load_file(const char *path)
{
    return load_image_file(path);
}

static int load_image_file(const char *path)
{
    if (!path || !path[0]) return 0;
    if (s_has_bg && strcmp(s_current_path, path) == 0) return 1;

    int w = 0, h = 0, comp = 0;
    sm_diag("bg: load %s", path);
    unsigned char *data = stbi_load(path, &w, &h, &comp, 3);
    sm_diag("bg: result %p %dx%d", (void *)data, w, h);
    if (!data || w <= 0 || h <= 0) return 0;

    /* Resample into 320x240 RGB565 */
    for (int y = 0; y < BG_HEIGHT; y++) {
        int sy = (y * h) / BG_HEIGHT;
        if (sy >= h) sy = h - 1;
        const unsigned char *srow = &data[sy * w * 3];
        uint16_t *drow = &s_bg_pixels[y * BG_WIDTH];
        for (int x = 0; x < BG_WIDTH; x++) {
            int sx = (x * w) / BG_WIDTH;
            if (sx >= w) sx = w - 1;
            const unsigned char *p = &srow[sx * 3];
            uint16_t r = p[0] >> 3;
            uint16_t g = p[1] >> 2;
            uint16_t b = p[2] >> 3;
            drow[x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }

    stbi_image_free(data);
    strncpy(s_current_path, path, sizeof(s_current_path) - 1);
    s_current_path[sizeof(s_current_path) - 1] = 0;
    s_has_bg = 1;
    return 1;
}

int sm_bg_load_for_song(const char *dir, const char *bg_file, const char *banner_file)
{
    char path[SM_MAXPATH * 2];

    /* 1. Try explicit background file */
    if (bg_file && bg_file[0]) {
        snprintf(path, sizeof(path), "%s/%s", dir, bg_file);
        if (load_image_file(path)) return 1;
    }

    /* 2. Try explicit banner file */
    if (banner_file && banner_file[0]) {
        snprintf(path, sizeof(path), "%s/%s", dir, banner_file);
        if (load_image_file(path)) return 1;
    }

    /* 3. Scan directory for background / banner files */
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        char best_bg[SM_MAXPATH];
        char best_bn[SM_MAXPATH];
        best_bg[0] = 0;
        best_bn[0] = 0;

        while ((e = readdir(d)) != NULL) {
            if (!sm_entry_is_file(dir, e->d_name, e->d_type)) continue;
            size_t len = strlen(e->d_name);
            if (len < 4) continue;
            const char *ext = e->d_name + len - 4;
            int is_img = (strcasecmp(ext, ".jpg") == 0 ||
                          strcasecmp(ext, ".png") == 0 ||
                          (len >= 5 && strcasecmp(e->d_name + len - 5, ".jpeg") == 0));
            if (!is_img) continue;

            char lower[256];
            size_t n = len < 255 ? len : 255;
            for (size_t i = 0; i < n; i++) {
                char c = e->d_name[i];
                lower[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
            }
            lower[n] = 0;

            if (strstr(lower, "bg") || strstr(lower, "background")) {
                strncpy(best_bg, e->d_name, sizeof(best_bg) - 1);
                best_bg[sizeof(best_bg) - 1] = 0;
                break;
            } else if (strstr(lower, "banner") || strstr(lower, "bn")) {
                strncpy(best_bn, e->d_name, sizeof(best_bn) - 1);
                best_bn[sizeof(best_bn) - 1] = 0;
            }
        }
        closedir(d);

        if (best_bg[0]) {
            snprintf(path, sizeof(path), "%s/%s", dir, best_bg);
            if (load_image_file(path)) return 1;
        }
        if (best_bn[0]) {
            snprintf(path, sizeof(path), "%s/%s", dir, best_bn);
            if (load_image_file(path)) return 1;
        }
    }

    s_has_bg = 0;
    return 0;
}

void sm_bg_render_fullscreen(uint16_t *fb, int brightness_pct)
{
    if (!s_has_bg || !fb) return;

    if (brightness_pct >= 100) {
        memcpy(fb, s_bg_pixels, BG_WIDTH * BG_HEIGHT * sizeof(uint16_t));
        return;
    }

    if (brightness_pct <= 0) {
        memset(fb, 0, BG_WIDTH * BG_HEIGHT * sizeof(uint16_t));
        return;
    }

    int mult = (brightness_pct * 256) / 100;
    for (int i = 0; i < BG_WIDTH * BG_HEIGHT; i++) {
        uint16_t c = s_bg_pixels[i];
        uint16_t r = (((c >> 11) & 0x1F) * mult) >> 8;
        uint16_t g = (((c >> 5)  & 0x3F) * mult) >> 8;
        uint16_t b = (( c        & 0x1F) * mult) >> 8;
        fb[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

void sm_bg_render_thumbnail(uint16_t *fb, int x, int y, int w, int h)
{
    if (!s_has_bg || !fb || w <= 0 || h <= 0) return;

    for (int dy = 0; dy < h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= BG_HEIGHT) continue;
        int sy = (dy * BG_HEIGHT) / h;
        if (sy >= BG_HEIGHT) sy = BG_HEIGHT - 1;

        uint16_t *dst_row = fb + py * BG_WIDTH;
        const uint16_t *src_row = s_bg_pixels + sy * BG_WIDTH;

        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            if (px < 0 || px >= BG_WIDTH) continue;
            int sx = (dx * BG_WIDTH) / w;
            if (sx >= BG_WIDTH) sx = BG_WIDTH - 1;
            dst_row[px] = src_row[sx];
        }
    }
}

/* ----------------------- dedicated banner preview ------------------- */

#define BANNER_WIDTH  136
#define BANNER_HEIGHT 44

static uint16_t s_banner_pixels[BANNER_WIDTH * BANNER_HEIGHT];
static int      s_has_banner = 0;
static char     s_current_banner_path[SM_MAXPATH];

int sm_banner_load_file(const char *path)
{
    if (!path || !path[0]) {
        s_has_banner = 0;
        s_current_banner_path[0] = 0;
        return 0;
    }
    if (s_has_banner && strcmp(s_current_banner_path, path) == 0)
        return 1;

    int w = 0, h = 0, comp = 0;
    sm_diag("banner: load %s", path);
    unsigned char *data = stbi_load(path, &w, &h, &comp, 3);
    sm_diag("banner: result %p %dx%d", (void *)data, w, h);
    if (!data || w <= 0 || h <= 0) {
        s_has_banner = 0;
        s_current_banner_path[0] = 0;
        return 0;
    }

    for (int y = 0; y < BANNER_HEIGHT; y++) {
        int sy = (y * h) / BANNER_HEIGHT;
        if (sy >= h) sy = h - 1;
        const unsigned char *srow = &data[sy * w * 3];
        uint16_t *drow = &s_banner_pixels[y * BANNER_WIDTH];
        for (int x = 0; x < BANNER_WIDTH; x++) {
            int sx = (x * w) / BANNER_WIDTH;
            if (sx >= w) sx = w - 1;
            const unsigned char *p = &srow[sx * 3];
            uint16_t r = p[0] >> 3;
            uint16_t g = p[1] >> 2;
            uint16_t b = p[2] >> 3;
            drow[x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
    stbi_image_free(data);
    s_has_banner = 1;
    strncpy(s_current_banner_path, path, sizeof(s_current_banner_path) - 1);
    s_current_banner_path[sizeof(s_current_banner_path) - 1] = 0;
    return 1;
}

int sm_banner_has_image(void)
{
    return s_has_banner;
}

void sm_banner_clear(void)
{
    s_has_banner = 0;
    s_current_banner_path[0] = 0;
}

void sm_banner_render(uint16_t *fb, int x, int y, int w, int h)
{
    if (!s_has_banner || !fb || w <= 0 || h <= 0) return;
    for (int dy = 0; dy < h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= BG_HEIGHT) continue;
        int sy = (dy * BANNER_HEIGHT) / h;
        if (sy >= BANNER_HEIGHT) sy = BANNER_HEIGHT - 1;

        uint16_t *dst_row = fb + py * BG_WIDTH;
        const uint16_t *src_row = s_banner_pixels + sy * BANNER_WIDTH;

        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            if (px < 0 || px >= BG_WIDTH) continue;
            int sx = (dx * BANNER_WIDTH) / w;
            if (sx >= BANNER_WIDTH) sx = BANNER_WIDTH - 1;
            dst_row[px] = src_row[sx];
        }
    }
}
