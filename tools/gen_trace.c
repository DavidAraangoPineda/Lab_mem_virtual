/*
 * gen_trace.c - Generador de trazas para stress testing.  [BONUS]
 *
 * uso: gen_trace <seq|locality|random> <paginas> <accesos> [semilla]
 *
 *   seq       recorre las paginas en ciclo (el peor caso para FIFO y LRU)
 *   locality  90% de los accesos caen en una ventana de 4 paginas que se
 *             desplaza cada cierto tiempo: imita la localidad de un programa real
 *   random    paginas uniformemente aleatorias (nadie acierta mucho)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE 4096u
#define WINDOW    4

int main(int argc, char **argv)
{
    const char  *mode;
    unsigned     npages, naccesses, i, seed = 1, base = 0;

    if (argc < 4) {
        fprintf(stderr, "uso: %s <seq|locality|random> <paginas> <accesos> [semilla]\n",
                argv[0]);
        return 1;
    }
    mode      = argv[1];
    npages    = (unsigned)strtoul(argv[2], NULL, 0);
    naccesses = (unsigned)strtoul(argv[3], NULL, 0);
    if (argc > 4) seed = (unsigned)strtoul(argv[4], NULL, 0);
    if (npages == 0) { fprintf(stderr, "error: 0 paginas\n"); return 1; }
    srand(seed);

    printf("# generado por gen_trace %s %u %u %u\n", mode, npages, naccesses, seed);
    printf("alloc %u\n", npages * PAGE_SIZE);

    for (i = 0; i < naccesses; i++) {
        unsigned page;

        if (strcmp(mode, "seq") == 0) {
            page = i % npages;
        } else if (strcmp(mode, "locality") == 0) {
            if (i % 200 == 0) base = (unsigned)rand() % npages;   /* la ventana salta */
            page = (rand() % 10 < 9) ? (base + (unsigned)rand() % WINDOW) % npages
                                     : (unsigned)rand() % npages;
        } else {
            page = (unsigned)rand() % npages;
        }

        /* 1 de cada 4 accesos escribe: asi se ejercitan el bit dirty y el swap. */
        if (i % 4 == 0)
            printf("write %u %u\n", page * PAGE_SIZE + (unsigned)rand() % PAGE_SIZE,
                   i % 256);
        else
            printf("read %u\n", page * PAGE_SIZE + (unsigned)rand() % PAGE_SIZE);
    }
    return 0;
}
