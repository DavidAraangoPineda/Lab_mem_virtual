/*
 * main.c - Composition root.
 *
 * Lo unico que hace es construir las dependencias, inyectarlas en el VMM,
 * ejecutar la traza y destruir. Toda la logica vive en las capas de abajo, y
 * cambiar de politica de reemplazo no toca ni una linea de este archivo: la
 * eleccion viaja como texto desde la CLI hasta replacer_create().
 */
#include <stdio.h>

#include "cli.h"
#include "replacer.h"

int main(int argc, char **argv)
{
    Config cfg;
    int    rc;
    VMM   *vm;

    if (!cli_parse_args(argc, argv, &cfg)) { cli_usage(argv[0]); return 1; }

    vm = vmm_create(frames_create(cfg.num_frames, cfg.page_size),  /* fisica  */
                    &cfg,
                    replacer_create(cfg.policy, cfg.num_frames));  /* politica */
    if (!vm) {
        fprintf(stderr, "error: politica '%s' invalida o memoria insuficiente\n",
                cfg.policy);
        return 1;
    }

    rc = cli_run_file(vm, &cfg);
    if (rc == 0) vmm_print_stats(vm);
    vmm_destroy(vm);
    return rc;
}
