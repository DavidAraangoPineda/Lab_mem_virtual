/*
 * replacer.h - INTERFAZ de politica de reemplazo (la dependencia inyectada).
 *
 * En C una "interfaz" es un struct de punteros a funcion (vtable) mas un estado
 * opaco. El VMM recibe un Replacer * en su constructor y llama rep->evict(...)
 * sin saber si por debajo hay una cola FIFO, una lista LRU o el reloj de CLOCK.
 * Este header no incluye nada del VMM: la dependencia va en un solo sentido.
 *
 * Para agregar una politica nueva basta con un .c que exporte su *_create() y
 * una linea en replacer_create(): ni vmm.c ni main.c se tocan.
 */
#ifndef REPLACER_H
#define REPLACER_H

typedef struct Replacer Replacer;

struct Replacer {
    void (*on_map)   (void *st, int frame); /* se cargo una pagina en el marco   */
    void (*on_access)(void *st, int frame); /* acierto: LRU/CLOCK lo usan, FIFO no */
    int  (*evict)    (void *st);            /* elige y devuelve el marco victima */
    void (*on_unmap) (void *st, int frame); /* el marco se libero (free)         */
    void (*destroy)  (void *st);

    void       *state;  /* cola FIFO / lista LRU / anillo + bits de referencia */
    const char *name;   /* "FIFO" | "LRU" | "CLOCK": se imprime en las stats   */
};

Replacer *fifo_create (int nframes);
Replacer *lru_create  (int nframes);
Replacer *clock_create(int nframes);

/* Factory: unico punto del programa que traduce un nombre a una implementacion.
 * Devuelve NULL si la politica no existe. */
Replacer *replacer_create(const char *policy, int nframes);

#endif /* REPLACER_H */
