#ifndef SM_THEME_H
#define SM_THEME_H

#include <stdint.h>
#include <stddef.h>
#include "sm_library.h"

#define SM_MAX_THEMES 16

/*
 * StepMania theme definition.
 * Can be loaded from official StepMania 3.9 / 5 themes containing metrics.ini / ThemeInfo.ini
 * and graphic backgrounds under Themes/<name>/
 */
typedef struct {
    char name[48];        /* Display name e.g. "StepMania 3.9", "DDR Extreme" */
    char author[32];      /* e.g. "StepMania Team" */
    char dir[SM_MAXPATH]; /* Directory path on SD card, or "" if built-in */
    int  is_custom;       /* 1 if loaded from external metrics.ini, 0 if built-in */

    /* Palette colors (RGB565) */
    uint16_t header_bg;
    uint16_t header_accent;
    uint16_t panel_bg;
    uint16_t panel_border;
    uint16_t sel_bg;
    uint16_t sel_border;
    uint16_t life_full;
    uint16_t life_mid;
    uint16_t life_low;
    uint16_t lane_bg;

    /* Associated StepMania graphics (optional) */
    char bg_select[SM_MAXPATH];
    char bg_play[SM_MAXPATH];
    char bg_eval[SM_MAXPATH];
} sm_theme_t;

void sm_theme_init(void);

/* Scan given root directory for Themes/<name>/metrics.ini */
int  sm_theme_scan(const char *root_dir);

int  sm_theme_count(void);
const sm_theme_t *sm_theme_get(int idx);
const sm_theme_t *sm_theme_current(void);
int  sm_theme_current_idx(void);

/* Apply theme by index (loads theme backgrounds if present) */
void sm_theme_apply(int idx);
void sm_theme_next(void);
void sm_theme_prev(void);

/* Helper to convert hex, rgba float/int or named color string into RGB565 */
uint16_t sm_theme_parse_color(const char *str, uint16_t fallback);

#endif /* SM_THEME_H */
