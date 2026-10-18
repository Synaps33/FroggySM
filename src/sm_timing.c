#include "sm_types.h"

/* ---------------------------------------------------------------------------
 * TimingData port -- faithful reimplementation of
 *   TimingData.cpp:78-85   GetBPMAtBeat
 *   TimingData.cpp:97-155  GetBeatAndBPSFromElapsedTime
 *   TimingData.cpp:158-195 GetElapsedTimeFromBeat
 *
 * NOTE: the SF2000/GB300 core links against -lc only (no libm), so the trig
 * needed by the scroll effects is provided locally.
 * ------------------------------------------------------------------------- */

#define SM_PI 3.14159265358979323846f

float sm_bpm_at_beat(const sm_timing_t *t, float beat)
{
    float bpm = 60.0f;
    for (int i = 0; i < t->num_bpm; i++) {
        if (t->bpm[i].start_beat <= beat)
            bpm = t->bpm[i].bpm;
        else
            break;
    }
    return bpm;
}

/* TimingData.cpp:158-195
 *
 *   t = -beat0Offset
 *   stops with startBeat < fBeat contribute their full duration, up front
 *   BPM segments clamp their contribution and consume the beat span
 *
 * The `>=` on stops is deliberate in the original: a stop's beat sits *before*
 * the stop begins.
 */
float sm_time_from_beat(const sm_timing_t *t, float beat)
{
    float time = -t->beat0_offset;

    for (int j = 0; j < t->num_stop; j++) {
        if (t->stop[j].start_beat >= beat)
            break;
        time += t->stop[j].seconds;
    }

    for (int i = 0; i < t->num_bpm; i++) {
        int last = (i == t->num_bpm - 1);
        float bps = t->bpm[i].bpm / 60.0f;

        if (last) {
            time += beat / bps;
        } else {
            float beats = t->bpm[i + 1].start_beat - t->bpm[i].start_beat;
            float take  = (beat < beats) ? beat : beats;
            time += take / bps;
            beat  -= beats;
        }
        if (beat <= 0.0f)
            return time;
    }
    return time;
}

/* TimingData.cpp:97-155 -- seconds -> beat (and beats-per-second).
 * Stops are resolved inside each BPM segment; landing inside a stop returns
 * that stop's beat, which is how the engine freezes the field. */
float sm_beat_from_time(const sm_timing_t *t, float elapsed, float *out_bps)
{
    float beat = 0.0f;
    float bps  = 10.0f;

    elapsed += t->beat0_offset;

    for (int i = 0; i < t->num_bpm; i++) {
        float start_this = t->bpm[i].start_beat;
        int   first = (i == 0);
        int   last  = (i == t->num_bpm - 1);
        float start_next = last ? 40000.0f : t->bpm[i + 1].start_beat;
        float seg_bps    = t->bpm[i].bpm / 60.0f;

        for (int j = 0; j < t->num_stop; j++) {
            if (!first && start_this >= t->stop[j].start_beat)
                continue;
            if (!last && t->stop[j].start_beat > start_next)
                continue;

            float beats_since = t->stop[j].start_beat - start_this;
            float freeze_at   = beats_since / seg_bps;
            if (freeze_at >= elapsed)
                break;

            elapsed -= t->stop[j].seconds;
            if (freeze_at >= elapsed) {
                if (out_bps) *out_bps = seg_bps;
                return t->stop[j].start_beat;
            }
        }

        float beats_in_seg = start_next - start_this;
        float secs_in_seg  = beats_in_seg / seg_bps;

        if (last || elapsed <= secs_in_seg) {
            beat = start_this + elapsed * seg_bps;
            bps  = seg_bps;
            break;
        }
        elapsed -= secs_in_seg;
    }

    if (out_bps) *out_bps = bps;
    return beat;
}

/* ------------------------------------------------------------------------- */

int sm_beat_to_row(float beat)
{
    return (int)(beat * (float)SM_ROWS_PER_BEAT + 0.5f);
}

float sm_row_to_beat(int row)
{
    return (float)row / (float)SM_ROWS_PER_BEAT;
}

float sm_scale(float value, float in_min, float in_max, float out_min, float out_max)
{
    if (in_max == in_min)
        return out_min;
    return out_min + (value - in_min) * (out_max - out_min) / (in_max - in_min);
}

float sm_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float sm_sinf(float x)
{
    /* Range-reduce into (-pi, pi] */
    float k = x * (1.0f / (2.0f * SM_PI));
    k = (float)(int)k;
    if (k < 0.0f) k -= 1.0f;
    x -= k * 2.0f * SM_PI;
    if (x > SM_PI)  x -= 2.0f * SM_PI;
    if (x < -SM_PI) x += 2.0f * SM_PI;

    /* Taylor series; |error| < 1e-6 across the reduced range. */
    float x2 = x * x;
    return x * (1.0f + x2 * (-0.1666666667f + x2 * (0.0083333333f
              + x2 * (-0.0001984127f + x2 * 0.0000027557f))));
}

float sm_cosf(float x)
{
    return sm_sinf(x + 1.5707963268f);
}