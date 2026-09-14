# Reporte de análisis — Simulador de memoria virtual paginada

Integrantes: _(completar)_

Todos los números de este reporte se obtienen ejecutando `make compare` y
`make stress`, o los comandos que se indican en cada tabla.

---

## 1. Estructuras de datos

### 1.1 Dirección virtual (32 bits)

```
 31          22 21          12 11             0
+--------------+--------------+----------------+
|   PT1 (10b)  |   PT2 (10b)  |  offset (12b)  |
+--------------+--------------+----------------+
```

Con páginas de 4 KB el offset ocupa 12 bits y los 20 bits del VPN se reparten en
dos índices de 10 bits. Los anchos no son constantes de compilación: viven en
`Config` (`src/config.h`) y se recalculan si se cambia `--page`, de modo que el
simulador sigue funcionando con páginas de otro tamaño.

### 1.2 Tabla de páginas de dos niveles (`src/pagetable.c`)

```c
struct PageTable {
    PTE **l1;           /* 1024 punteros a tablas de nivel 2 */
    int   l1_entries, l2_entries, tables;
    Config cfg;
};

typedef struct {
    uint32_t frame;      /* marco físico, válido solo si valid == 1        */
    uint8_t  valid;      /* residente en memoria física                    */
    uint8_t  accessed;   /* referenciada (bit de referencia, lo usa CLOCK) */
    uint8_t  dirty;      /* escrita: hay que respaldarla al expulsarla     */
    uint8_t  allocated;  /* reservada por alloc, aunque no esté residente  */
    uint8_t *disk;       /* bloque de respaldo tras ser expulsada (swap)   */
} PTE;                   /* sizeof(PTE) = 16 bytes */
```

El nivel 1 es un arreglo de 1024 punteros creado al arrancar; **cada tabla de
nivel 2 se crea con `calloc` la primera vez que se toca su región de 4 MB**
(`pt_lookup(pt, vpn, 1)`). Ese es el ahorro de las tablas multinivel:

| Esquema                               | Memoria de tablas |
|---------------------------------------|-------------------|
| Tabla lineal: 2²⁰ PTE × 16 B          | **16 MiB** por proceso |
| Dos niveles, 1 región de 4 MB tocada  | 8 KiB (L1) + 16 KiB (L2) = **24 KiB** |

Es decir, ~680× menos memoria para los programas de prueba, que solo usan las
primeras páginas del espacio virtual. El simulador imprime cuántas tablas de
nivel 2 están vivas al final de cada corrida.

Nótese que `disk` (el respaldo de swap) **vive dentro del PTE**, igual que un
número de bloque de disco en un SO real: no hace falta una estructura de swap
aparte y `pt_destroy()` lo libera junto con la tabla.

### 1.3 Memoria física (`src/frames.c`)

Un bloque contiguo de `num_frames × page_size` bytes, más tres arreglos
paralelos indexados por número de marco: `vpn[]` (qué página virtual aloja),
`busy[]` y una pila de marcos libres. `frames_alloc()` es O(1) y devuelve −1
cuando la memoria está llena: **esa es la señal que dispara la política de
reemplazo**. `vpn[]` es lo que permite, al expulsar un marco, encontrar e
invalidar el PTE de la víctima.

### 1.4 La política como dependencia inyectada (`src/replacer.h`)

```c
struct Replacer {
    void (*on_map)   (void *st, int frame);
    void (*on_access)(void *st, int frame);
    int  (*evict)    (void *st);
    void (*on_unmap) (void *st, int frame);
    void (*destroy)  (void *st);
    void       *state;
    const char *name;
};
```

En C una interfaz es una *vtable*: un struct de punteros a función más un estado
opaco. `vmm_create(frames, cfg, replacer)` recibe la política ya construida
(inyección por constructor) y la invoca como `rep->evict(rep->state)` sin saber
cuál es. `src/vmm.c` no incluye `fifo.h`, `lru.h` ni `clock.h`; el único archivo
que conoce los tres constructores es la factory `replacer_create()`.

Por eso las tres políticas se comparan **sobre exactamente el mismo motor de
traducción y de fallos**: cualquier diferencia en las tablas de abajo es
atribuible solo a la política, nunca a otra implementación del simulador.

### 1.5 Lista intrusiva de marcos (`src/framelist.c`)

