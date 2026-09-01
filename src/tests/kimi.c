/*
 * benchmarks_compare.c
 * Comparativa darken.h (v1 state-machine) vs darken2.h (v2 API explicita)
 * Para SGDK / Motorola 68000 / Mega Drive
 */

#include <genesis.h>
#include "../darken.h"
#include "../darken2.h"   /* renombrado desde bbb2.h para claridad */

/* ============================================================================
 * CONFIGURACION
 * ============================================================================ */
#define BENCH_REPS_SMALL   2000
#define BENCH_REPS_MEDIUM  500
#define BENCH_REPS_LARGE   250
#define BENCH_REPS_SWAP    50000

static volatile u32 g_frameCounter = 0;
static void bench_vblank(void) { g_frameCounter++; }
static u32 bench_start(void)   { return g_frameCounter; }
static u32 bench_elapsed(u32 s){ return g_frameCounter - s; }

/* Payload tipico de juego: posicion + salud + flags */
struct MyComponent
{
    int x, y;
    uint8_t health;
    uint8_t pad[3];   /* alineacion a 4 para no penalizar a ninguno */
};

/* ============================================================================
 * ESTADOS PARA DARKEN.H (retornan siguiente estado)
 * ============================================================================ */
static void *st_darken_loop(void *d)  { (void)d; return DARKEN_LOOP; }
static void *st_darken_delete(void *d){ (void)d; return DARKEN_DELETE; }
static void *st_darken_pause(void *d) { (void)d; return DARKEN_PAUSE; }

static void *st_darken_walk(void *d)
{
    struct MyComponent *c = (struct MyComponent *)d;
    c->x++;
    return DARKEN_LOOP;
}

/* Estado que transiciona una sola vez y luego queda en idle */
static void *st_darken_once_then_walk(void *d)
{
    (void)d;
    return st_darken_walk;
}

/* ============================================================================
 * ESTADOS PARA DARKEN2.H (void, sin retorno)
 * ============================================================================ */
static void st_bbb_loop(void *d)  { (void)d; }
static void st_bbb_walk(void *d)
{
    struct MyComponent *c = (struct MyComponent *)d;
    c->x++;
}

/* ============================================================================
 * BENCHMARK 1: Creacion + Reset masivo (32 entidades)
 * ============================================================================ */
static void bench_create_reset_32_darken(void)
{
    DARKEN_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);

    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SMALL; ++r) {
        for (u16 i = 0; i < 32; ++i) DARKEN_SPAWN(&m);
        darken_reset(&m);
    }
    u32 f = bench_elapsed(t0);
    kprintf("[DARKEN] create+reset 32x%d: %ld frames", BENCH_REPS_SMALL, f);
}

static void bench_create_reset_32_bbb(void)
{
    BBB_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);

    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SMALL; ++r) {
        for (u16 i = 0; i < 32; ++i) BBB_SPAWN(&m);
        bbb_reset(&m);
    }
    u32 f = bench_elapsed(t0);
    kprintf("[BBB2]   create+reset 32x%d: %ld frames", BENCH_REPS_SMALL, f);
}

/* ============================================================================
 * BENCHMARK 2: Update puro LOOP (32 entidades, 2000 reps)
 * Mide el overhead post-call de darken.h vs la llamada directa de bbb2
 * ============================================================================ */
static void bench_update_loop_32_darken(void)
{
    DARKEN_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    for (u16 i = 0; i < 32; ++i) {
        darken_entity e = DARKEN_SPAWN(&m);
        e->update = st_darken_loop;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SMALL; ++r) darken_update(&m);
    u32 f = bench_elapsed(t0);
    kprintf("[DARKEN] update LOOP 32x%d: %ld frames", BENCH_REPS_SMALL, f);
}

static void bench_update_loop_32_bbb(void)
{
    BBB_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    for (u16 i = 0; i < 32; ++i) {
        bbb_entity e = BBB_SPAWN(&m);
        e->update = st_bbb_loop;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SMALL; ++r) bbb_update(&m);
    u32 f = bench_elapsed(t0);
    kprintf("[BBB2]   update LOOP 32x%d: %ld frames", BENCH_REPS_SMALL, f);
}

/* ============================================================================
 * BENCHMARK 3: Update con transicion de estado (darken.h)
 * En bbb2 simulamos el cambio manual fuera del loop para comparar coste
 * ============================================================================ */
static void bench_update_transition_darken(void)
{
    DARKEN_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    for (u16 i = 0; i < 32; ++i) {
        darken_entity e = DARKEN_SPAWN(&m);
        e->update = st_darken_once_then_walk; /* cambia a walk en 1er frame */
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SMALL; ++r) darken_update(&m);
    u32 f = bench_elapsed(t0);
    kprintf("[DARKEN] update+transition 32x%d: %ld frames", BENCH_REPS_SMALL, f);
}

