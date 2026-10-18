#ifndef SM_BG_H
#define SM_BG_H

#include <stdint.h>
#include <stddef.h>

/*
 * Song Background & Banner loader.
 * Loads JPEG / PNG images and scales them into a static 320x240 RGB565 buffer.
 */

void sm_bg_init(void);
void sm_bg_clear(void);

/* Load background for given song folder and sm paths */
int  sm_bg_load_for_song(const char *dir, const char *bg_file, const char *banner_file);

/* Load specific image file into the background buffer */
int  sm_bg_load_file(const char *path);

/* Draw fullscreen background with brightness control (0..100) */
void sm_bg_render_fullscreen(uint16_t *fb, int brightness_pct);

/* Draw a thumbnail preview of the current background / banner */
void sm_bg_render_thumbnail(uint16_t *fb, int x, int y, int w, int h);

int  sm_bg_has_image(void);

/* Dedicated banner preview support */
int  sm_banner_load_file(const char *path);
int  sm_banner_has_image(void);
void sm_banner_clear(void);
void sm_banner_render(uint16_t *fb, int x, int y, int w, int h);

#endif /* SM_BG_H */
