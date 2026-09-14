#include "vmm.h"
#include "pagetable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Region reservada por un alloc; se guarda para poder liberarla con free. */
typedef struct {
    uint32_t base_vpn;
    uint32_t npages;
    int      live;
} Region;

typedef struct {
    unsigned long accesses;     /* read + write validos              */
    unsigned long faults;       /* fallos de pagina                  */
    unsigned long replacements; /* fallos que ademas expulsaron algo */
    unsigned long writebacks;   /* paginas sucias respaldadas a disco */
    unsigned long swapins;      /* paginas restauradas desde disco   */
    unsigned long allocs;
    unsigned long frees;
    unsigned long errors;       /* accesos invalidos                 */
} Stats;

struct VMM {
    Config     cfg;
    Frames    *fr;    /* inyectado */
    Replacer  *rep;   /* inyectado */
    PageTable *pt;    /* propio    */

    Region   *regions;
    int       nregions;
    int       cap_regions;
    uint32_t  next_vpn;    /* bump allocator de paginas virtuales */

    Stats   st;
    clock_t t0;
};

/* ---------------------------------------------------------------- ciclo de vida */

VMM *vmm_create(Frames *fr, const Config *cfg, Replacer *rep)
{
    VMM *vm = calloc(1, sizeof *vm);
    if (!vm) return NULL;

    /* Toma posesion de inmediato: si algo falla (por ejemplo rep == NULL porque
     * la politica pedida no existe), vmm_destroy libera todo y main solo tiene
     * que comprobar un NULL. */
    vm->cfg = *cfg;
    vm->fr  = fr;
    vm->rep = rep;
    if (!fr || !rep) { vmm_destroy(vm); return NULL; }

    vm->pt  = pt_create(cfg);

    vm->cap_regions = 8;
    vm->regions     = calloc((size_t)vm->cap_regions, sizeof *vm->regions);

    if (!vm->pt || !vm->regions) { vmm_destroy(vm); return NULL; }

    vm->t0 = clock();
    return vm;
}

void vmm_destroy(VMM *vm)
{
    if (!vm) return;
    pt_destroy(vm->pt);              /* libera tablas de nivel 2 y bloques de swap */
    frames_destroy(vm->fr);
    if (vm->rep) vm->rep->destroy(vm->rep->state);
    free(vm->rep);
    free(vm->regions);
    free(vm);
}

/* --------------------------------------------------------------------- swap */

/* Respalda el contenido del marco en el "disco" del PTE (se reserva la primera
 * vez que esa pagina se expulsa sucia y se reutiliza despues). */
static void swap_out(VMM *vm, PTE *e, int frame)
{
    if (!e->disk) {
        e->disk = malloc(vm->cfg.page_size);
        if (!e->disk) return;        /* sin respaldo: la pagina volvera en ceros */
    }
    memcpy(e->disk, frames_ptr(vm->fr, frame), vm->cfg.page_size);
    vm->st.writebacks++;
}

/* ------------------------------------------------------- fallos de pagina */

/* Consigue un marco para vpn: uno libre o, si no hay, la victima que elija la
 * politica inyectada. Devuelve 0 en exito. */
static int handle_page_fault(VMM *vm, uint32_t vpn, PTE *e)
{
    uint8_t *page;
    int frame = frames_alloc(vm->fr, vpn);

    if (frame < 0) {
        /* Memoria fisica llena: aqui es donde entra la politica. */
        uint32_t victim_vpn;
        PTE     *victim;

        frame = vm->rep->evict(vm->rep->state);
        if (frame < 0) {
            fprintf(stderr, "error: no hay marco que reemplazar\n");
            return -1;
        }
        victim_vpn = frames_vpn(vm->fr, frame);
        victim     = pt_lookup(vm->pt, victim_vpn, 0);

        if (vm->cfg.verbose)
            printf("    [reemplazo] victima VPN %u (marco %d)%s\n",
                   victim_vpn, frame,
                   (victim && victim->dirty) ? " sucia -> respaldada" : "");

        if (victim) {
            if (victim->dirty) swap_out(vm, victim, frame);
            victim->valid    = 0;
            victim->accessed = 0;
            victim->dirty    = 0;
        }
        vm->st.replacements++;
        frames_reassign(vm->fr, frame, vpn);
    }

    /* Cargar la pagina: desde el respaldo si existe, en ceros la primera vez. */
    page = frames_ptr(vm->fr, frame);
    if (e->disk) {
        memcpy(page, e->disk, vm->cfg.page_size);
        vm->st.swapins++;
    } else {
        memset(page, 0, vm->cfg.page_size);
    }

    e->frame    = (uint32_t)frame;
    e->valid    = 1;
    e->accessed = 1;
    e->dirty    = 0;
    vm->rep->on_map(vm->rep->state, frame);
    return 0;
}

