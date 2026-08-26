#ifndef AV_JITTER_H
#define AV_JITTER_H

#include "av/video_decoder.h"

/* Bounded FIFO of decoded video frames, used to absorb network jitter before
 * presentation. Frames are copied on push and owned by the buffer until popped;
 * the caller frees a popped frame with jitter_frame_free(). */
typedef struct jitter_buf jitter_buf_t;

jitter_buf_t *jitter_buf_create(int max_frames);
int  jitter_buf_push(jitter_buf_t *jb, const video_frame_t *f);
video_frame_t *jitter_buf_pop(jitter_buf_t *jb); /* NULL if empty */
int  jitter_buf_size(const jitter_buf_t *jb);
void jitter_buf_destroy(jitter_buf_t *jb);
void jitter_frame_free(video_frame_t *f);

#endif /* AV_JITTER_H */
