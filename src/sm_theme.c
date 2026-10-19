#include "sm_theme.h"
#include "sm_game.h"
#include "sm_render.h"
#include "sm_bg.h"
#include "sm_dirent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

static int file_exists(const char *p)
{
    if (!p || !p[0]) return 0;
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int dir_exists(const char *p)
{
    if (!p || !p[0]) return 0;
    DIR *d = opendir(p);
    if (!d) return 0;
    closedir(d);
    return 1;
}

static sm_theme_t s_themes[SM_MAX_THEMES];
static int        s_num_themes = 0;
static int        s_current_theme_idx = 0;

static void str_trim(char *s)
{
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && (isspace((unsigned char)s[len - 1]) || s[len - 1] == '"' || s[len - 1] == '\r')) {
        s[--len] = 0;
    }
    if (s[0] == '"') {
        memmove(s, s + 1, strlen(s));
    }
}

uint16_t sm_theme_parse_color(const char *str, uint16_t fallback)
{
    if (!str || !str[0]) return fallback;

    char buf[64];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    str_trim(buf);

    /* 1. Named colors */
    if (strcasecmp(buf, "cyan") == 0)    return COLOR_RGB565(0, 220, 255);
    if (strcasecmp(buf, "green") == 0)   return COLOR_RGB565(0, 255, 64);
    if (strcasecmp(buf, "lime") == 0)    return COLOR_RGB565(60, 255, 80);
    if (strcasecmp(buf, "red") == 0)     return COLOR_RGB565(255, 32, 32);
    if (strcasecmp(buf, "yellow") == 0)  return COLOR_RGB565(255, 230, 20);
    if (strcasecmp(buf, "amber") == 0)   return COLOR_RGB565(255, 176, 0);
    if (strcasecmp(buf, "orange") == 0)  return COLOR_RGB565(255, 128, 0);
    if (strcasecmp(buf, "purple") == 0)  return COLOR_RGB565(190, 50, 255);
    if (strcasecmp(buf, "magenta") == 0) return COLOR_RGB565(255, 40, 220);
    if (strcasecmp(buf, "blue") == 0)    return COLOR_RGB565(30, 80, 255);
    if (strcasecmp(buf, "navy") == 0)    return COLOR_RGB565(10, 20, 60);
    if (strcasecmp(buf, "white") == 0)   return COLOR_RGB565(255, 255, 255);
    if (strcasecmp(buf, "black") == 0)   return COLOR_RGB565(0, 0, 0);
    if (strcasecmp(buf, "grey") == 0 || strcasecmp(buf, "gray") == 0)
        return COLOR_RGB565(128, 128, 128);

    /* 2. Hex formats: #RRGGBB, 0xRRGGBB, RRGGBB */
    const char *hex = buf;
    if (hex[0] == '#') hex++;
    else if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex += 2;
    if (strlen(hex) == 6) {
        unsigned int val = 0;
        if (sscanf(hex, "%x", &val) == 1) {
            int r = (val >> 16) & 0xFF;
            int g = (val >> 8) & 0xFF;
            int b = val & 0xFF;
            return COLOR_RGB565(r, g, b);
        }
    }

    /* 3. Comma-separated: r,g,b or r,g,b,a */
    if (strchr(buf, ',')) {
        if (strchr(buf, '.')) {
            /* Float format: 0.0 .. 1.0 */
            float rf = 0, gf = 0, bf = 0;
            if (sscanf(buf, "%f,%f,%f", &rf, &gf, &bf) >= 3) {
                int r = (int)(rf * 255.0f);
                int g = (int)(gf * 255.0f);
                int b = (int)(bf * 255.0f);
                if (r < 0) r = 0; else if (r > 255) r = 255;
                if (g < 0) g = 0; else if (g > 255) g = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;
                return COLOR_RGB565(r, g, b);
            }
        } else {
            /* Integer format: 0 .. 255 */
            int r = 0, g = 0, b = 0;
            if (sscanf(buf, "%d,%d,%d", &r, &g, &b) >= 3) {
                if (r < 0) r = 0; else if (r > 255) r = 255;
                if (g < 0) g = 0; else if (g > 255) g = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;
                return COLOR_RGB565(r, g, b);
            }
        }
    }

    return fallback;
}

