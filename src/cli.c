#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 256

/* Valores por defecto: los del enunciado. */
#define DEF_PAGE_SIZE 4096u
#define DEF_PHYS_SIZE (256u * 1024u)   /* 256 KB = 64 marcos */
#define DEF_POLICY    "fifo"

/* ------------------------------------------------------------- argumentos */

/* log2 de una potencia de dos; 0 si v no lo es. */
static int ilog2_pow2(uint32_t v, int *bits)
{
    int b = 0;
    if (v == 0 || (v & (v - 1)) != 0) return 0;
    while (v > 1) { v >>= 1; b++; }
    *bits = b;
    return 1;
}

/* Acepta decimal (4096) y hexadecimal (0x1000). */
static int parse_u32(const char *s, uint32_t *out)
{
    char         *end;
    unsigned long v;

    if (!s || !*s) return 0;
    v = strtoul(s, &end, 0);
    if (*end != '\0') return 0;
    *out = (uint32_t)v;
    return 1;
}

void cli_usage(const char *prog)
{
    fprintf(stderr,
        "uso: %s <archivo> [opciones]\n"
        "  --policy=fifo|lru|clock   politica de reemplazo (def. %s)\n"
        "  --phys=BYTES              memoria fisica        (def. %u)\n"
        "  --page=BYTES              tamano de pagina      (def. %u)\n"
        "  --verbose                 traza cada traduccion y fallo\n"
        "  --csv                     solo una linea CSV con el resumen\n",
        prog, DEF_POLICY, DEF_PHYS_SIZE, DEF_PAGE_SIZE);
}

int cli_parse_args(int argc, char **argv, Config *cfg)
{
    int i, remaining;

    memset(cfg, 0, sizeof *cfg);
    cfg->page_size = DEF_PAGE_SIZE;
    cfg->phys_size = DEF_PHYS_SIZE;
    cfg->policy    = DEF_POLICY;

    for (i = 1; i < argc; i++) {
        char *a = argv[i];

        if (strncmp(a, "--policy=", 9) == 0) {
            cfg->policy = a + 9;
        } else if (strncmp(a, "--phys=", 7) == 0) {
            if (!parse_u32(a + 7, &cfg->phys_size)) return 0;
        } else if (strncmp(a, "--page=", 7) == 0) {
            if (!parse_u32(a + 7, &cfg->page_size)) return 0;
        } else if (strcmp(a, "--verbose") == 0) {
            cfg->verbose = 1;
        } else if (strcmp(a, "--csv") == 0) {
            cfg->csv = 1;
        } else if (a[0] == '-') {
            fprintf(stderr, "error: opcion desconocida '%s'\n", a);
            return 0;
        } else if (!cfg->input_file) {
            cfg->input_file = a;
        } else {
            fprintf(stderr, "error: solo se admite un archivo de entrada\n");
            return 0;
        }
    }

    if (!cfg->input_file) return 0;

    if (!ilog2_pow2(cfg->page_size, &cfg->offset_bits) ||
        cfg->offset_bits < 6 || cfg->offset_bits > 20) {
        fprintf(stderr, "error: --page debe ser potencia de 2 entre 64 y 1 MB\n");
        return 0;
    }
    /* Los bits que sobran del VPN se reparten entre los dos niveles.
     * Con paginas de 4 KB: 32 - 12 = 20 -> PT1 10 bits, PT2 10 bits. */
    remaining     = VA_BITS - cfg->offset_bits;
    cfg->pt1_bits = remaining / 2 + remaining % 2;
    cfg->pt2_bits = remaining / 2;

    cfg->num_frames = (int)(cfg->phys_size / cfg->page_size);
    if (cfg->num_frames < 1) {
        fprintf(stderr, "error: --phys debe alcanzar para al menos una pagina\n");
        return 0;
    }
    return 1;
}

/* --------------------------------------------------- archivo de comandos */

/* Ejecuta una linea ya tokenizada. Devuelve 0 si el comando se entendio. */
static int run_command(VMM *vm, const Config *cfg, char *op, char *a1, char *a2,
                       int lineno)
{
    uint32_t addr, bytes, base;
    uint32_t value;
    unsigned got;
    int      quiet = cfg->csv;

    if (strcmp(op, "alloc") == 0) {
        if (!parse_u32(a1, &bytes)) goto badargs;
        if (vmm_alloc(vm, bytes, &base) == 0 && !quiet)
            printf("alloc %u bytes -> VA 0x%08x (%u paginas)\n", bytes, base,
                   (bytes + cfg->page_size - 1) / cfg->page_size);

    } else if (strcmp(op, "write") == 0) {
        /* El resultado se imprime despues de ejecutar, para que en modo
         * --verbose la traza del fallo y de la traduccion salga antes. */
        if (!parse_u32(a1, &addr) || !parse_u32(a2, &value)) goto badargs;
        if (vmm_write(vm, addr, value) == 0 && !quiet)
            printf("write 0x%08x = %u\n", addr, value);

    } else if (strcmp(op, "read") == 0) {
        if (!parse_u32(a1, &addr)) goto badargs;
        if (vmm_read(vm, addr, &got) == 0 && !quiet)
            printf("read  0x%08x -> %u\n", addr, got);

    } else if (strcmp(op, "free") == 0) {
        if (!parse_u32(a1, &addr)) goto badargs;
        if (vmm_free(vm, addr) == 0 && !quiet)
            printf("free  0x%08x\n", addr);

    } else {
        fflush(stdout);
        fprintf(stderr, "linea %d: comando desconocido '%s'\n", lineno, op);
        return -1;
    }
    return 0;

badargs:
    fflush(stdout);
    fprintf(stderr, "linea %d: argumentos invalidos para '%s'\n", lineno, op);
    return -1;
}

int cli_run_file(VMM *vm, const Config *cfg)
{
    char  line[LINE_MAX_LEN];
    FILE *f = fopen(cfg->input_file, "r");
    int   lineno = 0;

    if (!f) {
        fprintf(stderr, "error: no se pudo abrir '%s'\n", cfg->input_file);
        return 1;
    }

    while (fgets(line, sizeof line, f)) {
        char *op, *a1, *a2, *hash;
        lineno++;

        hash = strchr(line, '#');           /* comentarios hasta fin de linea */
        if (hash) *hash = '\0';

        op = strtok(line, " \t\r\n");
        if (!op) continue;                  /* linea en blanco */
        a1 = strtok(NULL, " \t\r\n");
        a2 = strtok(NULL, " \t\r\n");

        run_command(vm, cfg, op, a1, a2, lineno);
    }

    fclose(f);
    return 0;
}
