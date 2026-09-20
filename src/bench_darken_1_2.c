/* bench_darken_1_2.c — benchmark suite para darken-1.2 (SGDK, 68K) */

#include <genesis.h>
#include <stdint.h>
#include "darken-1.2.0_dev.h"

#define BENCH_CAP     64
#define BENCH_TICKS   120
#define BENCH_ZONES   4
#define BENCH_ZONES8  8

typedef struct {
    s16 x, y;
    s16 vx, vy;
    u16 hp;
    u16 anim;
    u16 state;
    u16 timer;
} bench_payload;  /* 16 bytes */

typedef void *(*cb_fn)(void *);

static u32 g_sink;

/* 1 zona: comparación directa con 1.1 */
static DARKEN_DECLARE(bench_storage, BENCH_CAP, sizeof(bench_payload));
static darken_t g_ctx;

/* 4 zonas: benchmark específico de zonas */
static DARKEN_DECLARE_ZONES(bench_storage_mz, BENCH_CAP, BENCH_ZONES,
                            sizeof(bench_payload));
static darken_t g_ctx_mz;

/* 8 zonas: escalado de set_zone O(zones) */
static DARKEN_DECLARE_ZONES(bench_storage_8z, BENCH_CAP, BENCH_ZONES8,
                            sizeof(bench_payload));
static darken_t g_ctx_8z;

/* ------------------------------------------------------------------ */
/* Callbacks                                                          */
/* ------------------------------------------------------------------ */

static void *cb_increment(void *data)
{
    ((bench_payload *)data)->x++;
    return DARKEN_CONTINUE;
}

static void *cb_delete(void *data)
{
    (void)data;
    return DARKEN_DELETE;
}

static void *cb_destroy(void *data)
{
    ((bench_payload *)data)->y = (s16)0x7FFF;
    return DARKEN_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* Harness                                                            */
/* ------------------------------------------------------------------ */

static void report(const char *name, u32 ops, u32 ticks)
{
    u32 rate_x100 = ticks ? (ops * 100u) / ticks : 0;
    kprintf("%s: %d ops, %d tk, %d.%02d op/tk",
            name, (int)ops, (int)ticks,
            (int)(rate_x100 / 100u), (int)(rate_x100 % 100u));
}

static void setup_full(cb_fn update_cb, cb_fn destroy_cb)
{
    darken_init(&g_ctx);
    for (u16 i = 0; i < BENCH_CAP; i++) {
        darken_entity_t e = DARKEN_SPAWN(&g_ctx);
        e->update  = update_cb;
        e->destroy = destroy_cb;
    }
}

static void setup_full_mz(void)
{
    darken_init(&g_ctx_mz);
    for (u16 i = 0; i < BENCH_CAP; i++) {
        darken_entity_t e = DARKEN_SPAWN_ZONE(&g_ctx_mz, i % BENCH_ZONES);
        e->update  = cb_increment;
        e->destroy = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Benchmarks comunes con 1.1                                         */
/* ------------------------------------------------------------------ */

static void b_init(void)
{
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) { darken_init(&g_ctx); n++; }
    report("init", n, getTick() - t0);
}

static void b_spawn_fill(void)
{
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        darken_init(&g_ctx);
        for (u16 i = 0; i < BENCH_CAP; i++) {
            darken_entity_t e = DARKEN_SPAWN(&g_ctx);
            e->destroy = NULL;
        }
        n++;
    }
    report("spawn_fill", n * BENCH_CAP, getTick() - t0);
}

static void b_setup_full_64(void)
{
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        setup_full(cb_increment, NULL);
        n++;
    }
    report("setup_full_64", n * BENCH_CAP, getTick() - t0);
}

static void b_spawn_delete_cycle(void)
{
    darken_init(&g_ctx);
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        darken_entity_t e = DARKEN_SPAWN(&g_ctx);
        if (e) { e->destroy = NULL; darken_entity_delete(e); }
        n++;
    }
    report("spawn_delete_cycle", n, getTick() - t0);
}

static void b_update_idle_16(void)
{
    darken_init(&g_ctx);
    for (u16 i = 0; i < 16; i++) {
        darken_entity_t e = DARKEN_SPAWN(&g_ctx);
        e->update  = cb_increment;
        e->destroy = NULL;
    }
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) { darken_update(&g_ctx); n++; }
    report("update_idle_16", n * 16, getTick() - t0);
}

