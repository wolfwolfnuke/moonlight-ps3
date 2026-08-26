#include "av/jitter.h"

#include <stdlib.h>
#include <string.h>

typedef struct stored_frame {
    video_frame_t f;
    struct stored_frame *next;
} stored_frame_t;

struct jitter_buf {
    int max;
    int count;
    stored_frame_t *head;
    stored_frame_t *tail;
};

static void frame_copy(video_frame_t *dst, const video_frame_t *src)
{
    dst->width = src->width;
    dst->height = src->height;
    dst->pts = src->pts;
    for (int i = 0; i < 3; i++) {
        dst->linesize[i] = src->linesize[i];
        size_t rows = (i == 0) ? (size_t)src->height : (size_t)src->height / 2;
        size_t n = (size_t)src->linesize[i] * rows;
        dst->y = NULL;
        uint8_t **pp = (i == 0) ? &dst->y : (i == 1 ? &dst->u : &dst->v);
        *pp = malloc(n);
        if (*pp)
            memcpy(*pp, (i == 0) ? src->y : (i == 1 ? src->u : src->v), n);
    }
}

static void frame_free(video_frame_t *f)
{
    free(f->y);
    free(f->u);
    free(f->v);
    f->y = f->u = f->v = NULL;
}

jitter_buf_t *jitter_buf_create(int max_frames)
{
    jitter_buf_t *jb = calloc(1, sizeof(*jb));
    if (!jb)
        return NULL;
    jb->max = max_frames > 0 ? max_frames : 1;
    return jb;
}

int jitter_buf_push(jitter_buf_t *jb, const video_frame_t *f)
{
    if (!jb)
        return -1;

    stored_frame_t *sf = calloc(1, sizeof(*sf));
    if (!sf)
        return -1;
    frame_copy(&sf->f, f);

    /* If full, drop the oldest frame to make room. */
    if (jb->count >= jb->max) {
        stored_frame_t *old = jb->head;
        jb->head = old->next;
        if (!jb->head)
            jb->tail = NULL;
        jb->count--;
        frame_free(&old->f);
        free(old);
    }

    sf->next = NULL;
    if (jb->tail)
        jb->tail->next = sf;
    else
        jb->head = sf;
    jb->tail = sf;
    jb->count++;
    return 0;
}

video_frame_t *jitter_buf_pop(jitter_buf_t *jb)
{
    if (!jb || jb->count == 0)
        return NULL;
    stored_frame_t *sf = jb->head;
    jb->head = sf->next;
    if (!jb->head)
        jb->tail = NULL;
    jb->count--;
    return &sf->f; /* freed by caller via jitter_frame_free */
}

int jitter_buf_size(const jitter_buf_t *jb)
{
    return jb ? jb->count : 0;
}

void jitter_buf_destroy(jitter_buf_t *jb)
{
    if (!jb)
        return;
    stored_frame_t *sf = jb->head;
    while (sf) {
        stored_frame_t *nx = sf->next;
        frame_free(&sf->f);
        free(sf);
        sf = nx;
    }
    free(jb);
}

void jitter_frame_free(video_frame_t *f)
{
    if (!f)
        return;
    stored_frame_t *sf = (stored_frame_t *)((char *)f - offsetof(stored_frame_t, f));
    frame_free(&sf->f);
    free(sf);
}
