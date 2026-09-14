/*
 * cli.h - Capa de entrada: argumentos de linea de comandos y archivo de traza.
 *
 * Depende del VMM (le da ordenes), pero el VMM no sabe que existe.
 */
#ifndef CLI_H
#define CLI_H

#include "config.h"
#include "vmm.h"

/* Llena cfg con los valores por defecto y lo que venga en argv.
 * Devuelve 1 si los argumentos son validos, 0 si no. */
int  cli_parse_args(int argc, char **argv, Config *cfg);

void cli_usage(const char *prog);

/* Ejecuta el archivo de comandos linea a linea. Devuelve 0 en exito. */
int  cli_run_file(VMM *vm, const Config *cfg);

#endif /* CLI_H */