static void resolve_theme_graphics(sm_theme_t *t, const char *theme_dir)
{
    t->bg_select[0] = 0;
    t->bg_play[0]   = 0;
    t->bg_eval[0]   = 0;

    const char *subdirs[2] = { theme_dir, NULL };
    char gfx_dir[SM_MAXPATH];
    snprintf(gfx_dir, sizeof(gfx_dir), "%s/Graphics", theme_dir);
    if (dir_exists(gfx_dir)) {
        subdirs[1] = gfx_dir;
    }

    for (int s = 0; s < 2; s++) {
        const char *dir = subdirs[s];
        if (!dir) continue;
        DIR *d = opendir(dir);
        if (!d) continue;

        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (!sm_entry_is_file(dir, ent->d_name, ent->d_type)) continue;
            size_t len = strlen(ent->d_name);
            if (len < 4) continue;

            const char *ext = ent->d_name + len - 4;
            int is_img = (strcasecmp(ext, ".jpg") == 0 ||
                          strcasecmp(ext, ".png") == 0 ||
                          (len >= 5 && strcasecmp(ent->d_name + len - 5, ".jpeg") == 0));
            if (!is_img) continue;

            char lower[128];
            size_t n = len < 127 ? len : 127;
            for (size_t i = 0; i < n; i++) {
                char c = ent->d_name[i];
                lower[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
            }
            lower[n] = 0;

            if (!t->bg_select[0] && (strstr(lower, "selectmusic") || strstr(lower, "select"))) {
                snprintf(t->bg_select, sizeof(t->bg_select), "%s/%s", dir, ent->d_name);
            } else if (!t->bg_play[0] && (strstr(lower, "gameplay") || strstr(lower, "stage"))) {
                snprintf(t->bg_play, sizeof(t->bg_play), "%s/%s", dir, ent->d_name);
            } else if (!t->bg_eval[0] && (strstr(lower, "eval") || strstr(lower, "result"))) {
                snprintf(t->bg_eval, sizeof(t->bg_eval), "%s/%s", dir, ent->d_name);
            } else if (!t->bg_select[0] && strstr(lower, "background")) {
                snprintf(t->bg_select, sizeof(t->bg_select), "%s/%s", dir, ent->d_name);
            }
        }
        closedir(d);
    }
}

static int parse_metrics_file(const char *filepath, sm_theme_t *t)
{
    FILE *f = fopen(filepath, "r");
    if (!f) return 0;

    char line[256];
    char section[64] = "";

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == 0 || *p == '#' || *p == ';' || (*p == '/' && *(p + 1) == '/'))
            continue;

        if (*p == '[') {
            char *end = strchr(p, ']');
            if (end) {
                *end = 0;
                strncpy(section, p + 1, sizeof(section) - 1);
                section[sizeof(section) - 1] = 0;
                str_trim(section);
            }
            continue;
        }

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = p;
        char *val = eq + 1;
        str_trim(key);
        str_trim(val);

        if (strcasecmp(key, "DisplayName") == 0 || strcasecmp(key, "ThemeName") == 0) {
            if (val[0]) {
                strncpy(t->name, val, sizeof(t->name) - 1);
                t->name[sizeof(t->name) - 1] = 0;
            }
        } else if (strcasecmp(key, "Author") == 0) {
            strncpy(t->author, val, sizeof(t->author) - 1);
            t->author[sizeof(t->author) - 1] = 0;
        } else if (strcasecmp(key, "HeaderColor") == 0 || strcasecmp(key, "HeaderBg") == 0 ||
                   strcasecmp(key, "HeaderBackground") == 0) {
            t->header_bg = sm_theme_parse_color(val, t->header_bg);
        } else if (strcasecmp(key, "HeaderAccent") == 0 || strcasecmp(key, "AccentColor") == 0 ||
                   strcasecmp(key, "ColorP1") == 0) {
            t->header_accent = sm_theme_parse_color(val, t->header_accent);
        } else if (strcasecmp(key, "PanelBg") == 0 || strcasecmp(key, "PanelBackground") == 0 ||
                   strcasecmp(key, "CardBg") == 0) {
            t->panel_bg = sm_theme_parse_color(val, t->panel_bg);
        } else if (strcasecmp(key, "PanelBorder") == 0 || strcasecmp(key, "CardBorder") == 0) {
            t->panel_border = sm_theme_parse_color(val, t->panel_border);
        } else if (strcasecmp(key, "SelectionColor") == 0 || strcasecmp(key, "SelBg") == 0) {
            t->sel_bg = sm_theme_parse_color(val, t->sel_bg);
        } else if (strcasecmp(key, "SelectionBorderColor") == 0 || strcasecmp(key, "SelBorder") == 0) {
            t->sel_border = sm_theme_parse_color(val, t->sel_border);
        } else if (strcasecmp(key, "LifeMeterNormalColor") == 0 || strcasecmp(key, "LifeColor") == 0) {
            t->life_full = sm_theme_parse_color(val, t->life_full);
        } else if (strcasecmp(key, "LifeMeterMidColor") == 0 || strcasecmp(key, "LifeMidColor") == 0) {
            t->life_mid = sm_theme_parse_color(val, t->life_mid);
        } else if (strcasecmp(key, "LifeMeterDangerColor") == 0 || strcasecmp(key, "DangerColor") == 0) {
            t->life_low = sm_theme_parse_color(val, t->life_low);
        } else if (strcasecmp(key, "LaneBackgroundColor") == 0 || strcasecmp(key, "LaneBg") == 0) {
            t->lane_bg = sm_theme_parse_color(val, t->lane_bg);
        }
    }

    fclose(f);
    return 1;
}