FIFO y LRU comparten una lista doblemente enlazada cuyos nodos no se asignan con
`malloc`: viven en arreglos `next[]`/`prev[]` indexados por número de marco. Así
insertar, mover al final y sacar del frente son **O(1) exactos**, sin borrado
perezoso ni entradas obsoletas cuando un marco se libera y se vuelve a usar.

---

## 2. Traducción y manejo de fallos

`translate()` en `src/vmm.c`:

1. Descompone la VA en `PT1`, `PT2` y `offset`.
2. `pt_lookup()` baja por los dos niveles. Si la página no fue reservada por un
   `alloc`, se reporta *segmentation fault simulado* y el acceso no cuenta.
3. Si `valid == 0` → **fallo de página**: `handle_page_fault()`.
4. Si `valid == 1` → acierto: se notifica `on_access` a la política.
5. Se actualizan `accessed` y, si es escritura, `dirty`; se devuelve
   `frames_ptr(frame) + offset`.

`handle_page_fault()`:

1. `frames_alloc()`; si hay marco libre, se usa.
2. Si no hay: `rep->evict()` elige la víctima. Si está **sucia**, su contenido se
   copia al bloque `disk` de su PTE (*write-back*) y se cuenta un reemplazo. Se
   invalida su PTE.
3. La página entrante se restaura desde su `disk` si ya existía, o se pone a
   ceros la primera vez.
4. `rep->on_map()` registra el marco en la política.

---

## 3. Políticas de reemplazo implementadas

**FIFO** (`src/fifo.c`) — la víctima es la página que lleva más tiempo cargada.
`on_map` encola al final, `evict` desencola por el frente y **`on_access` no hace
nada**: esa indiferencia ante el uso es exactamente su definición, y la causa de
que expulse páginas calientes solo por ser viejas.

**LRU** (`src/lru.c`) — la víctima es la que lleva más tiempo sin usarse. Misma
lista que FIFO con una sola diferencia: `on_access` manda el marco al final. Es
el mejor aprovechamiento de la localidad temporal, a costa de tocar la estructura
en **cada** acceso (en hardware real eso significaría actualizar una estructura
por cada referencia a memoria: inviable, por eso los SO usan aproximaciones).

**CLOCK** (`src/clock.c`, bonus) — aproximación barata a LRU. Los marcos forman
un anillo recorrido por una manecilla; cada uno tiene un bit de referencia que se
pone en 1 al cargarse o usarse. Al buscar víctima la manecilla avanza: si el bit
está en 1 le da una *segunda oportunidad* (lo pone en 0 y sigue), si está en 0 la
expulsa. Solo necesita un bit por marco, que es justo lo que el hardware real
ofrece gratis.

---

## 4. Resultados

### 4.1 Prueba 1 — `tests/t1_basico.trace` (ejemplo del enunciado)

```
alloc 8192 / write 0 42 / write 4096 99 / read 0 / read 4096
```

| Métrica | Valor |
|---|---|
| Accesos | 4 |
| Fallos de página | 2 |
| Hit rate | 50.00 % |
| Reemplazos | 0 |

Los dos fallos son **obligatorios** (*compulsory misses*): la primera vez que se
toca cada página. Los dos `read` posteriores son aciertos. Con 64 marcos el
resultado es idéntico en las tres políticas: sin presión de memoria, la política
es irrelevante. El programa usa 2 de las 2²⁰ páginas del espacio virtual y
mantiene **una sola** tabla de nivel 2 viva.

### 4.2 Prueba 2 — `tests/t2_thrash.trace` (working set que no cabe)

8 páginas escritas y luego recorridas en ciclo tres veces, con `--phys=16384`
(4 marcos):

| Política | Accesos | Fallos | Hit rate | Reemplazos | Write-backs |
|---|---|---|---|---|---|
| FIFO  | 34 | 33 | 2.94 % | 29 | 8 |
| LRU   | 34 | 33 | 2.94 % | 29 | 8 |
| CLOCK | 34 | 33 | 2.94 % | 29 | 8 |

**Las tres empatan en el peor resultado posible.** Un recorrido cíclico de N
páginas con menos de N marcos es el peor caso de FIFO *y* de LRU: cuando se
vuelve a la página 0, es justo la que se acaba de expulsar. Esto es *thrashing*,
y la conclusión importante es que **ninguna política lo arregla**: el problema es
que el working set no cabe, y la solución es más memoria (o menos concurrencia),
no un algoritmo más listo.

