#include "sm_types.h"
#include "sm_song.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * .sm / .ssc chart loader
 *
 * Ported from mafu9/PSPMania (StepMania 3.9):
 *   MsdFile.cpp:36-121          MSD tokenizer
 *   NotesLoaderSM.cpp:82-156   pass 1: #OFFSET / #STOPS / #BPMS
 *   NotesLoaderSM.cpp:192-362  pass 2: metadata + #NOTES
 *   NoteDataUtil.cpp:48-167    LoadFromSMNoteDataString
 *   NoteData.cpp:628-656       Convert2sAnd3sToHoldNotes
 * ------------------------------------------------------------------------- */

/* ---------------------------- MSD tokenizer --------------------------- */
/*
 * Value syntax:  #TAG:param:param;
 *
 * MsdFile.cpp:46-55 blanks out "//" comments first, which leaves ':' and ';'
 * unambiguous as parameter delimiters.  A '#' that begins a line (ignoring
 * whitespace) starts the next value -- that is the recovery path for a value
 * missing its ';', see MsdFile.cpp:57-85.
 *
 * Parameters are handed back as spans into the caller's buffer.  A .sm note
 * payload can be hundreds of kilobytes, so copying every parameter into a
 * stack array would be fatal on a 200MHz MIPS core with a small stack.
 */

typedef struct {
    char  *buf;      /* mutable working copy, comments already blanked */
    size_t len;
    size_t pos;
} sm_tok_t;

typedef struct {
    char  *p;
    size_t len;
} sm_span_t;

#define SM_MAX_PARAMS 12

static void tok_init(sm_tok_t *t, char *buf, size_t len)
{
    t->buf = buf;
    t->len = len;
    t->pos = 0;

    /* MsdFile.cpp:46-55 -- overwrite comments with spaces */
    for (size_t i = 0; i + 1 < len; i++) {
        if (buf[i] == '/' && buf[i + 1] == '/') {
            size_t j = i;
            while (j < len && buf[j] != '\n') {
                buf[j] = ' ';
                j++;
            }
            i = j - 1;
        }
    }
}

/* Does a '#' at `at` start a new value?  Mirrors the backwards line check in
 * MsdFile.cpp:57-85: only whitespace may precede it on its line. */
static int hash_starts_value(const sm_tok_t *t, size_t at)
{
    size_t i = at;
    while (i > 0) {
        char c = t->buf[i - 1];
        if (c == '\n') return 1;
        if (c == ' ' || c == '\t' || c == '\r') { i--; continue; }
        return 0;
    }
    return 1;   /* start of buffer */
}

/*
 * Read one complete value.  Returns 0 once the buffer is exhausted.
 * `out_np` receives the parameter count, which may exceed max_params.
 */
static int tok_next_value(sm_tok_t *t, sm_span_t *out, int max_params, int *out_np)
{
    /* find the next '#' that starts a value */
    for (;;) {
        while (t->pos < t->len && t->buf[t->pos] != '#') t->pos++;
        if (t->pos >= t->len) return 0;
        if (hash_starts_value(t, t->pos)) break;
        t->pos++;            /* a '#' inside a value, not a tag */
    }
    t->pos++;                /* consume the '#' */

    int np = 0;
    for (;;) {
        size_t start = t->pos;
        while (t->pos < t->len && t->buf[t->pos] != ':' &&
               t->buf[t->pos] != ';' && t->buf[t->pos] != '#')
            t->pos++;

        size_t n = t->pos - start;
        if (n > (1u << 30)) n = (1u << 30);
        if (np < max_params) {
            out[np].p   = t->buf + start;
            out[np].len = n;
        }
        np++;

        if (t->pos >= t->len) break;

        char d = t->buf[t->pos];
        if (d == ':') {
            t->pos++;
            if (np >= max_params)
                break;      /* drop the overflow tail, keep the value */
            continue;
        }
        if (d == ';') {
            t->pos++;
            break;
        }
        /* '#' mid-value: the value was never terminated.  Leave the '#' for
         * the next call. */
        break;
    }

    *out_np = np;
    return 1;
}

