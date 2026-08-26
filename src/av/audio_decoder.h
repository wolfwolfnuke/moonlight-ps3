#ifndef AV_AUDIO_DECODER_H
#define AV_AUDIO_DECODER_H

#include <stdint.h>
#include <stddef.h>

/* A decoded PCM block, 16-bit signed, interleaved across channels. */
typedef struct audio_frame {
    int16_t *pcm;
    int samples;     /* per channel */
    int channels;
    int sample_rate;
} audio_frame_t;

typedef struct audio_decoder audio_decoder_t;
struct audio_decoder {
    int (*submit)(audio_decoder_t *ad, const uint8_t *data, size_t len, int64_t pts);
    int (*pump)(audio_decoder_t *ad, audio_frame_t *out);
    void (*destroy)(audio_decoder_t *ad);
    void *priv;
};

/* Create an AAC-LC decoder. Returns NULL if decoding is unavailable. */
audio_decoder_t *audio_decoder_create_aac(void);

#endif /* AV_AUDIO_DECODER_H */
