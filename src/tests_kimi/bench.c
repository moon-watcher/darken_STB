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

static void *st_darken_walk(struct MyComponent *c)
{
    c->x++;
    return DARKEN_LOOP;
}

/* Estado que transiciona una sola vez y luego queda en idle */
static void *st_darken_once_then_walk(struct MyComponent *c)
{
    return st_darken_walk;
}

/* ============================================================================
 * ESTADOS PARA DARKEN2.H (void, sin retorno)
 * ============================================================================ */
static void st_bbb_loop(struct MyComponent *c)  { (void)c; }
static void st_bbb_walk(struct MyComponent *c)
{
    c->x++;
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
static void *st_dk_walk(struct MyComponent *c)
{
    c->x++;
    return DARKEN_LOOP;
}
static void *st_dk_delete(void *d) { (void)d; return DARKEN_DELETE; }
static void *st_dk_once_then_walk(void *d)
{
    (void)d;
    return st_dk_walk;
}
static void *st_dk_mixed(struct MyComponent *c)
{
    c->x++;
    if (c->x % 5 == 0) return DARKEN_PAUSE;
    if (c->x % 7 == 0) return DARKEN_DELETE;
    return DARKEN_LOOP;
}

/* ============================================================================
 * ESTADOS BBB2 (reciben data, sin retorno)
 * ============================================================================ */

static void st_bb_loop(void *d) { (void)d; }
static void st_bb_walk(struct MyComponent *c)
{
    c->x++;
}

/* Autodestruccion inline usando BBB_DATA_GET_ENTITY + kill_fast */
static void st_bb_selfkill(struct MyComponent *c)
{
    c->x++;
    if (c->health == 0)
    {
        bbb_entity e = BBB_DATA_GET_ENTITY(c);
        bbb_entity_kill_fast(e);   /* macro, 0 overhead de comprobaciones */
    }
}

/* Mixed inline: pause o kill desde el propio callback */
static void st_bb_mixed(struct MyComponent *c)
{
    c->x++;
    bbb_entity e = BBB_DATA_GET_ENTITY(c);
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

void kimi_benchmarks(void)
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