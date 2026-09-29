# Lab_mem_virtual — Simulador de memoria virtual con paginación

Simulador de gestión de memoria virtual basada en paginación, con **tabla de
páginas de dos niveles**, traducción VA→PA, manejo de fallos de página y
**tres políticas de reemplazo intercambiables** (FIFO, LRU y CLOCK).

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
- **Sin swap**: la página expulsada se descarta, y toda página que se carga
  —por primera vez o de nuevo— empieza en ceros. Consecuencia: un `read` a una
  página que fue expulsada después de escribirla devuelve `0`. El simulador
  mide la **política de reemplazo** (fallos, reemplazos, hit rate), y esas
  métricas no dependen de conservar el contenido. El bit `dirty` se mantiene en
  el PTE y se cuentan las **páginas sucias expulsadas**: las que un SO real
  tendría que escribir a disco.
- **Acceso a una VA no reservada**: se reporta como "segmentation fault
  simulado" en `stderr`, se contabiliza aparte y **no** entra en el hit rate;
  el simulador continúa con la siguiente línea.

## Arquitectura

### En una frase

Es un **monolito modular organizado por capas**: se compila en **un solo
ejecutable** (`vmsim`), pero por dentro está dividido en capas con
dependencias en un solo sentido, y la política de reemplazo es un
**componente enchufable** (patrón *Strategy*) que se entrega por inyección de
dependencias.

- **¿Monolito?** Sí, en el sentido de que es un solo programa, un solo proceso,
  sin servicios separados ni red de por medio. Para un simulador de este
  tamaño es lo correcto.
- **¿Monolito "espagueti"?** No. Ningún archivo hace de todo: cada capa tiene
  una responsabilidad y solo conoce a las capas de abajo.
- **¿Microservicios o plugins cargados en tiempo de ejecución?** No. Las
  políticas se enlazan en el mismo ejecutable y se eligen con `--policy`.

### Las capas

```
 CAPA 4  Arranque       main.c         construye la política, la inyecta, corre, destruye
            │
 CAPA 3  Entrada        cli.c/h        argv -> Config; lee la traza y despacha comandos
            │
 CAPA 2  Núcleo         vmm.c/h        traducción VA->PA, fallos de página, estadísticas
            │
            ├──────────────┬──────────────┬─────────────────────┐
 CAPA 1  Servicios   pagetable.c/h   frames.c/h        replacer.h  (INTERFAZ)
                     tabla 2 niveles memoria física          ▲
                                                             │ implementan
                                                  ┌──────────┼──────────┐
         Estrategias                           fifo.c      lru.c     clock.c
                                                  └── framelist.c/h ──┘
                                                   (lista compartida)
            │
 CAPA 0  Base           config.h       parámetros de la máquina y decodificación de la VA
```

| Capa | Archivos | Responsabilidad | Conoce a |
|---|---|---|---|
| 4 · Arranque | `main.c` | Armar el programa y ejecutarlo | `cli`, `replacer` (factory) |
| 3 · Entrada | `cli.c/h` | Traducir texto (argv y traza) a llamadas | `vmm`, `config` |
| 2 · Núcleo | `vmm.c/h` | Traducir direcciones y resolver fallos | `pagetable`, `frames`, **la interfaz** `replacer.h` |
| 1 · Servicios | `pagetable`, `frames` | Estructuras de datos de la máquina | `config` |
| 1 · Estrategias | `fifo`, `lru`, `clock` | Elegir la víctima | `replacer.h`, `framelist` |
| 0 · Base | `config.h` | Parámetros y operaciones de bits | nadie |

**Regla de dependencia:** cada capa solo incluye a las de abajo, nunca a las
de arriba, y no hay ciclos. `frames` y `pagetable` no saben que existe el
`vmm`; el `vmm` no sabe que existe la `cli`. Esto se puede comprobar leyendo
los `#include` de cada archivo.

### Inyección de dependencias: se inyecta solo lo que puede cambiar

El núcleo (`vmm`) necesita tres piezas. La regla para decidir cuáles se le
entregan desde afuera es: **se inyecta solo lo que tiene más de una
implementación.**

| Pieza que usa el VMM | Implementaciones | ¿Cómo la obtiene? |
|---|---|---|
| Política de reemplazo | 3 (FIFO, LRU, CLOCK) | **Inyectada** en `vmm_create()` |
| Tabla de páginas | 1 | La crea el VMM por dentro |
| Memoria física | 1 | La crea el VMM por dentro |

Por eso el arranque completo es una sola línea:

```c
vm = vmm_create(&cfg, replacer_create(cfg.policy, cfg.num_frames));
```

1. `cfg.policy` es el texto de `--policy=lru`.
2. `replacer_create()` (en `replacer.c`, la *factory*) es el **único lugar del
   programa** que traduce ese texto a una política concreta.
3. `vmm_create()` recibe la política ya construida —esa es la inyección— y
   crea por su cuenta la tabla de páginas y la memoria física.

### Cómo funciona el enchufe

En C, una interfaz se hace con un `struct` de punteros a función (una
*vtable*) más un estado privado:

```c
struct Replacer {
    void (*on_map)   (void *st, int frame);  /* se cargó una página         */
    void (*on_access)(void *st, int frame);  /* se usó una página (acierto) */
    int  (*evict)    (void *st);             /* ¿a quién saco?              */
    void (*on_unmap) (void *st, int frame);  /* se liberó un marco          */
    void (*destroy)  (void *st);
    void       *state;                        /* datos propios de la política */
    const char *name;
};
```

Cada política rellena esos punteros con sus propias funciones. Cuando la
memoria se llena, el núcleo simplemente llama:

```c
frame = vm->rep->evict(vm->rep->state);
```

sin saber cuál política hay detrás. `vmm.c` no nombra FIFO, LRU ni CLOCK en
ninguna parte.

### Qué se gana con esta organización

- **Agregar una política no toca el núcleo.** Basta un `.c` nuevo con las
  cinco funciones y una línea en `replacer_create()`. Ni `vmm.c` ni `main.c`
  cambian, así que no hay riesgo de romper la traducción de direcciones.
- **La comparación es justa.** Las tres políticas corren sobre exactamente el
  mismo núcleo; cualquier diferencia en hit rate viene solo de la política.
- **Cada pieza se entiende sola.** Para entender FIFO basta leer `fifo.c`
  (menos de 60 líneas); para entender la traducción basta `vmm.c` y
  `pagetable.c`.
- **`main` queda mínimo.** Solo arma y ejecuta; no contiene lógica.

### Quién libera qué

`vmm_create()` **toma posesión** de la política que recibe: `vmm_destroy()`
libera la política, la memoria física y la tabla de páginas. Si `vmm_create()` falla —por ejemplo, porque la política
pedida no existe— libera lo que haya recibido y devuelve `NULL`, así que
`main` solo tiene que comprobar ese `NULL`.

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