/* ----------------------------------------------------------- traduccion VA->PA */

/* Traduce va y devuelve el puntero al byte fisico. Resuelve el fallo de pagina
 * si hace falta y actualiza los bits del PTE y las estadisticas. */
static uint8_t *translate(VMM *vm, uint32_t va, int is_write)
{
    uint32_t vpn = va_vpn(&vm->cfg, va);
    uint32_t off = va_offset(&vm->cfg, va);
    PTE     *e   = pt_lookup(vm->pt, vpn, 0);

    if (!e || !e->allocated) {
        fflush(stdout);   /* que el error no se adelante a la salida normal */
        fprintf(stderr, "error: segmentation fault simulado en VA 0x%08x "
                        "(pagina no reservada)\n", va);
        vm->st.errors++;
        return NULL;
    }

    vm->st.accesses++;

    if (!e->valid) {
        vm->st.faults++;
        if (vm->cfg.verbose)
            printf("    [fallo de pagina] VPN %u\n", vpn);
        if (handle_page_fault(vm, vpn, e) != 0) return NULL;
    } else {
        vm->rep->on_access(vm->rep->state, (int)e->frame);
    }

    e->accessed = 1;
    if (is_write) e->dirty = 1;

    if (vm->cfg.verbose)
        printf("    VA 0x%08x -> PT1[%u] PT2[%u] off %u -> marco %u "
               "-> PA 0x%08x\n",
               va, vpn_pt1(&vm->cfg, vpn), vpn_pt2(&vm->cfg, vpn), off,
               e->frame, (unsigned)(e->frame * vm->cfg.page_size + off));

    return frames_ptr(vm->fr, (int)e->frame) + off;
}

/* ------------------------------------------------------------------- API */

int vmm_alloc(VMM *vm, uint32_t bytes, uint32_t *base_va_out)
{
    uint32_t npages, i;

    if (bytes == 0) {
        fprintf(stderr, "error: alloc de 0 bytes\n");
        return -1;
    }
    npages = (bytes + vm->cfg.page_size - 1) / vm->cfg.page_size;

    if (npages > max_vpn(&vm->cfg) - vm->next_vpn) {
        fprintf(stderr, "error: espacio virtual agotado\n");
        return -1;
    }

    /* Paginacion bajo demanda: se reservan las paginas virtuales pero no se
     * toca un solo marco hasta el primer acceso. */
    for (i = 0; i < npages; i++) {
        PTE *e = pt_lookup(vm->pt, vm->next_vpn + i, 1);
        if (!e) { fprintf(stderr, "error: sin memoria para la tabla\n"); return -1; }
        e->allocated = 1;
    }

    if (vm->nregions == vm->cap_regions) {
        int    cap = vm->cap_regions * 2;
        Region *r  = realloc(vm->regions, (size_t)cap * sizeof *r);
        if (!r) { fprintf(stderr, "error: sin memoria\n"); return -1; }
        vm->regions     = r;
        vm->cap_regions = cap;
    }
    vm->regions[vm->nregions].base_vpn = vm->next_vpn;
    vm->regions[vm->nregions].npages   = npages;
    vm->regions[vm->nregions].live     = 1;
    vm->nregions++;

    if (base_va_out) *base_va_out = vpn_to_va(&vm->cfg, vm->next_vpn);
    vm->next_vpn += npages;
    vm->st.allocs++;
    return 0;
}

