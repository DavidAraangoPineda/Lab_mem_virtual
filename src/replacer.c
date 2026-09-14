/*
 * replacer.c - Factory de politicas.
 *
 * Unico archivo del programa que conoce simultaneamente los tres constructores.
 * Gracias a el, main.c pide una politica por nombre y vmm.c solo ve la interfaz.
 */
#include "replacer.h"

#include <string.h>

Replacer *replacer_create(const char *policy, int nframes)
{
    if (!policy) return NULL;
    if (strcmp(policy, "fifo")  == 0) return fifo_create(nframes);
    if (strcmp(policy, "lru")   == 0) return lru_create(nframes);
    if (strcmp(policy, "clock") == 0) return clock_create(nframes);
    return NULL;
}
