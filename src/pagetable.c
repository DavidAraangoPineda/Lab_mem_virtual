#include "pagetable.h"

#include <stdlib.h>

struct PageTable {
    PTE    **l1;          /* 2^pt1_bits punteros a tablas de nivel 2 */
    int      l1_entries;
    int      l2_entries;
    int      tables;      /* tablas de nivel 2 creadas               */
    Config   cfg;
};

PageTable *pt_create(const Config *cfg)
{
    PageTable *pt = calloc(1, sizeof *pt);
    if (!pt) return NULL;

    pt->cfg        = *cfg;
    pt->l1_entries = 1 << cfg->pt1_bits;
    pt->l2_entries = 1 << cfg->pt2_bits;
    pt->l1         = calloc((size_t)pt->l1_entries, sizeof *pt->l1);

    if (!pt->l1) { free(pt); return NULL; }
    return pt;
}

void pt_destroy(PageTable *pt)
{
    int i, j;
    if (!pt) return;

    for (i = 0; i < pt->l1_entries; i++) {
        if (!pt->l1[i]) continue;
        /* Cada PTE puede tener un bloque de swap colgando: se libera aqui
         * para no dejar fugas cuando el programa termina con paginas fuera. */
        for (j = 0; j < pt->l2_entries; j++)
            free(pt->l1[i][j].disk);
        free(pt->l1[i]);
    }
    free(pt->l1);
    free(pt);
}

PTE *pt_lookup(PageTable *pt, uint32_t vpn, int create)
{
    uint32_t i1 = vpn_pt1(&pt->cfg, vpn);
    uint32_t i2 = vpn_pt2(&pt->cfg, vpn);

    if (i1 >= (uint32_t)pt->l1_entries) return NULL;   /* fuera del espacio */

    if (!pt->l1[i1]) {
        if (!create) return NULL;
        pt->l1[i1] = calloc((size_t)pt->l2_entries, sizeof **pt->l1);
        if (!pt->l1[i1]) return NULL;
        pt->tables++;
    }
    return &pt->l1[i1][i2];
}

int pt_l2_tables(const PageTable *pt) { return pt->tables; }