Esta prueba también valida el swap: tras 29 reemplazos, `read 0` sigue
devolviendo 10 y `read 28672` devuelve 17, los valores escritos al principio.
Hay 8 write-backs, uno por página sucia. El `free` final devuelve los marcos a
la lista de libres (`Marcos ocupados al final: 0 / 4`).

### 4.3 Prueba 3 — `tests/t3_locality.trace` (3 páginas calientes + barrido frío)

Las páginas 0, 1 y 2 se acceden constantemente; entre medias se toca una página
fría distinta cada vez. Con 4 marcos el conjunto caliente cabe y sobra un marco
para la fría de turno:

| Política | Accesos | Fallos | Hit rate | Reemplazos | Write-backs |
|---|---|---|---|---|---|
| FIFO  | 43 | 25 | 41.86 % | 21 | 3 |
| **LRU**   | 43 | **13** | **69.77 %** | **9** | **0** |
| CLOCK | 43 | 25 | 41.86 % | 21 | 3 |

**LRU reduce los fallos casi a la mitad** (25 → 13) y los reemplazos de 21 a 9.
La razón: LRU nunca expulsa las páginas calientes porque son siempre las más
recientes, así que solo falla al traer cada página fría. FIFO las expulsa por
antigüedad aunque se estén usando en ese mismo instante, y además tiene que
escribirlas a disco (3 write-backs frente a **0** de LRU: las páginas sucias
calientes nunca salen de memoria).

### 4.4 Hit rate frente a número de marcos (`tests/t3_locality.trace`)

| Marcos | FIFO | LRU | CLOCK |
|---|---|---|---|
| 2 |  0.00 % |  0.00 % |  0.00 % |
| 3 |  6.98 % |  6.98 % |  6.98 % |
| 4 | 41.86 % | **69.77 %** | 41.86 % |
| 5 | 48.84 % | **69.77 %** | 62.79 % |
| 8 | 81.40 % | 81.40 % | 81.40 % |

Tres regímenes muy claros:

- **Muy poca memoria (2–3 marcos)**: ni el conjunto caliente cabe. Todas fallan
  igual; la política no puede hacer nada.
- **Memoria intermedia (4–5 marcos)**: es la única zona donde la política
  importa, y la diferencia es grande (hasta 28 puntos de hit rate).
- **Memoria suficiente (8 marcos)**: todo cabe, quedan solo los 8 fallos
  obligatorios y las tres vuelven a empatar, con **0 reemplazos**.

Esto es lo más importante del laboratorio: *elegir bien la política solo paga en
la zona intermedia*. Fuera de ella, lo que manda es la relación entre el working
set y la memoria disponible.

El caso de 4 marcos explica también una limitación real de CLOCK: como **todas**
las páginas residentes se habían usado desde la última barrida, sus bits de
referencia estaban en 1, la manecilla tuvo que dar una vuelta completa
limpiándolos y terminó expulsando la misma víctima que FIFO. **CLOCK degenera en
FIFO cuando todos los bits de referencia están encendidos.** Con 5 marcos la
presión baja, los bits dejan de estar todos en 1 y CLOCK se separa de FIFO
(62.79 % contra 48.84 %), acercándose a LRU.

### 4.5 Stress test — 20 000 accesos, 64 páginas, patrón con localidad

Generado con `make stress` (`./gen_trace locality 64 20000 1`):

| Marcos | FIFO | LRU | CLOCK |
|---|---|---|---|
|  8 | 83.73 % | **89.06 %** | 87.58 % |
| 16 | 89.16 % | **90.78 %** | 90.67 % |
| 32 | 93.51 % | 93.89 % | **93.94 %** |
| 64 | 99.68 % | 99.68 % | 99.68 % |

Fallos absolutos con 8 marcos: FIFO 3254, CLOCK 2483, LRU 2188; es decir, **LRU
evita un 33 % de los fallos de FIFO**. Con 64 marcos todo el espacio cabe y
quedan exactamente los 64 fallos obligatorios (99.68 %).

El orden esperado LRU ≥ CLOCK > FIFO se cumple en todos los tamaños con presión
de memoria, y CLOCK queda mucho más cerca de LRU que de FIFO: con 16 marcos está
a 0.11 puntos de LRU usando **un solo bit por marco** en lugar de reordenar una
lista en cada acceso. Con 32 marcos CLOCK queda 0.05 puntos por *encima* de LRU;
la diferencia es de una decena de fallos sobre 20 000 accesos, dentro del ruido
del patrón concreto — no es que CLOCK sea mejor que LRU, sino que con esa
holgura de memoria ambas ya aciertan casi siempre.

