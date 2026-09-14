# Lab_mem_virtual — Simulador de memoria virtual con paginación

Simulador de gestión de memoria virtual basada en paginación, con **tabla de
páginas de dos niveles**, traducción VA→PA, manejo de fallos de página, swap
simulado y **tres políticas de reemplazo intercambiables** (FIFO, LRU y CLOCK).

Integrantes: _(completar)_

---

## Compilación

```bash
make
```

Compila con `gcc -Wall -Werror -std=c99 -O2`, **sin una sola advertencia**.
Genera el ejecutable `vmsim`.

En Windows se usa `make` desde Git Bash o MSYS2 con MinGW-w64 en el `PATH`.

## Uso

```bash
./vmsim <archivo> [opciones]

  --policy=fifo|lru|clock   política de reemplazo   (def. fifo)
  --phys=BYTES              memoria física          (def. 262144 = 256 KB)
  --page=BYTES              tamaño de página        (def. 4096)
  --verbose                 traza cada traducción, fallo y reemplazo
  --csv                     imprime solo una línea CSV con el resumen
```

Ejemplos:

```bash
./vmsim tests/t1_basico.trace                              # ejemplo del enunciado
./vmsim tests/t3_locality.trace --policy=lru --phys=16384  # 4 marcos, LRU
./vmsim tests/t2_thrash.trace --policy=clock --verbose     # traza detallada
```

### Targets del Makefile

| Target          | Qué hace                                                           |
|-----------------|--------------------------------------------------------------------|
| `make`          | Compila `vmsim`                                                     |
| `make run`      | Ejecuta el ejemplo del enunciado (`tests/t1_basico.trace`)          |
| `make compare`  | Las 4 trazas × las 3 políticas con 4 marcos, en formato CSV         |
| `make stress`   | Genera una traza de 20 000 accesos y la corre con las 3 políticas   |
| `make valgrind` | Chequeo de fugas (**solo Linux/WSL**)                               |
| `make clean`    | Borra objetos, ejecutables y la traza generada                      |

## Formato del archivo de entrada

Un comando por línea; `#` inicia un comentario. Las direcciones y valores
aceptan decimal (`4096`) o hexadecimal (`0x1000`).

```
alloc <bytes>              reserva bytes de espacio virtual e imprime la VA base
write <virtual_addr> <val> escribe un byte (0-255) en esa dirección virtual
read  <virtual_addr>       lee el byte de esa dirección virtual
free  <virtual_addr>       libera la región que contiene esa dirección
```

Ejemplo (`tests/t1_basico.trace`):

```
alloc 8192
write 0 42
write 4096 99
read 0
read 4096
```

Salida:

```
alloc 8192 bytes -> VA 0x00000000 (2 paginas)
write 0x00000000 = 42
write 0x00001000 = 99
read  0x00000000 -> 42
read  0x00001000 -> 99

===== ESTADISTICAS =====
Politica                 : FIFO
Total de accesos         : 4
Total fallos de pagina   : 2
Hit rate                 : 50.00%
Total reemplazos         : 0
...
```

## Decisiones de diseño

- **Memoria de bytes**: cada dirección virtual apunta a un byte, así que
  `write` acepta valores de 0 a 255. Evita el caso especial de un acceso que
  cruzaría el borde de la página.
- **Paginación bajo demanda**: `alloc` solo reserva páginas *virtuales*
  (marca `allocated` en el PTE); ningún marco físico se ocupa hasta el primer
  acceso, que provoca el fallo de página.
- **Swap simulado**: al expulsar una página sucia, su contenido se copia a un
  bloque de respaldo apuntado por el propio PTE (campo `disk`), igual que un
  número de bloque de disco en un SO real. Al volver a cargarla se restaura, de
  modo que un `read` posterior a un reemplazo devuelve el valor correcto.
- **Acceso a una VA no reservada**: se reporta como "segmentation fault
  simulado" en `stderr`, se contabiliza aparte y **no** entra en el hit rate;
  el simulador continúa con la siguiente línea.

## Arquitectura

```
main.c                 composition root: construye, inyecta, ejecuta y destruye
 └── cli.c/h           argv -> Config; lee el archivo de comandos y despacha
      └── vmm.c/h      traducción VA->PA, fallos de página, swap, estadísticas
           ├── pagetable.c/h   tabla de 2 niveles, PTE, creación dinámica del L2
           ├── frames.c/h      memoria física: bytes + tabla de marcos + free list
           └── replacer.h      INTERFAZ de política (sin implementación)
                ├── fifo.c  ─┐
                ├── lru.c   ─┼─ framelist.c/h (lista intrusiva compartida)
                └── clock.c ─┘
```

Las dependencias van **en un solo sentido**: `frames`, `pagetable` y `replacer`
no saben que existe el `vmm`, y el `vmm` incluye `replacer.h` pero nunca
`fifo.h`, `lru.h` ni `clock.h`. La política se inyecta ya construida en
`vmm_create()`, y el único archivo que conoce los tres constructores es la
factory `replacer_create()` en `replacer.c`.

Consecuencia práctica: **agregar una política nueva no toca `vmm.c` ni
`main.c`** — basta un `.c` que implemente las cinco funciones de la interfaz y
una línea en la factory.

Ver `REPORTE.md` para el detalle de las estructuras de datos, los resultados y
el análisis.

## Verificación de fugas de memoria

```bash
make valgrind      # Linux / WSL
```

Debe reportar `All heap blocks were freed -- no leaks are possible`.

En Windows no hay valgrind ni AddressSanitizer con MinGW; durante el desarrollo
se verificó con una compilación instrumentada que cuenta `malloc`/`free`, con
resultado **0 bloques vivos** al terminar en las tres políticas, en todas las
trazas y también en las rutas de error (política inválida, archivo inexistente,
`free` doble, acceso fuera de rango).