static void bench_update_transition_bbb(void)
{
    BBB_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    bbb_entity ents[32];
    for (u16 i = 0; i < 32; ++i) {
        ents[i] = BBB_SPAWN(&m);
        ents[i]->update = st_bbb_walk; /* lo ponemos directo; sin transicion */
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SMALL; ++r) {
        /* Simulamos la transicion manual en la primera iteracion */
        if (r == 0) {
            for (u16 i = 0; i < 32; ++i) ents[i]->update = st_bbb_walk;
        }
        bbb_update(&m);
    }
    u32 f = bench_elapsed(t0);
    kprintf("[BBB2]   update+manualTransition 32x%d: %ld frames", BENCH_REPS_SMALL, f);
}

/* ============================================================================
 * BENCHMARK 4: Autodestruccion masiva (cada entidad se mata sola)
 * darken.h: return DARKEN_DELETE
 * bbb2:     no puede, hacemos delete externo post-update
 * ============================================================================ */
static void bench_selfkill_darken(void)
{
    DARKEN_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    for (u16 i = 0; i < 32; ++i) {
        darken_entity e = DARKEN_SPAWN(&m);
        e->update = st_darken_delete;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SMALL; ++r) {
        for (u16 i = 0; i < 32; ++i) DARKEN_SPAWN(&m);
        /* en el primer update mueren todas por retornar DELETE */
        darken_update(&m);
    }
    u32 f = bench_elapsed(t0);
    kprintf("[DARKEN] self-kill 32x%d: %ld frames", BENCH_REPS_SMALL, f);
}

static void bench_selfkill_bbb(void)
{
    BBB_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SMALL; ++r) {
        bbb_entity ents[32];
        for (u16 i = 0; i < 32; ++i) ents[i] = BBB_SPAWN(&m);
        /* bbb2 no puede autodestruirse desde el callback; borramos desde fuera */
        for (u16 i = 0; i < 32; ++i) bbb_entity_delete(ents[i]);
    }
    u32 f = bench_elapsed(t0);
    kprintf("[BBB2]   external-kill 32x%d: %ld frames", BENCH_REPS_SMALL, f);
}

/* ============================================================================
 * BENCHMARK 5: Escalabilidad — 128 y 256 entidades, update puro
 * ============================================================================ */