### 4.6 Anomalía de Belady — `tests/t4_belady.trace`

Secuencia clásica 1,2,3,4,1,2,5,1,2,3,4,5:

| Política | 3 marcos | 4 marcos | ¿Mejora con más memoria? |
|---|---|---|---|
| FIFO  | 9 fallos  | **10 fallos** | ❌ empeora |
| LRU   | 10 fallos | 8 fallos      | ✅ |
| CLOCK | 9 fallos  | **10 fallos** | ❌ empeora |

Reproducimos la **anomalía de Belady**: FIFO falla *más* con *más* memoria. LRU
no puede sufrirla porque es un *algoritmo de pila* (el conjunto de páginas
residentes con N marcos siempre es un subconjunto del que habría con N+1). CLOCK
tampoco es de pila y aquí exhibe la anomalía igual que FIFO, lo que confirma que
es una aproximación a LRU y no LRU.

---

## 5. Comparación teórica

| Criterio | FIFO | LRU | CLOCK | ÓPTIMO (Belady) |
|---|---|---|---|---|
| Información que usa | orden de carga | orden de uso | 1 bit de referencia | el futuro |
| Costo por acierto | **O(1), nada** | O(1) pero toca la lista | poner un bit | — |
| Costo por fallo | O(1) | O(1) | O(n) en el peor caso | O(n) |
| Memoria extra | 1 puntero/marco | 2 punteros/marco | **1 bit/marco** | — |
| ¿Aprovecha la localidad? | no | sí | aproximadamente | sí |
| ¿Anomalía de Belady? | **sí** | no (es de pila) | sí | no |
| ¿Implementable en hardware real? | sí | **no** (caro) | **sí** | no (irrealizable) |

**FIFO vs LRU.** LRU gana siempre que haya localidad temporal, que es el caso de
casi todos los programas reales: en nuestra prueba de localidad pasa de 41.86 % a
69.77 % de hit rate. FIFO solo empata con LRU en dos situaciones: cuando no hay
presión de memoria (nadie expulsa nada) y cuando el patrón es cíclico sin
localidad (ambas dan el peor resultado posible, sección 4.2). A cambio, FIFO es
trivial y su `on_access` no cuesta nada, mientras que LRU necesitaría que el
hardware informara de **cada** referencia a memoria: por eso ningún SO real
implementa LRU exacto.

**Por qué CLOCK es lo que se usa de verdad.** CLOCK conserva casi todo el
beneficio de LRU (87.58 % contra 89.06 % con 8 marcos) con un costo que el
hardware ya paga: el bit de referencia del PTE. Su punto débil quedó medido en la
sección 4.4: bajo mucha presión todos los bits se encienden y degenera en FIFO.
Linux lo mitiga con dos listas (activa e inactiva) y un segundo bit.

**Cota inferior.** El algoritmo ÓPTIMO expulsa la página que se usará más tarde
en el futuro; es irrealizable, pero sirve de referencia: en `t4_belady` con 3
marcos, ÓPTIMO necesita 7 fallos frente a los 9 de FIFO y los 10 de LRU.

---

## 6. Conclusiones

1. La traducción de dos niveles funciona y **paga por sí sola**: 24 KiB de tablas
   frente a los 16 MiB de una tabla lineal, porque los niveles 2 se crean solo
   para las regiones que el programa toca.
2. **El hit rate lo determina primero la relación working set / memoria, y solo
   después la política.** Con 2 o con 8 marcos las tres políticas empatan; la
   política decide únicamente en la zona intermedia.
3. **LRU le gana claramente a FIFO cuando hay localidad** (69.77 % contra
   41.86 %; 33 % menos fallos en el stress test) y además genera menos tráfico a
   disco, porque no expulsa páginas sucias que se están usando.
4. **Ninguna política arregla el thrashing.** Con un recorrido cíclico más grande
   que la memoria, las tres bajan al 2.94 %.
5. **FIFO y CLOCK sufren la anomalía de Belady** (más memoria → más fallos); LRU
   no puede sufrirla por ser un algoritmo de pila.
6. Separar la política detrás de una interfaz inyectada no fue solo higiene de
   diseño: es lo que permite afirmar que estas diferencias vienen de la política
   y de nada más, porque las tres corren sobre el mismo motor de traducción.
