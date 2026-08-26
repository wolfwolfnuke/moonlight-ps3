#include "av/audio_playback.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <audio/audio.h>

struct audio_playback {
    u32   port;
    u32   data_start;   /* audioDataStart effective address */
    u32   num_blocks;
    u32   block_bytes;  /* bytes per block (256 * 2ch * s16) */
    int16_t *staging;   /* stereo staging buffer (samples per channel) */
    size_t staging_len;
    size_t staging_cap;
};

/* Resample `samples` of interleaved `ch`-channel s16 PCM at `rate` to the
 * port's 48000 stereo, appending into the staging buffer. */
static void stage(audio_playback_t *a, const int16_t *pcm, int samples, int ch, int rate)
{
    if (rate <= 0)
        rate = 48000;

    int out_n = (int)((int64_t)samples * 48000 / rate);
    size_t need = a->staging_len + (size_t)out_n;
    if (need > a->staging_cap) {
        a->staging_cap = need + 1024;
        a->staging = realloc(a->staging, a->staging_cap * 2 * sizeof(int16_t));
    }
    for (int i = 0; i < out_n; i++) {
        double pos = (double)i * rate / 48000.0;
        int i0 = (int)pos;
        int i1 = (i0 + 1 < samples) ? i0 + 1 : samples - 1;
        double frac = pos - i0;
        for (int c = 0; c < 2; c++) {
            double s0, s1;
            if (ch == 1) {
                s0 = pcm[i0]; s1 = pcm[i1];
            } else {
                s0 = pcm[i0 * 2 + (c ? 1 : 0)];
                s1 = pcm[i1 * 2 + (c ? 1 : 0)];
            }
            double v = s0 + (s1 - s0) * frac;
            if (v > 32767.0) v = 32767.0;
            else if (v < -32768.0) v = -32768.0;
            a->staging[a->staging_len * 2 + c] = (int16_t)v;
        }
        a->staging_len++;
    }
}

audio_playback_t *audio_playback_init(int channels, int sample_rate)
{
    (void)channels;
    (void)sample_rate; /* port is always 48000; input is resampled in stage() */

    audio_playback_t *a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;

    if (audioInit() != 0) {
        free(a);
        return NULL;
    }

    audioPortParam param;
    memset(&param, 0, sizeof(param));
    param.numChannels = AUDIO_PORT_2CH;
    param.numBlocks   = AUDIO_BLOCK_16;
    param.attrib      = 0;

    if (audioPortOpen(&param, &a->port) != 0) {
        audioQuit();
        free(a);
        return NULL;
    }

    audioPortConfig cfg;
    audioGetPortConfig(a->port, &cfg);
    a->data_start  = cfg.audioDataStart;
    a->num_blocks  = cfg.numBlocks;
    a->block_bytes = AUDIO_BLOCK_SAMPLES * 2 * (u32)sizeof(int16_t);

    audioPortStart(a->port);
    return a;
}

void audio_playback_submit(audio_playback_t *a, const int16_t *pcm,
                           int samples, int channels, int sample_rate)
{
    if (!a)
        return;

    stage(a, pcm, samples, channels, sample_rate);

    /* Write whole blocks; target the block just consumed by the HW so we never
     * clobber the one currently playing. */
    while (a->staging_len >= AUDIO_BLOCK_SAMPLES) {
        audioPortConfig cfg;
        audioGetPortConfig(a->port, &cfg);
        u32 wb = (cfg.readIndex + a->num_blocks - 1) % a->num_blocks;
        uint8_t *dst = (uint8_t *)(uintptr_t)(a->data_start + wb * a->block_bytes);
        memcpy(dst, a->staging, a->block_bytes);
        memmove(a->staging,
                a->staging + AUDIO_BLOCK_SAMPLES * 2,
                (a->staging_len - AUDIO_BLOCK_SAMPLES) * 2 * sizeof(int16_t));
        a->staging_len -= AUDIO_BLOCK_SAMPLES;
    }
}

void audio_playback_destroy(audio_playback_t *a)
{
    if (!a)
        return;
    audioPortStop(a->port);
    audioPortClose(a->port);
    audioQuit();
    free(a->staging);
    free(a);
}
