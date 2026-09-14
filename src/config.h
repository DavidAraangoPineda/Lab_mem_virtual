/*
 * config.h - Parametros de la maquina simulada y decodificacion de la VA.
 *
 * Capa mas baja del diseno: no depende de ningun otro modulo, todos los demas
 * la incluyen. Una direccion virtual de 32 bits se parte asi (pagina de 4 KB):
 *
 *   31          22 21          12 11             0
 *  +--------------+--------------+----------------+
 *  |   PT1 (10b)  |   PT2 (10b)  |  offset (12b)  |
 *  +--------------+--------------+----------------+
 *
 * El VPN (numero de pagina virtual) son los 20 bits altos; PT1 indexa la tabla
 * de nivel 1 y PT2 la de nivel 2. Si se cambia el tamano de pagina por CLI los
 * anchos se recalculan en cli.c, por eso viven en el Config y no en macros.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#define VA_BITS 32

typedef struct {
    uint32_t    page_size;   /* bytes por pagina                 (def. 4096)   */
    uint32_t    phys_size;   /* bytes de memoria fisica          (def. 256 KB) */
    int         num_frames;  /* phys_size / page_size                          */
    int         offset_bits; /* log2(page_size)                  -> 12         */
    int         pt2_bits;    /* ancho del indice de nivel 2      -> 10         */
    int         pt1_bits;    /* ancho del indice de nivel 1      -> 10         */
    const char *policy;      /* "fifo" | "lru" | "clock"                       */
    const char *input_file;  /* archivo de comandos                            */
    int         verbose;     /* traza detallada de cada traduccion             */
    int         csv;         /* resumen en una linea CSV (para make compare)   */
} Config;

/* Decodificacion de la VA. Son static inline: no generan simbolos ni avisos
 * de "funcion sin usar" en las unidades de traduccion que no las llaman. */

static inline uint32_t va_offset(const Config *c, uint32_t va) {
    return va & (c->page_size - 1u);
}

static inline uint32_t va_vpn(const Config *c, uint32_t va) {
    return va >> c->offset_bits;
}

static inline uint32_t vpn_pt1(const Config *c, uint32_t vpn) {
    return vpn >> c->pt2_bits;
}

static inline uint32_t vpn_pt2(const Config *c, uint32_t vpn) {
    return vpn & ((1u << c->pt2_bits) - 1u);
}

static inline uint32_t vpn_to_va(const Config *c, uint32_t vpn) {
    return vpn << c->offset_bits;
}

/* Cantidad de paginas virtuales direccionables: 2^(32 - offset_bits). */
static inline uint32_t max_vpn(const Config *c) {
    return (uint32_t)1 << (VA_BITS - c->offset_bits);
}

#endif /* CONFIG_H */
