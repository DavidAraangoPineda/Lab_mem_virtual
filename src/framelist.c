#include "framelist.h"

#include <stdlib.h>

struct FrameList {
    int *next;   /* next[f] / prev[f] = vecinos del marco f, -1 = ninguno */
    int *prev;
    int *in;     /* 1 si el marco esta en la lista                        */
    int  head;
    int  tail;
    int  n;
};

FrameList *fl_create(int nframes)
{
    int i;
    FrameList *l = calloc(1, sizeof *l);
    if (!l) return NULL;

    l->n    = nframes;
    l->head = l->tail = -1;
    l->next = malloc((size_t)nframes * sizeof *l->next);
    l->prev = malloc((size_t)nframes * sizeof *l->prev);
    l->in   = calloc((size_t)nframes, sizeof *l->in);

    if (!l->next || !l->prev || !l->in) { fl_destroy(l); return NULL; }
    for (i = 0; i < nframes; i++) l->next[i] = l->prev[i] = -1;

    return l;
}

void fl_destroy(FrameList *l)
{
    if (!l) return;
    free(l->next);
    free(l->prev);
    free(l->in);
    free(l);
}

void fl_push_back(FrameList *l, int frame)
{
    if (frame < 0 || frame >= l->n || l->in[frame]) return;

    l->prev[frame] = l->tail;
    l->next[frame] = -1;
    if (l->tail >= 0) l->next[l->tail] = frame;
    else              l->head = frame;
    l->tail = frame;
    l->in[frame] = 1;
}

void fl_remove(FrameList *l, int frame)
{
    if (frame < 0 || frame >= l->n || !l->in[frame]) return;

    if (l->prev[frame] >= 0) l->next[l->prev[frame]] = l->next[frame];
    else                     l->head = l->next[frame];

    if (l->next[frame] >= 0) l->prev[l->next[frame]] = l->prev[frame];
    else                     l->tail = l->prev[frame];

    l->next[frame] = l->prev[frame] = -1;
    l->in[frame] = 0;
}

void fl_move_back(FrameList *l, int frame)
{
    if (frame < 0 || frame >= l->n || !l->in[frame]) return;
    if (l->tail == frame) return;            /* ya es el mas reciente */
    fl_remove(l, frame);
    fl_push_back(l, frame);
}

int fl_pop_front(FrameList *l)
{
    int f = l->head;
    if (f < 0) return -1;
    fl_remove(l, f);
    return f;
}