static int load_theme_from_dir(const char *dir, const char *folder_name)
{
    if (s_num_themes >= SM_MAX_THEMES) return 0;

    sm_theme_t t;
    memset(&t, 0, sizeof(t));
    strncpy(t.dir, dir, sizeof(t.dir) - 1);
    t.dir[sizeof(t.dir) - 1] = 0;
    t.is_custom = 1;

    /* Default colors based on classic StepMania blue */
    strncpy(t.name, folder_name, sizeof(t.name) - 1);
    t.header_bg     = COLOR_RGB565(12, 28, 64);
    t.header_accent = COLOR_RGB565(0, 220, 255);
    t.panel_bg      = COLOR_RGB565(8, 16, 40);
    t.panel_border  = COLOR_RGB565(24, 48, 96);
    t.sel_bg        = COLOR_RGB565(20, 48, 110);
    t.sel_border    = COLOR_RGB565(80, 180, 255);
    t.life_full     = COLOR_RGB565(40, 220, 255);
    t.life_mid      = COLOR_RGB565(255, 220, 60);
    t.life_low      = COLOR_RGB565(255, 40, 40);
    t.lane_bg       = COLOR_RGB565(6, 12, 32);

    char path[SM_MAXPATH];

    /* 1. ThemeInfo.ini */
    snprintf(path, sizeof(path), "%s/ThemeInfo.ini", dir);
    if (file_exists(path)) parse_metrics_file(path, &t);

    /* 2. theme.ini */
    snprintf(path, sizeof(path), "%s/theme.ini", dir);
    if (file_exists(path)) parse_metrics_file(path, &t);

    /* 3. metrics.ini */
    snprintf(path, sizeof(path), "%s/metrics.ini", dir);
    if (file_exists(path)) parse_metrics_file(path, &t);

    /* 4. Resolve graphics */
    resolve_theme_graphics(&t, dir);

    /* Deduplicate by theme name */
    for (int i = 0; i < s_num_themes; i++) {
        if (strcasecmp(s_themes[i].name, t.name) == 0) {
            /* Update existing theme directory if this one has graphics */
            if (t.bg_select[0] || t.bg_play[0]) {
                s_themes[i] = t;
            }
            return 1;
        }
    }

    s_themes[s_num_themes++] = t;
    return 1;
}

int sm_theme_scan(const char *root_dir)
{
    if (!root_dir || !dir_exists(root_dir)) return 0;

    DIR *d = opendir(root_dir);
    if (!d) return 0;

    int loaded = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && s_num_themes < SM_MAX_THEMES) {
        if (ent->d_name[0] == '.') continue;
        if (!sm_entry_is_dir(root_dir, ent->d_name, ent->d_type)) continue;

        char sub[SM_MAXPATH];
        snprintf(sub, sizeof(sub), "%.200s/%.50s", root_dir, ent->d_name);

        char met[SM_MAXPATH + 32];
        snprintf(met, sizeof(met), "%s/metrics.ini", sub);
        char ti[SM_MAXPATH + 32];
        snprintf(ti, sizeof(ti), "%s/ThemeInfo.ini", sub);
        char ini[SM_MAXPATH + 32];
        snprintf(ini, sizeof(ini), "%s/theme.ini", sub);

        if (file_exists(met) || file_exists(ti) || file_exists(ini) || dir_exists(sub)) {
            if (load_theme_from_dir(sub, ent->d_name)) loaded++;
        }
    }
    closedir(d);
    return loaded;
}

