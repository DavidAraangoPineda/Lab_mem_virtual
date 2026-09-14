/*
 * frames.h - Memoria fisica: el arreglo de bytes y la tabla de marcos.
 *
 * No sabe nada de tablas de paginas ni de politicas de reemplazo; solo reparte
 * marcos y recuerda que VPN vive en cada uno (dato que el VMM necesita para
 * invalidar el PTE de la victima cuando hay un reemplazo).
 */
#ifndef FRAMES_H
#define FRAMES_H

#include <stdint.h>

typedef struct Frames Frames;

Frames  *frames_create(int num_frames, uint32_t page_size);
void     frames_destroy(Frames *f);

/* Toma un marco libre y lo asocia al vpn dado. Devuelve -1 si no hay libres
 * (es la senal para que el VMM invoque la politica de reemplazo). */
int      frames_alloc(Frames *f, uint32_t vpn);

/* Reasigna un marco ya ocupado a otro vpn (tras expulsar a su inquilino). */
void     frames_reassign(Frames *f, int idx, uint32_t vpn);

void     frames_free(Frames *f, int idx);

uint8_t *frames_ptr(Frames *f, int idx);          /* inicio del marco idx      */
uint32_t frames_vpn(const Frames *f, int idx);    /* VPN alojado en el marco   */
int      frames_is_used(const Frames *f, int idx);
int      frames_used(const Frames *f);
int      frames_count(const Frames *f);

#endif /* FRAMES_H */
