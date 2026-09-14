/*
 * pagetable.h - Tabla de paginas de dos niveles (estilo x86 simplificado).
 *
 * Nivel 1: arreglo de 2^pt1_bits punteros (1024 con paginas de 4 KB).
 * Nivel 2: arreglo de 2^pt2_bits PTEs, creado *bajo demanda* la primera vez que
 *          se toca una region: ese es el ahorro de memoria de las tablas
 *          multinivel frente a una tabla lineal de 2^20 entradas.
 */
#ifndef PAGETABLE_H
#define PAGETABLE_H

#include <stdint.h>
#include "config.h"

typedef struct {
    uint32_t frame;      /* marco fisico, valido solo si valid == 1           */
    uint8_t  valid;      /* la pagina esta residente en memoria fisica        */
    uint8_t  accessed;   /* referenciada desde la ultima limpieza (usa CLOCK) */
    uint8_t  dirty;      /* escrita: hay que respaldarla al expulsarla        */
    uint8_t  allocated;  /* reservada por alloc (aunque no este residente)    */
    uint8_t *disk;       /* bloque de "disco": respaldo tras ser expulsada    */
} PTE;

typedef struct PageTable PageTable;

PageTable *pt_create(const Config *cfg);
void       pt_destroy(PageTable *pt);

/* Devuelve el PTE del vpn. Con create != 0 crea la tabla de nivel 2 si falta;
 * con create == 0 devuelve NULL cuando esa region nunca se ha tocado. */
PTE       *pt_lookup(PageTable *pt, uint32_t vpn, int create);

/* Numero de tablas de nivel 2 vivas (para la estadistica de memoria de tablas). */
int        pt_l2_tables(const PageTable *pt);

#endif /* PAGETABLE_H */