static void bench_update_128_darken(void)
{
    DARKEN_POOL_DECLARE(st, 128, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    for (u16 i = 0; i < 128; ++i) {
        darken_entity e = DARKEN_SPAWN(&m);
        e->update = st_darken_walk;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_MEDIUM; ++r) darken_update(&m);
    kprintf("[DARKEN] update 128x%d: %ld frames", BENCH_REPS_MEDIUM, bench_elapsed(t0));
}

static void bench_update_128_bbb(void)
{
    BBB_POOL_DECLARE(st, 128, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    for (u16 i = 0; i < 128; ++i) {
        bbb_entity e = BBB_SPAWN(&m);
        e->update = st_bbb_walk;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_MEDIUM; ++r) bbb_update(&m);
    kprintf("[BBB2]   update 128x%d: %ld frames", BENCH_REPS_MEDIUM, bench_elapsed(t0));
}

static void bench_update_256_darken(void)
{
    DARKEN_POOL_DECLARE(st, 256, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    for (u16 i = 0; i < 256; ++i) {
        darken_entity e = DARKEN_SPAWN(&m);
        e->update = st_darken_walk;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_LARGE; ++r) darken_update(&m);
    kprintf("[DARKEN] update 256x%d: %ld frames", BENCH_REPS_LARGE, bench_elapsed(t0));
}

static void bench_update_256_bbb(void)
{
    BBB_POOL_DECLARE(st, 256, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    for (u16 i = 0; i < 256; ++i) {
        bbb_entity e = BBB_SPAWN(&m);
        e->update = st_bbb_walk;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_LARGE; ++r) bbb_update(&m);
    kprintf("[BBB2]   update 256x%d: %ld frames", BENCH_REPS_LARGE, bench_elapsed(t0));
}

/* ============================================================================
 * BENCHMARK 6: Pausa / Resume masivo
 * ============================================================================ */
static void bench_pause_resume_darken(void)
{
    DARKEN_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    darken_entity ents[32];
    for (u16 i = 0; i < 32; ++i) {
        ents[i] = DARKEN_SPAWN(&m);
        ents[i]->update = st_darken_loop;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SMALL; ++r) {
        for (u16 i = 0; i < 32; ++i) darken_entity_pause(ents[i]);
        for (u16 i = 0; i < 32; ++i) darken_entity_resume(ents[i]);
    }
    kprintf("[DARKEN] pause+resume 32x%d: %ld frames", BENCH_REPS_SMALL, bench_elapsed(t0));
}

static void bench_pause_resume_bbb(void)
{
    BBB_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    bbb_entity ents[32];
    for (u16 i = 0; i < 32; ++i) {
        ents[i] = BBB_SPAWN(&m);
        ents[i]->update = st_bbb_loop;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SMALL; ++r) {
        for (u16 i = 0; i < 32; ++i) bbb_entity_pause(ents[i]);
        for (u16 i = 0; i < 32; ++i) bbb_entity_resume(ents[i]);
    }
    kprintf("[BBB2]   pause+resume 32x%d: %ld frames", BENCH_REPS_SMALL, bench_elapsed(t0));
}

/* ============================================================================
 * BENCHMARK 7: Swap puro (stress del reordering)
 * ============================================================================ */
static void bench_swap_darken(void)
{
    DARKEN_POOL_DECLARE(st, 2, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    darken_entity a = DARKEN_SPAWN(&m);
    darken_entity b = DARKEN_SPAWN(&m);
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SWAP; ++r) {
        /* Forzamos swap manual via pause/resume o delete+spawn */
        darken_entity_pause(a);
        darken_entity_resume(a);
    }
    kprintf("[DARKEN] swap stress x%d: %ld frames", BENCH_REPS_SWAP, bench_elapsed(t0));
    (void)b;
}

static void bench_swap_bbb(void)
{
    BBB_POOL_DECLARE(st, 2, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    bbb_entity a = BBB_SPAWN(&m);
    bbb_entity b = BBB_SPAWN(&m);
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SWAP; ++r) {
        bbb_entity_pause(a);
        bbb_entity_resume(a);
    }
    kprintf("[BBB2]   swap stress x%d: %ld frames", BENCH_REPS_SWAP, bench_elapsed(t0));
    (void)b;
}

/* ============================================================================
 * BENCHMARK 8: "Apply" condicional — borrar pares durante iteracion
 * ============================================================================ */
static void bench_apply_delete_darken(void)
{
    DARKEN_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SMALL; ++r) {
        for (u16 i = 0; i < 32; ++i) {
            darken_entity e = DARKEN_SPAWN(&m);
            e->tag = i;
        }
        uint16_t idx = 0;
        while (idx < m.size) {
            if ((m.pool[idx]->tag % 2) == 0) darken_entity_delete(m.pool[idx]);
            else ++idx;
        }
        darken_reset(&m);
    }
    kprintf("[DARKEN] apply-delete-pares 32x%d: %ld frames", BENCH_REPS_SMALL, bench_elapsed(t0));
}

static void bench_apply_delete_bbb(void)
{
    BBB_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SMALL; ++r) {
        for (u16 i = 0; i < 32; ++i) {
            bbb_entity e = BBB_SPAWN(&m);
            e->tag = i;
        }
        uint16_t idx = 0;
        while (idx < m.size) {
            if ((m.pool[idx]->tag % 2) == 0) bbb_entity_delete(m.pool[idx]);
            else ++idx;
        }
        bbb_reset(&m);
    }
    kprintf("[BBB2]   apply-delete-pares 32x%d: %ld frames", BENCH_REPS_SMALL, bench_elapsed(t0));
}

/* ============================================================================
 * BENCHMARK 9: Update con mix de estados (loop, pause, delete)
 * Stress realista: 20% se pausan, 20% se borran, 60% loop
 * ============================================================================ */
static void *st_darken_mixed(void *d)
{
    struct MyComponent *c = (struct MyComponent *)d;
    c->x++;
    if (c->x % 5 == 0) return DARKEN_PAUSE;
    if (c->x % 7 == 0) return DARKEN_DELETE;
    return DARKEN_LOOP;
}

static void bench_mixed_stress_darken(void)
{
    DARKEN_POOL_DECLARE(st, 64, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    for (u16 i = 0; i < 64; ++i) {
        darken_entity e = DARKEN_SPAWN(&m);
        e->update = st_darken_mixed;
        ((struct MyComponent *)e->data)->x = i;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_MEDIUM; ++r) {
        /* recreamos cada rep para tener siempre 64 al inicio */
        if (r > 0) {
            darken_reset(&m);
            for (u16 i = 0; i < 64; ++i) {
                darken_entity e = DARKEN_SPAWN(&m);
                e->update = st_darken_mixed;
                ((struct MyComponent *)e->data)->x = i;
            }
        }
        darken_update(&m);
    }
    kprintf("[DARKEN] mixed-stress 64x%d: %ld frames", BENCH_REPS_MEDIUM, bench_elapsed(t0));
}

/* En bbb2 no hay sentinelas; simulamos la logica manual post-update */
static void bench_mixed_stress_bbb(void)
{
    BBB_POOL_DECLARE(st, 64, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    bbb_entity ents[64];
    for (u16 i = 0; i < 64; ++i) {
        ents[i] = BBB_SPAWN(&m);
        ents[i]->update = st_bbb_walk;
        ((struct MyComponent *)ents[i]->data)->x = i;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_MEDIUM; ++r) {
        if (r > 0) {
            bbb_reset(&m);
            for (u16 i = 0; i < 64; ++i) {
                ents[i] = BBB_SPAWN(&m);
                ents[i]->update = st_bbb_walk;
                ((struct MyComponent *)ents[i]->data)->x = i;
            }
        }
        bbb_update(&m);
        /* Logica que en darken.h vive dentro del estado */
        for (u16 i = 0; i < m.size; ) {
            struct MyComponent *c = (struct MyComponent *)m.pool[i]->data;
            if (c->x % 5 == 0) { bbb_entity_pause(m.pool[i]); continue; }
            if (c->x % 7 == 0) { bbb_entity_delete(m.pool[i]); continue; }
            ++i;
        }
    }
    kprintf("[BBB2]   mixed-stress-manual 64x%d: %ld frames", BENCH_REPS_MEDIUM, bench_elapsed(t0));
}

/* ============================================================================
 * BENCHMARK 10: Manager vacio (overhead del loop cero)
 * ============================================================================ */
static void bench_empty_darken(void)
{
    DARKEN_POOL_DECLARE(st, 64, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SMALL; ++r) darken_update(&m);
    kprintf("[DARKEN] empty update 64x%d: %ld frames", BENCH_REPS_SMALL, bench_elapsed(t0));
}

static void bench_empty_bbb(void)
{
    BBB_POOL_DECLARE(st, 64, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS_SMALL; ++r) bbb_update(&m);
    kprintf("[BBB2]   empty update 64x%d: %ld frames", BENCH_REPS_SMALL, bench_elapsed(t0));
}

/* ============================================================================
 * BENCHMARK 11: Memoria y stride (estatico, no mide tiempo)
 * ============================================================================ */
static void bench_memory_overhead(void)
{
    kprintf("========== MEMORIA ==========");
    u16 stride = _DARKEN_ENTITY_STRIDE(sizeof(struct MyComponent));
    kprintf("stride MyComponent: %d bytes/entidad", stride);
    kprintf("sizeof(darken_entity) base: %d", (int)sizeof(struct darken_entity));
    kprintf("sizeof(bbb_entity)   base: %d", (int)sizeof(struct bbb_entity));

    u32 ram = 64 * 1024;
    kprintf("Entidades en 64KB: ~%ld", ram / stride);

    DARKEN_POOL_DECLARE(st32, 32, sizeof(struct MyComponent));
    kprintf("Pool 32:  %d bytes storage", 32 * stride);
    DARKEN_POOL_DECLARE(st128, 128, sizeof(struct MyComponent));
    kprintf("Pool 128: %d bytes storage", 128 * stride);
    kprintf("==============================");
}

/* ============================================================================
 * ORQUESTADOR
 * ============================================================================ */
void darken_run_benchmarks_compare(void)
{
    SYS_setVIntCallback(bench_vblank);

    kprintf("========== BENCHMARKS COMPARATIVOS ==========");

    bench_memory_overhead();


    BLASTEM_PROFIL_START
    // kprintf("--- Creacion / Reset ---");
    bench_create_reset_32_darken();

    // kprintf("--- Update puro (LOOP) ---");
    bench_update_loop_32_darken();

    // kprintf("--- Update con transicion ---");
    bench_update_transition_darken();

    // kprintf("--- Autodestruccion ---");
    bench_selfkill_darken();

    // kprintf("--- Escalabilidad 128 / 256 ---");
    bench_update_128_darken();
    bench_update_256_darken();

    // kprintf("--- Pausa / Resume ---");
    bench_pause_resume_darken();

    // kprintf("--- Swap stress ---");
    bench_swap_darken();

    // kprintf("--- Apply condicional ---");
    bench_apply_delete_darken();

    // kprintf("--- Mixed stress ---");
    bench_mixed_stress_darken();

    // kprintf("--- Empty manager ---");
    bench_empty_darken();
    
    // kprintf("=============================================");
    BLASTEM_PROFIL_END


    BLASTEM_PROFIL_START
    // kprintf("--- Creacion / Reset ---");
    bench_create_reset_32_bbb();

    // kprintf("--- Update puro (LOOP) ---");
    bench_update_loop_32_bbb();

    // kprintf("--- Update con transicion ---");
    bench_update_transition_bbb();

    // kprintf("--- Autodestruccion ---");
    bench_selfkill_bbb();

    // kprintf("--- Escalabilidad 128 / 256 ---");
    bench_update_128_bbb();
    bench_update_256_bbb();

    // kprintf("--- Pausa / Resume ---");
    bench_pause_resume_bbb();

    // kprintf("--- Swap stress ---");
    bench_swap_bbb();

    // kprintf("--- Apply condicional ---");
    bench_apply_delete_bbb();

    // kprintf("--- Mixed stress ---");
    bench_mixed_stress_bbb();

    // kprintf("--- Empty manager ---");
    bench_empty_bbb();
    
    // kprintf("=============================================");
    BLASTEM_PROFIL_END
}












/*
 * benchmarks_compare.c
 * Comparativa darken.h (v1 state-machine) vs darken2.h (v2 API explicita)
 * Para SGDK / Motorola 68000 / Mega Drive
 */

// #include <genesis.h>
// #include "../darken.h"
// #include "../darken2.h"   /* renombrado desde bbb2.h para claridad */


/* ============================================================================
 * EXTENSION 68K: kill rapido sin comprobaciones
 * PRECONDICION: entidad activa (slot < owner->size).
 * Seguro dentro de BBB_FOREACH/bbb_update porque itera hacia atras.
 * ============================================================================ */
#define bbb_entity_kill_fast(ENTITY)                         \
    do                                                       \
    {                                                        \
        bbb_entity _kfe = (ENTITY);                          \
        bbb *_kfc = _kfe->owner;                             \
        _bbb_swap(_kfc->pool, _kfe->slot, --_kfc->size);   \
    } while (0)

// #define _bbb_swap(POOL, I, J)                    \
//     do                                           \
//     {                                            \
//         uint16_t _i = (I);                       \
//         uint16_t _j = (J);                       \
//         if (_i != _j)                            \
//         {                                        \
//             bbb_entity _tmp = (POOL)[_i];        \
//             (POOL)[_i] = (POOL)[_j];             \
//             (POOL)[_j] = _tmp;                   \
//             (POOL)[_i]->slot = _i;               \
//             (POOL)[_j]->slot = _j;               \
//         }                                        \
//     } while (0)


/* ============================================================================
 * MOTOR DE BENCHMARKS — UNA SOLA VARIABLE GLOBAL
 * ============================================================================ */

// static volatile u32 g_frameCounter = 0;

static void vblank_count(void) { g_frameCounter++; }

// static inline u32 bench_start(void)      { return g_frameCounter; }
// static inline u32 bench_elapsed(u32 s)   { return g_frameCounter - s; }

static void bench_report(const char *sys, const char *test,
                         u32 frames, u32 reps, u16 entities)
{
    u32 sl_total = frames * 262U;          /* scanlines NTSC */
    u32 sl_op    = sl_total / reps;
    u32 sl_ent   = sl_total / (reps * entities);
    // kprintf("[%s] %s", sys, test);
    // kprintf("  frames=%ld  sl/op=%ld  sl/ent=%ld", frames, sl_op, sl_ent);
    kprintf("[%s] %s""  frames=%ld  sl/op=%ld  sl/ent=%ld", sys, test, frames, sl_op, sl_ent);
}

static void bench_raw(const char *sys, const char *test, u32 frames)
{
    kprintf("[%s] %s: %ld frames (%ld sl)", sys, test, frames, frames * 262U);
}

/* ============================================================================
 * COMPONENTES
 * ============================================================================ */

// struct MyComponent
// {
//     int x, y;
//     uint8_t health;
//     uint8_t pad[3];
// };

/* ============================================================================
 * ESTADOS DARKEN.H (retornan siguiente estado)
 * ============================================================================ */

static void *st_dk_loop(void *d)  { (void)d; return DARKEN_LOOP; }
static void *st_dk_walk(void *d)
{
    struct MyComponent *c = (struct MyComponent *)d;
    c->x++;
    return DARKEN_LOOP;
}
static void *st_dk_delete(void *d) { (void)d; return DARKEN_DELETE; }
static void *st_dk_once_then_walk(void *d)
{
    (void)d;
    return st_dk_walk;
}
static void *st_dk_mixed(void *d)
{
    struct MyComponent *c = (struct MyComponent *)d;
    c->x++;
    if (c->x % 5 == 0) return DARKEN_PAUSE;
    if (c->x % 7 == 0) return DARKEN_DELETE;
    return DARKEN_LOOP;
}

/* ============================================================================
 * ESTADOS BBB2 (reciben data, sin retorno)
 * ============================================================================ */

static void st_bb_loop(void *d) { (void)d; }
static void st_bb_walk(void *d)
{
    struct MyComponent *c = (struct MyComponent *)d;
    c->x++;
}

/* Autodestruccion inline usando BBB_DATA_GET_ENTITY + kill_fast */
static void st_bb_selfkill(void *d)
{
    struct MyComponent *c = (struct MyComponent *)d;
    c->x++;
    if (c->health == 0)
    {
        bbb_entity e = BBB_DATA_GET_ENTITY(d);
        bbb_entity_kill_fast(e);   /* macro, 0 overhead de comprobaciones */
    }
}

/* Mixed inline: pause o kill desde el propio callback */
static void st_bb_mixed(void *d)
{
    struct MyComponent *c = (struct MyComponent *)d;
    c->x++;
    bbb_entity e = BBB_DATA_GET_ENTITY(d);
    if (c->x % 5 == 0) { bbb_entity_pause(e); return; }
    if (c->x % 7 == 0) { bbb_entity_kill_fast(e); return; }
}

/* ============================================================================
 * BENCHMARKS
 * ============================================================================ */

#define REPS_S  2000
#define REPS_M  500
#define REPS_L  250

/* --- 1. create + reset --- */
static void b_create_reset_dk(void)
{
    DARKEN_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) {
        for (u16 i = 0; i < 32; ++i) DARKEN_SPAWN(&m);
        darken_reset(&m);
    }
    bench_report("DARKEN", "create+reset 32", bench_elapsed(t), REPS_S, 32);
}

static void b_create_reset_bb(void)
{
    BBB_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) {
        for (u16 i = 0; i < 32; ++i) BBB_SPAWN(&m);
        bbb_reset(&m);
    }
    bench_report("BBB2  ", "create+reset 32", bench_elapsed(t), REPS_S, 32);
}

/* --- 2. update puro LOOP --- */
static void b_update_loop_dk(void)
{
    DARKEN_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    for (u16 i = 0; i < 32; ++i) { darken_entity e = DARKEN_SPAWN(&m); e->update = st_dk_loop; }
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) darken_update(&m);
    bench_report("DARKEN", "update LOOP 32", bench_elapsed(t), REPS_S, 32);
}

static void b_update_loop_bb(void)
{
    BBB_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    for (u16 i = 0; i < 32; ++i) { bbb_entity e = BBB_SPAWN(&m); e->update = st_bb_loop; }
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) bbb_update(&m);
    bench_report("BBB2  ", "update LOOP 32", bench_elapsed(t), REPS_S, 32);
}

