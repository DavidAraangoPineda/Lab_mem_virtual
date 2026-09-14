/*
 * lru.c - Politica LRU (Least Recently Used).
 *
 * La victima es la pagina que lleva mas tiempo sin usarse. Misma lista que
 * FIFO, con una sola diferencia: cada acierto manda el marco al final, asi que
 * el frente siempre es el menos recientemente usado. Aprovecha la localidad
 * temporal, a costa de tocar la estructura en *cada* acceso (en hardware real
 * eso es caro, por eso los SO usan aproximaciones como CLOCK).
 */
#include "replacer.h"
#include "framelist.h"

#include <stdlib.h>

static void lru_on_map(void *st, int frame)
{
    fl_push_back((FrameList *)st, frame);
}

static void lru_on_access(void *st, int frame)
{
    fl_move_back((FrameList *)st, frame);    /* pasa a ser el mas reciente */
}

static int lru_evict(void *st)
{
    return fl_pop_front((FrameList *)st);    /* el menos reciente */
}

static void lru_on_unmap(void *st, int frame)
{
    fl_remove((FrameList *)st, frame);
}

static void lru_destroy(void *st)
{
    fl_destroy((FrameList *)st);
}

Replacer *lru_create(int nframes)
{
    Replacer  *r = calloc(1, sizeof *r);
    FrameList *l = fl_create(nframes);

    if (!r || !l) { free(r); fl_destroy(l); return NULL; }

    r->on_map    = lru_on_map;
    r->on_access = lru_on_access;
    r->evict     = lru_evict;
    r->on_unmap  = lru_on_unmap;
    r->destroy   = lru_destroy;
    r->state     = l;
    r->name      = "LRU";
    return r;
}
