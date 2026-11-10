#include "platform/audio.h"

#include "core/vx_core.h"

#include <vectorpie/vp_audio.h>
#include <vectorpie/vp_error.h>

#include <math.h>
#include <stdio.h>

#define MAX_QUEUED (VX_AUDIO_RATE * 12 / 100)   /* 120 ms: more waiting and the oldest is dropped */

static vp_audio *g_audio;
static vp_voice g_stream;

int vx_audio_open(void)
{
    if (vp_audio_open(&g_audio) != VP_OK || !g_audio) {
        fprintf(stderr, "vectrex: no sound: %s\n", vp_last_error());
        g_audio = NULL;
        return -1;
    }
    g_stream = vp_stream_play(g_audio, VX_AUDIO_RATE, 1.0f);
    if (!g_stream) {
        fprintf(stderr, "vectrex: no sound stream\n");
        vp_audio_close(g_audio);
        g_audio = NULL;
        return -1;
    }
    /* 60 ms of silence ahead: the samples come a Vectrex frame (20 ms) at a time */
    static const float quiet[2 * 882] = { 0 };
    for (int k = 0; k < 3; k++) vp_stream_write(g_audio, g_stream, quiet, 882);
    return 0;
}

void vx_audio_close(void)
{
    if (g_audio) vp_audio_close(g_audio);
    g_audio = NULL;
    g_stream = 0;
}

void vx_audio_feed(const int16_t *s, int frames, int channels, float volume, float second)
{
    if (!g_audio || frames <= 0) return;
    int queued = vp_stream_queued(g_audio, g_stream);
    int skip = 0;
    if (queued + frames > MAX_QUEUED) skip = queued + frames - MAX_QUEUED;   /* behind: drop the oldest */
    if (skip >= frames) return;
    float out[2 * 1024];
    const float k = volume / 32768.0f;
    for (int i = skip; i < frames;) {
        int n = 0;
        for (; n < 1024 && i < frames; n++, i++) {
            float v = channels == 2 ? (float)s[2 * i] + second * (float)s[2 * i + 1] : (float)s[i];
            v *= k;
            /* (a soft limit above 0.7: a loud speech module does not clip hard) */
            if (v > 0.7f) v = 0.7f + 0.3f * tanhf((v - 0.7f) / 0.3f);
            else if (v < -0.7f) v = -0.7f - 0.3f * tanhf((-v - 0.7f) / 0.3f);
            out[2 * n] = out[2 * n + 1] = v;
        }
        vp_stream_write(g_audio, g_stream, out, n);
    }
}