/* --- 3. update con trabajo --- */
static void b_update_work_dk(void)
{
    DARKEN_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    for (u16 i = 0; i < 32; ++i) { darken_entity e = DARKEN_SPAWN(&m); e->update = st_dk_walk; }
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) darken_update(&m);
    bench_report("DARKEN", "update WORK 32", bench_elapsed(t), REPS_S, 32);
}

static void b_update_work_bb(void)
{
    BBB_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    for (u16 i = 0; i < 32; ++i) { bbb_entity e = BBB_SPAWN(&m); e->update = st_bb_walk; }
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) bbb_update(&m);
    bench_report("BBB2  ", "update WORK 32", bench_elapsed(t), REPS_S, 32);
}

/* --- 4. transicion de estado --- */
static void b_transition_dk(void)
{
    DARKEN_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    for (u16 i = 0; i < 32; ++i) { darken_entity e = DARKEN_SPAWN(&m); e->update = st_dk_once_then_walk; }
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) darken_update(&m);
    bench_report("DARKEN", "transition 32", bench_elapsed(t), REPS_S, 32);
}

static void b_transition_bb(void)
{
    BBB_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    for (u16 i = 0; i < 32; ++i) { bbb_entity e = BBB_SPAWN(&m); e->update = st_bb_walk; }
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) bbb_update(&m);
    bench_report("BBB2  ", "transition 32", bench_elapsed(t), REPS_S, 32);
}

