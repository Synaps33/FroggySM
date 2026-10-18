#ifndef SM_RENDER_H
#define SM_RENDER_H

#include "sm_types.h"
#include "sm_assets.h"

#define COLOR_RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define COLOR_BLACK      COLOR_RGB565(0, 0, 0)
#define COLOR_WHITE      COLOR_RGB565(255, 255, 255)
#define COLOR_GREY       COLOR_RGB565(128, 128, 128)
#define COLOR_MATRIX_GRN COLOR_RGB565(0, 255, 64)
#define COLOR_AMBER      COLOR_RGB565(255, 176, 0)
#define COLOR_CYAN       COLOR_RGB565(0, 200, 255)
#define COLOR_RED        COLOR_RGB565(255, 32, 32)
#define COLOR_YELLOW     COLOR_RGB565(255, 240, 0)
#define COLOR_DEEP_BLUE  COLOR_RGB565(8, 8, 32)
#define COLOR_PANEL      COLOR_RGB565(4, 20, 10)

void sm_render_init(void);
void sm_render_frame(uint16_t *fb);

void sm_render_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t col);
void sm_render_rect_outline(uint16_t *fb, int x, int y, int w, int h, uint16_t col);
void sm_render_text(uint16_t *fb, int x, int y, const char *s, uint16_t col, int scale);
void sm_render_sprite(uint16_t *fb, int x, int y, const sm_sprite_t *sp, int frame,
                      uint16_t key);
void sm_render_logo(uint16_t *fb, int x, int y);

#endif /* SM_RENDER_H */