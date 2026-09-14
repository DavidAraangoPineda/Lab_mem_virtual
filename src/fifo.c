/*
 * fifo.c - Politica FIFO (First In, First Out).
 *
 * La victima es la pagina que lleva mas tiempo cargada, sin importar cuanto se
 * use. Estado: una cola; se encola al cargar y se desencola por el frente.
 * on_access no hace nada, y justamente por eso FIFO puede expulsar una pagina
 * muy caliente solo por ser vieja (y puede sufrir la anomalia de Belady).
 */
#include "replacer.h"
#include "framelist.h"

#include <stdlib.h>

static void fifo_on_map(void *st, int frame)
{
    fl_push_back((FrameList *)st, frame);
}

static void fifo_on_access(void *st, int frame)
{
    (void)st; (void)frame;   /* FIFO ignora los aciertos: esa es su definicion */
}

static int fifo_evict(void *st)
{
    return fl_pop_front((FrameList *)st);    /* el mas antiguo */
}

static void fifo_on_unmap(void *st, int frame)
{
    fl_remove((FrameList *)st, frame);
}

static void fifo_destroy(void *st)
{
    fl_destroy((FrameList *)st);
}

Replacer *fifo_create(int nframes)
{
    Replacer  *r = calloc(1, sizeof *r);
    FrameList *l = fl_create(nframes);

    if (!r || !l) { free(r); fl_destroy(l); return NULL; }

    r->on_map    = fifo_on_map;
    r->on_access = fifo_on_access;
    r->evict     = fifo_evict;
    r->on_unmap  = fifo_on_unmap;
    r->destroy   = fifo_destroy;
    r->state     = l;
    r->name      = "FIFO";
    return r;
}