/* --- 5. autodestruccion masiva --- */
static void b_selfkill_dk(void)
{
    DARKEN_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) {
        for (u16 i = 0; i < 32; ++i) { darken_entity e = DARKEN_SPAWN(&m); e->update = st_dk_delete; }
        darken_update(&m);
    }
    bench_report("DARKEN", "self-kill 32", bench_elapsed(t), REPS_S, 32);
}

/* BBB2 con kill_fast inline: una sola pasada, igual que DARKEN */
static void b_selfkill_bb_fast(void)
{
    BBB_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) {
        for (u16 i = 0; i < 32; ++i) {
            bbb_entity e = BBB_SPAWN(&m);
            e->update = st_bb_selfkill;
            ((struct MyComponent *)e->data)->health = (i == 31) ? 0 : 100;
        }
        bbb_update(&m);   /* nacen y mueren en la misma pasada */
    }
    bench_report("BBB2-F", "self-kill 32", bench_elapsed(t), REPS_S, 32);
}

/* BBB2 antiguo (dos pasadas) para comparar */
static void b_selfkill_bb_slow(void)
{
    BBB_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) {
        bbb_entity ents[32];
        for (u16 i = 0; i < 32; ++i) ents[i] = BBB_SPAWN(&m);
        for (u16 i = 0; i < 32; ++i) bbb_entity_delete(ents[i]);
    }
    bench_report("BBB2  ", "external-kill 32", bench_elapsed(t), REPS_S, 32);
}