static void copy_str(char *dst, size_t n, const char *src)
{
    if (n == 0) return;
    size_t i = 0;
    for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

/* copy a span into a bounded, NUL-terminated scratch buffer and trim it */
static int span_to_buf(sm_span_t s, char *dst, size_t n);

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

static void trim(char *s)
{
    size_t n = strlen(s);
    size_t a = 0;
    while (a < n && is_space(s[a])) a++;
    size_t b = n;
    while (b > a && is_space(s[b - 1])) b--;
    memmove(s, s + a, b - a);
    s[b - a] = '\0';
}

/* copy a span into a bounded, NUL-terminated scratch buffer and trim it */
static int span_to_buf(sm_span_t s, char *dst, size_t n)
{
    if (n == 0) return 0;
    size_t i = 0;
    for (; i + 1 < n && i < s.len; i++) dst[i] = s.p[i];
    dst[i] = 0;
    trim(dst);
    return 1;
}

/* Case-insensitive compare of a span against a literal.  Tags in .sm files
 * are conventionally upper case but the original upper-cases before comparing
 * (NotesLoaderSM.cpp), so match that. */
static int span_ieq(sm_span_t s, const char *lit)
{
    size_t n = strlen(lit);
    if (s.len != n) return 0;
    for (size_t i = 0; i < n; i++)
        if (toupper((unsigned char)s.p[i]) != toupper((unsigned char)lit[i]))
            return 0;
    return 1;
}

/* Parse "a=b" or "a" -> returns 0/1.  *out_b set on success. */
static int parse_assign(const char *s, float *out_b, float *out_a)
{
    const char *eq = strchr(s, '=');
    if (!eq)
        return 0;
    char lhs[64];
    size_t n = (size_t)(eq - s);
    if (n >= sizeof(lhs)) n = sizeof(lhs) - 1;
    memcpy(lhs, s, n);
    lhs[n] = '\0';
    trim(lhs);
    *out_b = (float)atof(eq + 1);
    *out_a = (float)atof(lhs);
    return 1;
}

static void sort_bpms(sm_timing_t *t)
{
    for (int i = 1; i < t->num_bpm; i++) {
        sm_bpm_seg_t key = t->bpm[i];
        int j = i - 1;
        while (j >= 0 && t->bpm[j].start_beat > key.start_beat) {
            t->bpm[j + 1] = t->bpm[j];
            j--;
        }
        t->bpm[j + 1] = key;
    }
}

static void sort_stops(sm_timing_t *t)
{
    for (int i = 1; i < t->num_stop; i++) {
        sm_stop_seg_t key = t->stop[i];
        int j = i - 1;
        while (j >= 0 && t->stop[j].start_beat > key.start_beat) {
            t->stop[j + 1] = t->stop[j];
            j--;
        }
        t->stop[j + 1] = key;
    }
}

/* ---------------------------- note parsing ---------------------------- */

static void steps_alloc(sm_steps_t *st, int rows, int tracks)
{
    st->num_tracks = tracks;
    st->num_rows   = rows;
    st->num_holds  = 0;
    st->notes = (sm_note_t *)calloc((size_t)rows * (size_t)tracks, sizeof(sm_note_t));
    for (size_t i = 0; i < (size_t)rows * (size_t)tracks; i++)
        st->notes[i].hold_index = -1;
    /* Headroom: every note row could in principle become a hold. */
    st->holds = (sm_hold_t *)calloc((size_t)rows + 1, sizeof(sm_hold_t));
}

void sm_steps_free(sm_steps_t *st)
{
    free(st->notes);
    free(st->holds);
    st->notes = NULL;
    st->holds = NULL;
    st->num_rows = 0;
    st->num_holds = 0;
}

/*
 * NoteDataUtil.cpp:48-167 -- LoadFromSMNoteDataString
 *
 * Measures are comma-separated, rows newline-separated.  A measure split into
 * K rows occupies K evenly spaced beats, which is what makes K=3 triplets,
 * K=8 eighths and so on.  Any K is legal.
 *
 * Two deliberate deviations from the original, both bug fixes:
 *   - rows are classified as blank *after* trimming, so the CR of a CRLF file
 *     cannot be counted as an extra row and shift the subdivision;
 *   - the payload bounds the grid, so a chart can never write a row index
 *     outside the allocation and corrupt neighbouring notes.
 *
 * The payload is addressed by span and never permanently rewritten: it can be
 * hundreds of kilobytes and the note grid is already sized from its length.
 */
static void parse_note_data(sm_steps_t *st, sm_span_t s)
{
    const int tracks = st->num_tracks;
    const int rows   = st->num_rows;

    /* Work on a private, NUL-terminated copy: MsdFile already stripped
     * file-level comments, so this only bites on charts that still carry the
     * writer's " // measure N" headers inside the payload
     * (NoteDataUtil.cpp:61-69). */
    char *work = (char *)malloc(s.len + 1);
    if (!work) return;
    memcpy(work, s.p, s.len);
    work[s.len] = 0;

    for (char *p = work; *p; p++) {
        if (p[0] == '/' && p[1] == '/') {
            char *e = p;
            while (*e && *e != '\n') e++;
            char *dst = p;
            for (char *src = p; src < e; src++) *dst++ = ' ';
            p = dst - 1;
        }
    }

    size_t pos = 0;
    int measure = 0;

    while (pos < s.len && measure < rows) {
        size_t meas_end = pos;
        while (meas_end < s.len && work[meas_end] != ',') meas_end++;

        /* Collect this measure's non-blank rows. */
        int    line_off[64];
        size_t line_end[64];
        int    num_lines = 0;

        size_t lp = pos;
        while (lp < meas_end) {
            size_t nl = lp;
            while (nl < meas_end && work[nl] != '\n') nl++;

            size_t a = lp, b = nl;
            while (a < b && is_space(work[a])) a++;
            while (b > a && is_space(work[b - 1])) b--;

            if (a < b && num_lines < 64) {
                line_off[num_lines] = (int)a;
                line_end[num_lines] = b;
                num_lines++;
            }
            lp = (nl < meas_end) ? nl + 1 : meas_end;
        }

        for (int l = 0; l < num_lines; l++) {
            float percent = (float)l / (float)num_lines;
            float beat    = ((float)measure + percent) * (float)SM_BEATS_PER_MEASURE;
            int row       = (int)(beat * (float)SM_ROWS_PER_BEAT + 0.5f);
            if (row >= rows) row = rows - 1;
            if (row < 0)     row = 0;

            int len  = (int)(line_end[l] - line_off[l]);
            int cmax = len < tracks ? len : tracks;

            for (int c = 0; c < cmax; c++) {
                char ch = work[line_off[l] + c];
                uint8_t type = NT_EMPTY;
                if      (ch == '1') type = NT_TAP;
                else if (ch == '2') type = NT_HOLD_HEAD;
                else if (ch == 'M') type = NT_MINE;            /* uppercase only */
                else if (ch == '3') type = NT_HOLD_TAIL;      /* consumed by convert_holds */
                else if (ch >= 'a' && ch <= 'h') type = NT_TAP;  /* attack */

                if (type != NT_EMPTY)
                    st->notes[row * tracks + c].type = type;
            }
        }

        measure++;
        if (meas_end >= s.len) break;
        pos = meas_end + 1;
    }

    free(work);
}

/*
 * NoteData.cpp:628-656 -- Convert2sAnd3sToHoldNotes
 *
 * A '2' starts a hold and the matching '3' ends it.  Both markers are kept in
 * the grid while parsing and joined here: the head is recorded against a
 * HoldNote and the tail marker is erased, which is what lets the renderer draw
 * the hold body and the engine score the release.
 *
 * The terminator must be the '3', never merely the next non-empty cell.
 * Looking for "the next note" walks straight through the tail marker and eats
 * whatever note follows the hold, which both deletes that note and stretches
 * the hold to it.  A hold also spans an arbitrary number of rows -- a
 * quarter-note hold is 24 rows long at SM_ROWS_PER_BEAT 24 -- so there is no
 * useful upper bound on the search; it runs to the next '3' in the column, and
 * a second '2' in the same column means the chart is malformed and the head is
 * dropped rather than swallowing the rest of the column.
 */
static void convert_holds(sm_steps_t *st)
{
    int tracks = st->num_tracks;
    int rows   = st->num_rows;

    for (int col = 0; col < tracks; col++) {
        int row = 0;
        while (row < rows) {
            sm_note_t *n = &st->notes[row * tracks + col];
            if (n->type != NT_HOLD_HEAD) {
                row++;
                continue;
            }

            /* scan forward for the tail marker */
            int end = -1;
            for (int r = row + 1; r < rows; r++) {
                sm_note_t *m = &st->notes[r * tracks + col];
                if (m->type == NT_HOLD_TAIL) { end = r; break; }
                if (m->type == NT_HOLD_HEAD) break;   /* malformed: no terminator */
            }

            if (end < 0 || st->num_holds >= rows) {
                /* unterminated hold disappears entirely */
                n->type = NT_EMPTY;
                n->hold_index = -1;
                row++;
                continue;
            }

            int h = st->num_holds++;
            st->holds[h].start_row = (int16_t)row;
            st->holds[h].end_row   = (int16_t)end;
            st->holds[h].score     = HNS_NONE;
            n->hold_index = (int16_t)h;

            /* erase the tail marker; the row itself carries no scorable note */
            st->notes[end * tracks + col].type       = NT_EMPTY;
            st->notes[end * tracks + col].hold_index = -1;
            row = end;
        }
    }

    /* A stray '3' with no head is not a note at all. */
    for (int i = 0; i < rows * tracks; i++)
        if (st->notes[i].type == NT_HOLD_TAIL) {
            st->notes[i].type       = NT_EMPTY;
            st->notes[i].hold_index = -1;
        }
}

/* Difficulty defaults -- Steps.cpp:118-165 */
static int difficulty_from_meter(int meter)
{
    if (meter == 1)  return DIFFICULTY_BEGINNER;
    if (meter <= 3)  return DIFFICULTY_EASY;
    if (meter <= 6)  return DIFFICULTY_MEDIUM;
    return DIFFICULTY_HARD;
}

static const char *difficulty_name(int d)
{
    switch (d) {
        case DIFFICULTY_BEGINNER: return "Beginner";
        case DIFFICULTY_EASY:     return "Easy";
        case DIFFICULTY_MEDIUM:   return "Medium";
        case DIFFICULTY_HARD:     return "Hard";
        case DIFFICULTY_CHALLENGE:return "Challenge";
        default:                  return "Edit";
    }
}

/* Prediction -- Steps.cpp:118-143 */
static int predict_meter(const float *r, int difficulty)
{
    static const float radar_coef[10] = {10.1f, 5.27f, -0.905f, -1.10f, 2.86f, 0, 0, 0, 0, 0};
    static const float diff_coef[6]   = {-0.877f, -0.877f, 0.0f, 0.722f, 0.722f, 0.0f};

    float meter = 0.775f;
    for (int i = 0; i < 5; i++)
        meter += radar_coef[i] * r[i];
    meter += diff_coef[difficulty < 6 ? difficulty : 5];
    meter += -6.35f * r[4] * r[7];
    meter += -2.58f * r[6] * r[6];

    int m = (int)(meter + 0.5f);
    return m < 1 ? 1 : m;
}

/* Steps.cpp:145-165 -- a chart with no explicit difficulty falls back to the
 * meter, and a missing meter is predicted from the radar values. */
static void tidy_diff(sm_diff_t *d)
{
    if (d->difficulty < 0 || d->difficulty >= NUM_DIFFICULTIES)
        d->difficulty = difficulty_from_meter(d->meter);
    if (d->meter < 1)
        d->meter = predict_meter(d->radar, d->difficulty);
    if (d->difficulty_name[0] == 0)
        copy_str(d->difficulty_name, sizeof(d->difficulty_name),
                 difficulty_name(d->difficulty));
}

/*
 * Largest note row the payload can actually reach.
 *
 * A measure spans four beats however many rows it is split into, so counting
 * commas bounds the grid at SM_ROWS_PER_MEASURE rows per measure.  That is
 * cheap but wastes memory on charts whose trailing measures are empty, and it
 * is what put the demo library's longest song over a naive cap.  Walking the
 * rows and tracking the last row that actually holds a note is exact, costs
 * one extra linear pass over a payload we are about to copy anyway, and lets
 * a long song load without clamping.
 */
static int note_payload_last_row(sm_span_t s)
{
    int    max_row = -1;
    int    measure = 0;
    size_t pos = 0;

    while (pos < s.len && measure < SM_MAX_ROWS) {
        size_t meas_end = pos;
        while (meas_end < s.len && s.p[meas_end] != ',') meas_end++;

        int    line_off[64];
        size_t line_end[64];
        int    num_lines = 0;

        size_t lp = pos;
        while (lp < meas_end) {
            size_t nl = lp;
            while (nl < meas_end && s.p[nl] != '\n') nl++;

            size_t a = lp, b = nl;
            while (a < b && is_space(s.p[a])) a++;
            while (b > a && is_space(s.p[b - 1])) b--;

            if (a < b && num_lines < 64) {
                line_off[num_lines] = (int)a;
                line_end[num_lines] = b;
                num_lines++;
            }
            lp = (nl < meas_end) ? nl + 1 : meas_end;
        }

        for (int l = 0; l < num_lines; l++) {
            for (size_t i = line_off[l]; i < line_end[l]; i++) {
                char ch = s.p[i];
                /* '3' counts: a chart whose last note is a hold tail must keep
                 * the rows up to and including that tail. */
                if (ch == '1' || ch == '2' || ch == '3' || ch == 'M' ||
                    (ch >= 'a' && ch <= 'h')) {
                    float percent = (float)l / (float)num_lines;
                    float beat    = ((float)measure + percent)
                                    * (float)SM_BEATS_PER_MEASURE;
                    int row = (int)(beat * (float)SM_ROWS_PER_BEAT + 0.5f);
                    if (row > max_row) max_row = row;
                    break;
                }
            }
        }

        measure++;
        if (meas_end >= s.len) break;
        pos = meas_end + 1;
    }
    return max_row;
}

/* ------------------------------------------------------------------------ */
/*  Public entry point                                                       */
/* ------------------------------------------------------------------------ */

int sm_song_parse(char *text, size_t len, sm_song_t *song)
{
    memset(song, 0, sizeof(*song));
    song->text     = text;
    song->text_len = len;

    sm_tok_t tok;
    tok_init(&tok, text, len);

    sm_span_t params[SM_MAX_PARAMS];
    char      name[64];
    char      buf[SM_MAX_PARAM_LEN];
    int       np;
    int       pass;

    /* NotesLoaderSM.cpp:82-156 runs a dedicated timing pass before the main
     * loop, because #OFFSET/#BPMS/#STOPS are frequently written after the
     * #NOTES they affect.  Both passes therefore walk the same token stream. */
    for (pass = 0; pass < 2; pass++) {

        tok.pos = 0;

        while (tok_next_value(&tok, params, SM_MAX_PARAMS, &np)) {

            if (np < 1 || !span_to_buf(params[0], name, sizeof(name)))
                continue;

            if (pass == 0) {
                /* ---------------- timing pass ---------------- */
                if (span_ieq(params[0], "OFFSET")) {
                    if (np >= 2 && span_to_buf(params[1], buf, sizeof(buf)))
                        song->timing.beat0_offset = (float)atof(buf);
                } else if (span_ieq(params[0], "STOPS") ||
                           span_ieq(params[0], "FREEZES")) {
                    if (np >= 2 && span_to_buf(params[1], buf, sizeof(buf))) {
                        char *p = buf;
                        while (p) {
                            char *comma = strchr(p, ',');
                            if (comma) *comma = 0;
                            float b, s;
                            if (parse_assign(p, &s, &b) &&
                                song->timing.num_stop < 32) {
                                song->timing.stop[song->timing.num_stop].start_beat = b;
                                song->timing.stop[song->timing.num_stop].seconds    = s;
                                song->timing.num_stop++;
                                sort_stops(&song->timing);
                            }
                            p = comma ? comma + 1 : 0;
                        }
                    }
                } else if (span_ieq(params[0], "BPMS")) {
                    if (np >= 2 && span_to_buf(params[1], buf, sizeof(buf))) {
                        char *p = buf;
                        while (p) {
                            char *comma = strchr(p, ',');
                            if (comma) *comma = 0;
                            float b, v;
                            if (parse_assign(p, &v, &b) &&
                                song->timing.num_bpm < 32) {
                                song->timing.bpm[song->timing.num_bpm].start_beat = b;
                                song->timing.bpm[song->timing.num_bpm].bpm        = v;
                                song->timing.num_bpm++;
                                sort_bpms(&song->timing);
                            }
                            p = comma ? comma + 1 : 0;
                        }
                    }
                }
                continue;
            }

            /* ---------------- metadata pass ---------------- */
            if (span_ieq(params[0], "TITLE")) {
                if (np >= 2) span_to_buf(params[1], song->title, sizeof(song->title));
            } else if (span_ieq(params[0], "SUBTITLE")) {
                if (np >= 2) span_to_buf(params[1], song->subtitle, sizeof(song->subtitle));
            } else if (span_ieq(params[0], "ARTIST")) {
                if (np >= 2) span_to_buf(params[1], song->artist, sizeof(song->artist));
            } else if (span_ieq(params[0], "CREDIT")) {
                if (np >= 2) span_to_buf(params[1], song->credit, sizeof(song->credit));
            } else if (span_ieq(params[0], "MUSIC")) {
                if (np >= 2) span_to_buf(params[1], song->music_file, sizeof(song->music_file));
            } else if (span_ieq(params[0], "BACKGROUND") || span_ieq(params[0], "BG")) {
                if (np >= 2) span_to_buf(params[1], song->bg_file, sizeof(song->bg_file));
            } else if (span_ieq(params[0], "BANNER")) {
                if (np >= 2) span_to_buf(params[1], song->banner_file, sizeof(song->banner_file));
            } else if (span_ieq(params[0], "NOTES")) {

                /* NotesLoaderSM.cpp:340-356
                 *   #NOTES:<type>:<charter>:<difficulty>:<meter>:<radar>:<data>[:<attacks>]
                 * Every #NOTES becomes an independent difficulty.  Only the
                 * header is kept here; the note grid is decoded on demand by
                 * sm_song_load_steps so the song wheel never pays for charts
                 * the player does not pick. */
                if (np < 7 || song->num_diffs >= SM_MAX_STEPS_PER_SONG)
                    continue;

                sm_diff_t *d = &song->diffs[song->num_diffs];
                memset(d, 0, sizeof(*d));
                d->difficulty = DIFFICULTY_EDIT;

                span_to_buf(params[2], d->author,          sizeof(d->author));
                span_to_buf(params[2], d->description,     sizeof(d->description));
                span_to_buf(params[3], d->difficulty_name, sizeof(d->difficulty_name));

                if (span_to_buf(params[4], buf, sizeof(buf)))
                    d->meter = atoi(buf);

                /* Radar values are applied only when all ten are present
                 * (NotesLoaderSM.cpp:48-56) */
                if (span_to_buf(params[5], buf, sizeof(buf))) {
                    int count = 0;
                    char *p = buf;
                    while (p && count < 10) {
                        char *comma = strchr(p, ',');
                        if (comma) *comma = 0;
                        d->radar[count++] = (float)atof(p);
                        p = comma ? comma + 1 : 0;
                    }
                }

                d->note_off = (uint32_t)(params[6].p - text);
                d->note_len = (uint32_t)params[6].len;

                /* Count actual step arrows in this chart */
                int step_count = 0;
                int in_comment = 0;
                for (size_t k = 0; k < params[6].len; k++) {
                    char ch = params[6].p[k];
                    if (in_comment) {
                        if (ch == '\n') in_comment = 0;
                        continue;
                    }
                    if (ch == '/' && k + 1 < params[6].len && params[6].p[k + 1] == '/') {
                        in_comment = 1;
                        continue;
                    }
                    if (ch == '1' || ch == '2' || ch == '4' || (ch >= 'a' && ch <= 'h')) {
                        step_count++;
                    }
                }
                d->num_steps = step_count;

                tidy_diff(d);
                song->num_diffs++;
            }
        }
    }

    /* Song.cpp:514-526 -- a chart with no #BPMS runs at 60 BPM from beat 0,
     * and the first segment is always anchored at 0. */
    if (song->timing.num_bpm == 0) {
        song->timing.bpm[0].start_beat = 0.0f;
        song->timing.bpm[0].bpm        = 60.0f;
        song->timing.num_bpm          = 1;
    }
    song->timing.bpm[0].start_beat = 0.0f;

    /* Song.cpp:506-511 */
    if (song->artist[0] == 0)
        copy_str(song->artist, sizeof(song->artist), "Unknown artist");

    return song->num_diffs;
}

/*
 * Decode one difficulty.  PSPMania builds every grid during the initial song
 * scan; on a 32MB handheld that is several megabytes of note grids for charts
 * the player may never open, so the grid is produced here instead.
 */
int sm_song_load_steps(const sm_song_t *song, int diff_idx, sm_steps_t *st)
{
    if (!song || diff_idx < 0 || diff_idx >= song->num_diffs || !song->text)
        return 0;

    const sm_diff_t *d = &song->diffs[diff_idx];
    if (d->note_len == 0)
        return 0;

    memset(st, 0, sizeof(*st));
    st->num_tracks = SM_TRACKS;
    st->difficulty = d->difficulty;
    st->meter      = d->meter;
    copy_str(st->description,     sizeof(st->description),     d->description);
    copy_str(st->author,          sizeof(st->author),          d->author);
    copy_str(st->difficulty_name, sizeof(st->difficulty_name), d->difficulty_name);
    for (int i = 0; i < 10; i++) st->radar[i] = d->radar[i];

    sm_span_t span;
    span.p   = song->text + d->note_off;
    span.len = d->note_len;

    int last = note_payload_last_row(span);
    if (last < 0)
        return 0;                       /* difficulty holds no notes */

    int rows = last + 1;
    if (rows < 64)        rows = 64;
    if (rows > SM_MAX_ROWS) rows = SM_MAX_ROWS;

    steps_alloc(st, rows, SM_TRACKS);
    if (!st->notes || !st->holds) {
        sm_steps_free(st);
        return 0;
    }

    parse_note_data(st, span);
    convert_holds(st);
    return 1;
}

int sm_song_load_file(const char *path, sm_song_t *song)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 0; }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }

    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = 0;

    if (sm_song_parse(buf, got, song) <= 0) {
        sm_song_free(song);
        return 0;
    }
    return 1;
}

void sm_song_free(sm_song_t *song)
{
    if (!song)
        return;
    free(song->text);
    song->text     = NULL;
    song->text_len = 0;
    song->num_diffs = 0;
}