void sm_theme_init(void)
{
    s_num_themes = 0;

    /* StepMania 3.9 Official Theme */
    sm_theme_t t0;
    memset(&t0, 0, sizeof(t0));
    strncpy(t0.name, "StepMania 3.9 Official", sizeof(t0.name) - 1);
    strncpy(t0.author, "StepMania Team", sizeof(t0.author) - 1);
    t0.header_bg     = COLOR_RGB565(82, 27, 42);   /* Deep wine / plum */
    t0.header_accent = COLOR_RGB565(245, 195, 60);  /* Amber / gold */
    t0.panel_bg      = COLOR_RGB565(42, 14, 22);   /* Dark plum card background */
    t0.panel_border  = COLOR_RGB565(130, 44, 64);  /* Metallic rose border */
    t0.sel_bg        = COLOR_RGB565(144, 60, 62);  /* Active wine card */
    t0.sel_border    = COLOR_RGB565(255, 215, 60);  /* Glowing golden selection */
    t0.life_full     = COLOR_RGB565(40, 220, 255);
    t0.life_mid      = COLOR_RGB565(255, 220, 60);
    t0.life_low      = COLOR_RGB565(255, 40, 40);
    t0.lane_bg       = COLOR_RGB565(6, 12, 32);

    /* Try to load official graphics / metrics from Themes/default */
    const char *default_dirs[] = {
        "/mnt/sda1/ROMS/froggysm/Themes/default",
        "/mnt/sda1/ROMS/FroggySM/Themes/default",
        "/mnt/sda1/Themes/default",
        "/mnt/sda1/ROMS/PSPMania/Themes/default",
        "/media/Sajnaps/GB300/ROMS/froggysm/Themes/default",
        "/home/Sajnaps/stepmania-sf2000/Themes/default",
        "Themes/default",
        NULL
    };

    for (int i = 0; default_dirs[i]; i++) {
        if (dir_exists(default_dirs[i])) {
            char mpath[SM_MAXPATH];
            snprintf(mpath, sizeof(mpath), "%s/metrics.ini", default_dirs[i]);
            if (file_exists(mpath)) parse_metrics_file(mpath, &t0);

            snprintf(t0.bg_select, sizeof(t0.bg_select), "%s/Graphics/ScreenSelectMusic background.png", default_dirs[i]);
            if (!file_exists(t0.bg_select)) t0.bg_select[0] = 0;

            snprintf(t0.bg_play, sizeof(t0.bg_play), "%s/Graphics/ScreenGameplay background.png", default_dirs[i]);
            if (!file_exists(t0.bg_play)) t0.bg_play[0] = 0;

            snprintf(t0.bg_eval, sizeof(t0.bg_eval), "%s/Graphics/ScreenEvaluation background.png", default_dirs[i]);
            if (!file_exists(t0.bg_eval)) t0.bg_eval[0] = 0;

            strncpy(t0.dir, default_dirs[i], sizeof(t0.dir) - 1);
            t0.is_custom = 1;
            break;
        }
    }

    s_themes[0] = t0;
    s_num_themes = 1;
    s_current_theme_idx = 0;
}

int sm_theme_count(void)
{
    return s_num_themes;
}

const sm_theme_t *sm_theme_get(int idx)
{
    if (idx < 0 || idx >= s_num_themes) return &s_themes[0];
    return &s_themes[idx];
}

const sm_theme_t *sm_theme_current(void)
{
    return sm_theme_get(s_current_theme_idx);
}

int sm_theme_current_idx(void)
{
    return s_current_theme_idx;
}

void sm_theme_apply(int idx)
{
    if (s_num_themes <= 0) return;
    if (idx < 0 || idx >= s_num_themes) idx = 0;
    s_current_theme_idx = idx;
    g_game.theme = idx;

    const sm_theme_t *t = &s_themes[idx];
    if (g_game.screen == SCREEN_SELECT) {
        if (t->bg_select[0]) {
            sm_bg_load_file(t->bg_select);
        } else {
            sm_bg_clear();
        }
    } else if (g_game.screen == SCREEN_PLAY) {
        if (t->bg_play[0]) {
            sm_bg_load_file(t->bg_play);
        }
    }
}

void sm_theme_next(void)
{
    if (s_num_themes <= 0) return;
    int next = (s_current_theme_idx + 1) % s_num_themes;
    sm_theme_apply(next);
    g_game.theme_popup_timer = 90;
}

void sm_theme_prev(void)
{
    if (s_num_themes <= 0) return;
    int prev = (s_current_theme_idx + s_num_themes - 1) % s_num_themes;
    sm_theme_apply(prev);
    g_game.theme_popup_timer = 90;
}