/* --- 6. pause / resume --- */
static void b_pause_dk(void)
{
    DARKEN_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    darken_entity e[32];
    for (u16 i = 0; i < 32; ++i) { e[i] = DARKEN_SPAWN(&m); e[i]->update = st_dk_loop; }
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) {
        for (u16 i = 0; i < 32; ++i) darken_entity_pause(e[i]);
        for (u16 i = 0; i < 32; ++i) darken_entity_resume(e[i]);
    }
    bench_report("DARKEN", "pause+resume 32", bench_elapsed(t), REPS_S, 32);
}

static void b_pause_bb(void)
{
    BBB_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    bbb_entity e[32];
    for (u16 i = 0; i < 32; ++i) { e[i] = BBB_SPAWN(&m); e[i]->update = st_bb_loop; }
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) {
        for (u16 i = 0; i < 32; ++i) bbb_entity_pause(e[i]);
        for (u16 i = 0; i < 32; ++i) bbb_entity_resume(e[i]);
    }
    bench_report("BBB2  ", "pause+resume 32", bench_elapsed(t), REPS_S, 32);
}

/* --- 7. apply condicional (borrar pares) --- */
static void b_apply_dk(void)
{
    DARKEN_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) {
        for (u16 i = 0; i < 32; ++i) { darken_entity e = DARKEN_SPAWN(&m); e->tag = i; }
        uint16_t idx = 0;
        while (idx < m.size) {
            if ((m.pool[idx]->tag % 2) == 0) darken_entity_delete(m.pool[idx]);
            else ++idx;
        }
        darken_reset(&m);
    }
    bench_report("DARKEN", "apply-del 32", bench_elapsed(t), REPS_S, 32);
}

