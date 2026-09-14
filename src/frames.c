#include "frames.h"

#include <stdlib.h>
#include <string.h>

struct Frames {
    uint8_t  *mem;        /* memoria fisica contigua: num_frames * page_size */
    uint32_t  page_size;
    int       n;          /* numero de marcos          */
    int       used;       /* marcos ocupados           */
    uint32_t *vpn;        /* VPN alojado en cada marco */
    uint8_t  *busy;       /* 1 si el marco esta ocupado */
    int      *free_stack; /* pila de marcos libres     */
    int       free_top;
};

Frames *frames_create(int num_frames, uint32_t page_size)
{
    int i;
    Frames *f = calloc(1, sizeof *f);
    if (!f) return NULL;

    f->n          = num_frames;
    f->page_size  = page_size;
    f->mem        = calloc((size_t)num_frames, page_size);
    f->vpn        = calloc((size_t)num_frames, sizeof *f->vpn);
    f->busy       = calloc((size_t)num_frames, sizeof *f->busy);
    f->free_stack = calloc((size_t)num_frames, sizeof *f->free_stack);

    if (!f->mem || !f->vpn || !f->busy || !f->free_stack) {
        frames_destroy(f);
        return NULL;
    }
    /* La pila arranca con todos los marcos libres, en orden descendente para
     * que el primer frames_alloc() devuelva el marco 0. */
    for (i = 0; i < num_frames; i++)
        f->free_stack[i] = num_frames - 1 - i;
    f->free_top = num_frames;

    return f;
}

void frames_destroy(Frames *f)
{
    if (!f) return;
    free(f->mem);
    free(f->vpn);
    free(f->busy);
    free(f->free_stack);
    free(f);
}

int frames_alloc(Frames *f, uint32_t vpn)
{
    int idx;
    if (f->free_top == 0) return -1;      /* memoria fisica llena */

    idx = f->free_stack[--f->free_top];
    f->busy[idx] = 1;
    f->vpn[idx]  = vpn;
    f->used++;
    return idx;
}

void frames_reassign(Frames *f, int idx, uint32_t vpn)
{
    f->vpn[idx] = vpn;
}

void frames_free(Frames *f, int idx)
{
    if (idx < 0 || idx >= f->n || !f->busy[idx]) return;
    f->busy[idx] = 0;
    memset(f->mem + (size_t)idx * f->page_size, 0, f->page_size);
    f->free_stack[f->free_top++] = idx;
    f->used--;
}

uint8_t *frames_ptr(Frames *f, int idx)
{
    return f->mem + (size_t)idx * f->page_size;
}

uint32_t frames_vpn(const Frames *f, int idx) { return f->vpn[idx]; }
int frames_is_used(const Frames *f, int idx)  { return f->busy[idx]; }
int frames_used(const Frames *f)              { return f->used; }
int frames_count(const Frames *f)             { return f->n; }