static void b_update_idle_64(void)
{
    setup_full(cb_increment, NULL);
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) { darken_update(&g_ctx); n++; }
    report("update_idle_64", n * BENCH_CAP, getTick() - t0);
}

static void b_update_delete_all(void)
{
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        setup_full(cb_delete, NULL);
        darken_update(&g_ctx);
        n++;
    }
    report("update_delete_all", n * BENCH_CAP, getTick() - t0);
}

static void b_reset_destroy(void)
{
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        setup_full(cb_increment, cb_destroy);
        darken_reset(&g_ctx);
        n++;
    }
    report("reset_destroy", n * BENCH_CAP, getTick() - t0);
}

static void b_foreach_scan(void)
{
    setup_full(cb_increment, NULL);
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        u32 acc = 0;
        DARKEN_FOREACH(&g_ctx, {
            DARKEN_DATA(bench_payload, p, _entity);
            acc += (u32)p->x;
        });
        g_sink = acc;
        n++;
    }
    report("foreach_scan", n * BENCH_CAP, getTick() - t0);
}

static void b_query_in_active(void)
{
    setup_full(cb_increment, NULL);
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        u32 acc = 0;
        for (u16 i = 0; i < BENCH_CAP; i++)
            acc += DARKEN_ENTITY_IN_ACTIVE(g_ctx.pool[i]);
        g_sink = acc;
        n++;
    }
    report("query_in_active", n * BENCH_CAP, getTick() - t0);
}

/* ------------------------------------------------------------------ */
/* Específicos de 1.2                                                 */
/* ------------------------------------------------------------------ */

static void b_spawn_fastpath_baseline(void)
{
    /* Cota inferior teórica: mide "init + N puts directos en pool[]"
     * sin pasar por DARKEN_SPAWN. Simula el coste que tendría un atajo
     * "if (ctx->zones == 1) { pool[size++] }" dentro del macro de spawn.
     * NO es una API de darken — es una referencia para saber cuánto
     * podrías ahorrar si añadieses ese shortcut al motor. */
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        darken_init(&g_ctx);
        for (u16 i = 0; i < BENCH_CAP; i++) {
            u16 s = g_ctx.bounds[g_ctx.zones - 1];
            if (s < g_ctx.capacity) {
                darken_entity_t e = g_ctx.pool[s];
                g_ctx.bounds[g_ctx.zones - 1] = s + 1;
                e->destroy = NULL;
            }
        }
        n++;
    }
    report("spawn_fastpath_baseline", n * BENCH_CAP, getTick() - t0);
}

static void b_set_zone_noop(void)
{
    setup_full_mz();
    darken_entity_t e = g_ctx_mz.pool[0];
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        darken_entity_set_zone(e, 0);
        n++;
    }
    report("set_zone_noop", n, getTick() - t0);
}

static void b_set_zone_adjacent_4z(void)
{
    darken_init(&g_ctx_mz);
    darken_entity_t e = DARKEN_SPAWN_ZONE(&g_ctx_mz, 0);
    e->update  = cb_increment;
    e->destroy = NULL;

    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        darken_entity_set_zone(e, 1);
        darken_entity_set_zone(e, 0);
        n++;
    }
    report("set_zone_adjacent_4z", n, getTick() - t0);
}

static void b_set_zone_far_4z(void)
{
    darken_init(&g_ctx_mz);
    darken_entity_t e = DARKEN_SPAWN_ZONE(&g_ctx_mz, 0);
    e->update  = cb_increment;
    e->destroy = NULL;

    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        darken_entity_set_zone(e, 3);
        darken_entity_set_zone(e, 0);
        n++;
    }
    report("set_zone_far_4z", n, getTick() - t0);
}

static void b_set_zone_adjacent_8z(void)
{
    darken_init(&g_ctx_8z);
    darken_entity_t e = DARKEN_SPAWN_ZONE(&g_ctx_8z, 0);
    e->update  = cb_increment;
    e->destroy = NULL;

    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        darken_entity_set_zone(e, 1);
        darken_entity_set_zone(e, 0);
        n++;
    }
    report("set_zone_adjacent_8z", n, getTick() - t0);
}