int vmm_write(VMM *vm, uint32_t va, unsigned value)
{
    uint8_t *p;

    if (value > 255) {
        fprintf(stderr, "error: el valor %u no cabe en un byte (0-255)\n", value);
        vm->st.errors++;
        return -1;
    }
    p = translate(vm, va, 1);
    if (!p) return -1;
    *p = (uint8_t)value;
    return 0;
}

int vmm_read(VMM *vm, uint32_t va, unsigned *value_out)
{
    uint8_t *p = translate(vm, va, 0);
    if (!p) return -1;
    if (value_out) *value_out = *p;
    return 0;
}

int vmm_free(VMM *vm, uint32_t va)
{
    uint32_t vpn = va_vpn(&vm->cfg, va);
    uint32_t i;
    int      r;

    for (r = 0; r < vm->nregions; r++) {
        Region *rg = &vm->regions[r];
        if (!rg->live || vpn < rg->base_vpn || vpn >= rg->base_vpn + rg->npages)
            continue;

        for (i = 0; i < rg->npages; i++) {
            PTE *e = pt_lookup(vm->pt, rg->base_vpn + i, 0);
            if (!e) continue;
            if (e->valid) {
                vm->rep->on_unmap(vm->rep->state, (int)e->frame);
                frames_free(vm->fr, (int)e->frame);
            }
            free(e->disk);
            memset(e, 0, sizeof *e);
        }
        rg->live = 0;
        vm->st.frees++;
        return 0;
    }

    fflush(stdout);
    fprintf(stderr, "error: free de una VA no reservada (0x%08x)\n", va);
    vm->st.errors++;
    return -1;
}

/* ------------------------------------------------------------ estadisticas */

static void print_frame_map(const VMM *vm)
{
    int i, n = frames_count(vm->fr);

    printf("\nMapa de marcos fisicos:\n");
    for (i = 0; i < n; i++) {
        if (frames_is_used(vm->fr, i))
            printf("  marco %3d: VPN %-8u (VA 0x%08x)\n", i,
                   frames_vpn(vm->fr, i),
                   vpn_to_va(&vm->cfg, frames_vpn(vm->fr, i)));
        else
            printf("  marco %3d: libre\n", i);
    }
}

void vmm_print_stats(const VMM *vm)
{
    unsigned long n    = vm->st.accesses;
    unsigned long hits = n - vm->st.faults;
    double hit_rate    = (n > 0) ? (100.0 * (double)hits / (double)n) : 0.0;
    double ms          = 1000.0 * (double)(clock() - vm->t0) / CLOCKS_PER_SEC;

    if (vm->cfg.csv) {
        /* Linea unica para 'make compare'. */
        printf("%s,%s,%lu,%lu,%.2f,%lu,%lu\n",
               vm->rep->name, vm->cfg.input_file ? vm->cfg.input_file : "-",
               n, vm->st.faults, hit_rate, vm->st.replacements,
               vm->st.writebacks);
        return;
    }

    if (vm->cfg.verbose) print_frame_map(vm);

    printf("\n===== ESTADISTICAS =====\n");
    printf("Politica                 : %s\n", vm->rep->name);
    printf("Tamano de pagina         : %u bytes\n", vm->cfg.page_size);
    printf("Memoria fisica           : %u bytes (%d marcos)\n",
           vm->cfg.phys_size, vm->cfg.num_frames);
    printf("Total de accesos         : %lu\n", n);
    printf("Total fallos de pagina   : %lu\n", vm->st.faults);
    printf("Hit rate                 : %.2f%%\n", hit_rate);
    printf("Total reemplazos         : %lu\n", vm->st.replacements);
    printf("Escrituras a disco       : %lu\n", vm->st.writebacks);
    printf("Cargas desde disco       : %lu\n", vm->st.swapins);
    printf("Allocs / frees           : %lu / %lu\n", vm->st.allocs, vm->st.frees);
    printf("Accesos invalidos        : %lu\n", vm->st.errors);
    printf("Marcos ocupados al final : %d / %d\n",
           frames_used(vm->fr), frames_count(vm->fr));
    printf("Tablas de nivel 2 vivas  : %d\n", pt_l2_tables(vm->pt));
    printf("Tiempo de simulacion     : %.3f ms\n", ms);
}