static void b_apply_bb(void)
{
    BBB_POOL_DECLARE(st, 32, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) {
        for (u16 i = 0; i < 32; ++i) { bbb_entity e = BBB_SPAWN(&m); e->tag = i; }
        uint16_t idx = 0;
        while (idx < m.size) {
            if ((m.pool[idx]->tag % 2) == 0) bbb_entity_delete(m.pool[idx]);
            else ++idx;
        }
        bbb_reset(&m);
    }
    bench_report("BBB2  ", "apply-del 32", bench_elapsed(t), REPS_S, 32);
}

/* --- 8. mixed stress --- */
static void b_mixed_dk(void)
{
    DARKEN_POOL_DECLARE(st, 64, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_M; ++r) {
        if (r > 0) {
            darken_reset(&m);
            for (u16 i = 0; i < 64; ++i) {
                darken_entity e = DARKEN_SPAWN(&m);
                e->update = st_dk_mixed;
                ((struct MyComponent *)e->data)->x = i;
            }
        } else {
            for (u16 i = 0; i < 64; ++i) {
                darken_entity e = DARKEN_SPAWN(&m);
                e->update = st_dk_mixed;
                ((struct MyComponent *)e->data)->x = i;
            }
        }
        darken_update(&m);
    }
    bench_report("DARKEN", "mixed 64", bench_elapsed(t), REPS_M, 64);
}

