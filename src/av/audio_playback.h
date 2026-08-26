#ifndef AV_AUDIO_PLAYBACK_H
#define AV_AUDIO_PLAYBACK_H

#include <stdint.h>

typedef struct audio_playback audio_playback_t;

/* Open a PS3 audio port. channels/sample_rate describe the source; the port is
 * opened as stereo @ 48000 (the PS3 native rate). Mono is upmixed; sample-rate
 * conversion is a TODO (assumes 48000 for now). */
audio_playback_t *audio_playback_init(int channels, int sample_rate);

/* Queue interleaved s16 PCM for playback. sample_rate is the source rate; the
 * port runs at 48000, so the data is resampled (linear) here. */
void audio_playback_submit(audio_playback_t *ap, const int16_t *pcm,
                           int samples, int channels, int sample_rate);

void audio_playback_destroy(audio_playback_t *ap);

#endif /* AV_AUDIO_PLAYBACK_H */