static void b_set_zone_far_8z(void)
{
    darken_init(&g_ctx_8z);
    darken_entity_t e = DARKEN_SPAWN_ZONE(&g_ctx_8z, 0);
    e->update  = cb_increment;
    e->destroy = NULL;

    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        darken_entity_set_zone(e, 7);
        darken_entity_set_zone(e, 0);
        n++;
    }
    report("set_zone_far_8z", n, getTick() - t0);
}

static void b_entity_zone_query_4z(void)
{
    setup_full_mz();
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        u32 acc = 0;
        for (u16 i = 0; i < BENCH_CAP; i++)
            acc += darken_entity_zone(g_ctx_mz.pool[i]);
        g_sink = acc;
        n++;
    }
    report("entity_zone_query_4z", n * BENCH_CAP, getTick() - t0);
}

static void b_entity_zone_query_8z(void)
{
    darken_init(&g_ctx_8z);
    for (u16 i = 0; i < BENCH_CAP; i++)
        DARKEN_SPAWN_ZONE(&g_ctx_8z, i % BENCH_ZONES8);

    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        u32 acc = 0;
        for (u16 i = 0; i < BENCH_CAP; i++)
            acc += darken_entity_zone(g_ctx_8z.pool[i]);
        g_sink = acc;
        n++;
    }
    report("entity_zone_query_8z", n * BENCH_CAP, getTick() - t0);
}

static void b_count_zone_query(void)
{
    darken_init(&g_ctx_mz);
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        u32 acc = 0;
        for (u16 z = 0; z < BENCH_ZONES; z++)
            acc += darken_count_zone(&g_ctx_mz, z);
        g_sink = acc;
        n++;
    }
    report("count_zone_query", n * BENCH_ZONES, getTick() - t0);
}

static void b_update_zone_one(void)
{
    setup_full_mz();
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        darken_update_zone(&g_ctx_mz, 1);
        n++;
    }
    report("update_zone_one", n * (BENCH_CAP / BENCH_ZONES), getTick() - t0);
}

static void b_spawn_zone0(void)
{
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        darken_init(&g_ctx_mz);
        for (u16 i = 0; i < BENCH_CAP; i++) {
            darken_entity_t e = DARKEN_SPAWN_ZONE(&g_ctx_mz, 0);
            e->destroy = NULL;
        }
        n++;
    }
    report("spawn_zone0", n * BENCH_CAP, getTick() - t0);
}

static void b_spawn_zone3(void)
{
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        darken_init(&g_ctx_mz);
        for (u16 i = 0; i < BENCH_CAP; i++) {
            darken_entity_t e = DARKEN_SPAWN_ZONE(&g_ctx_mz, 3);
            e->destroy = NULL;
        }
        n++;
    }
    report("spawn_zone3", n * BENCH_CAP, getTick() - t0);
}

/* ------------------------------------------------------------------ */
/* Entrada                                                            */
/* ------------------------------------------------------------------ */

void bench_darken_1_2(void)
{
    kprintf("=== darken-1.2 ===");
    g_ctx    = DARKEN_BIND(bench_storage);
    g_ctx_mz = DARKEN_BIND(bench_storage_mz);
    g_ctx_8z = DARKEN_BIND(bench_storage_8z);

    /* Comunes con 1.1 */
    b_init();
    b_spawn_fill();
    b_setup_full_64();
    b_spawn_delete_cycle();
    b_update_idle_16();
    b_update_idle_64();
    b_update_delete_all();
    b_reset_destroy();
    b_foreach_scan();
    b_query_in_active();

    /* Específicos de 1.2 */
    b_spawn_fastpath_baseline();
    b_set_zone_noop();
    b_set_zone_adjacent_4z();
    b_set_zone_far_4z();
    b_set_zone_adjacent_8z();
    b_set_zone_far_8z();
    b_entity_zone_query_4z();
    b_entity_zone_query_8z();
    b_count_zone_query();
    b_update_zone_one();
    b_spawn_zone0();
    b_spawn_zone3();

    kprintf("=== end 1.2 ===");
}