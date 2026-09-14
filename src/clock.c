/*
 * clock.c - Politica CLOCK (segunda oportunidad).  [BONUS]
 *
 * Aproximacion barata a LRU: los marcos forman un anillo recorrido por una
 * manecilla. Cada marco tiene un bit de referencia que se pone en 1 al cargarlo
 * o usarlo. Al buscar victima, la manecilla avanza: si el bit esta en 1 le da
 * una segunda oportunidad (lo pone en 0 y sigue), si esta en 0 la expulsa.
 *
 * Mantiene su propio bit de referencia alimentado por on_map/on_access, de modo
 * que entra en la misma interfaz de 5 funciones que FIFO y LRU sin ampliarla.
 */
#include "replacer.h"

#include <stdlib.h>

typedef struct {
    unsigned char *ref;      /* bit de referencia por marco */
    unsigned char *present;  /* 1 si el marco tiene una pagina cargada */
    int            n;
    int            hand;     /* manecilla del reloj */
} ClockState;

static void clock_on_map(void *st, int frame)
{
    ClockState *c = st;
    c->present[frame] = 1;
    c->ref[frame]     = 1;
}

static void clock_on_access(void *st, int frame)
{
    ClockState *c = st;
    c->ref[frame] = 1;
}

static int clock_evict(void *st)
{
    ClockState *c = st;
    int steps, limit = 2 * c->n;   /* como mucho dos vueltas: la 1a limpia bits */

    for (steps = 0; steps <= limit; steps++) {
        int f = c->hand;
        c->hand = (c->hand + 1) % c->n;

        if (!c->present[f]) continue;
        if (c->ref[f]) { c->ref[f] = 0; continue; }   /* segunda oportunidad */

        c->present[f] = 0;
        return f;
    }
    return -1;   /* no hay ninguna pagina cargada */
}

static void clock_on_unmap(void *st, int frame)
{
    ClockState *c = st;
    c->present[frame] = 0;
    c->ref[frame]     = 0;
}

static void clock_destroy(void *st)
{
    ClockState *c = st;
    if (!c) return;
    free(c->ref);
    free(c->present);
    free(c);
}

Replacer *clock_create(int nframes)
{
    Replacer   *r = calloc(1, sizeof *r);
    ClockState *c = calloc(1, sizeof *c);

    if (!r || !c) { free(r); free(c); return NULL; }

    c->n       = nframes;
    c->ref     = calloc((size_t)nframes, sizeof *c->ref);
    c->present = calloc((size_t)nframes, sizeof *c->present);
    if (!c->ref || !c->present) { clock_destroy(c); free(r); return NULL; }

    r->on_map    = clock_on_map;
    r->on_access = clock_on_access;
    r->evict     = clock_evict;
    r->on_unmap  = clock_on_unmap;
    r->destroy   = clock_destroy;
    r->state     = c;
    r->name      = "CLOCK";
    return r;
}
