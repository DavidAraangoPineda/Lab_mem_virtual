/*
 * vmm.h - Gestor de memoria virtual: la capa que orquesta todo.
 *
 * Es el unico modulo que combina memoria fisica (Frames), tabla de paginas
 * (PageTable) y politica de reemplazo (Replacer). Recibe las tres dependencias
 * ya construidas en vmm_create(): inyeccion por constructor. No incluye
 * fifo.h/lru.h/clock.h, solo la interfaz replacer.h, de modo que agregar o
 * cambiar una politica no toca este archivo.
 *
 * Toma posesion de lo que se le inyecta: vmm_destroy() libera Frames y Replacer.
 */
#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include "config.h"
#include "frames.h"
#include "replacer.h"

typedef struct VMM VMM;

VMM *vmm_create(Frames *fr, const Config *cfg, Replacer *rep);
void vmm_destroy(VMM *vm);

/* Todas devuelven 0 en exito y -1 en error (imprimiendo el motivo). */
int  vmm_alloc(VMM *vm, uint32_t bytes, uint32_t *base_va_out);
int  vmm_write(VMM *vm, uint32_t va, unsigned value);
int  vmm_read (VMM *vm, uint32_t va, unsigned *value_out);
int  vmm_free (VMM *vm, uint32_t va);

void vmm_print_stats(const VMM *vm);

#endif /* VMM_H */