/* BBB2 con mixed inline: una sola pasada */
static void b_mixed_bb_fast(void)
{
    BBB_POOL_DECLARE(st, 64, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_M; ++r) {
        if (r > 0) {
            bbb_reset(&m);
            for (u16 i = 0; i < 64; ++i) {
                bbb_entity e = BBB_SPAWN(&m);
                e->update = st_bb_mixed;
                ((struct MyComponent *)e->data)->x = i;
            }
        } else {
            for (u16 i = 0; i < 64; ++i) {
                bbb_entity e = BBB_SPAWN(&m);
                e->update = st_bb_mixed;
                ((struct MyComponent *)e->data)->x = i;
            }
        }
        bbb_update(&m);
    }
    bench_report("BBB2-F", "mixed 64", bench_elapsed(t), REPS_M, 64);
}

/* BBB2 antiguo: dos pasadas */
static void b_mixed_bb_slow(void)
{
    BBB_POOL_DECLARE(st, 64, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_M; ++r) {
        if (r > 0) {
            bbb_reset(&m);
            for (u16 i = 0; i < 64; ++i) {
                bbb_entity e = BBB_SPAWN(&m);
                e->update = st_bb_walk;
                ((struct MyComponent *)e->data)->x = i;
            }
        } else {
            for (u16 i = 0; i < 64; ++i) {
                bbb_entity e = BBB_SPAWN(&m);
                e->update = st_bb_walk;
                ((struct MyComponent *)e->data)->x = i;
            }
        }
        bbb_update(&m);
        for (u16 i = 0; i < m.size; ) {
            struct MyComponent *c = (struct MyComponent *)m.pool[i]->data;
            if (c->x % 5 == 0) { bbb_entity_pause(m.pool[i]); continue; }
            if (c->x % 7 == 0) { bbb_entity_delete(m.pool[i]); continue; }
            ++i;
        }
    }
    bench_report("BBB2  ", "mixed-manual 64", bench_elapsed(t), REPS_M, 64);
}

/* --- 9. empty manager --- */
static void b_empty_dk(void)
{
    DARKEN_POOL_DECLARE(st, 64, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) darken_update(&m);
    bench_raw("DARKEN", "empty 64", bench_elapsed(t));
}

static void b_empty_bb(void)
{
    BBB_POOL_DECLARE(st, 64, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    u32 t = bench_start();
    for (u32 r = 0; r < REPS_S; ++r) bbb_update(&m);
    bench_raw("BBB2  ", "empty 64", bench_elapsed(t));
}

/* --- 10. escalabilidad 128 / 256 --- */
static void b_scale_dk(u16 n, u32 reps, const char *label)
{
    DARKEN_POOL_DECLARE(st, 256, sizeof(struct MyComponent)); /* max 256 */
    darken m = DARKEN_POOL_BIND(st);
    darken_init(&m);
    for (u16 i = 0; i < n; ++i) { darken_entity e = DARKEN_SPAWN(&m); e->update = st_dk_walk; }
    u32 t = bench_start();
    for (u32 r = 0; r < reps; ++r) darken_update(&m);
    bench_report("DARKEN", label, bench_elapsed(t), reps, n);
}

static void b_scale_bb(u16 n, u32 reps, const char *label)
{
    BBB_POOL_DECLARE(st, 256, sizeof(struct MyComponent));
    bbb m = BBB_POOL_BIND(st);
    bbb_init(&m);
    for (u16 i = 0; i < n; ++i) { bbb_entity e = BBB_SPAWN(&m); e->update = st_bb_walk; }
    u32 t = bench_start();
    for (u32 r = 0; r < reps; ++r) bbb_update(&m);
    bench_report("BBB2  ", label, bench_elapsed(t), reps, n);
}


/* ============================================================================
 * ORQUESTADOR
 * ============================================================================ */

void run_kimi_benchmarks(void)
{
    SYS_setVIntCallback(vblank_count);   /* UNICO callback, UNICA variable */
    g_frameCounter = 0;

    kprintf("========== BENCHMARKS ==========");

    b_create_reset_dk();                   b_create_reset_bb();
    b_update_loop_dk();                    b_update_loop_bb();
    b_update_work_dk();                    b_update_work_bb();
    b_transition_dk();                     b_transition_bb();
    b_selfkill_dk();                       b_selfkill_bb_fast();  b_selfkill_bb_slow();
    b_pause_dk();                          b_pause_bb();
    b_apply_dk();                          b_apply_bb();
    b_mixed_dk();                          b_mixed_bb_fast();     b_mixed_bb_slow();
    b_empty_dk();                          b_empty_bb();
    b_scale_dk(128, REPS_M, "update 128"); b_scale_bb(128, REPS_M, "update 128");
    b_scale_dk(256, REPS_L, "update 256"); b_scale_bb(256, REPS_L, "update 256");

    kprintf("========== DONE ==========");
}