/* bench_darken_1_3.c — benchmark suite para darken-1.3 (SGDK, 68K)
 *
 * Mismo formato y nombres que las suites de 1.1 y 1.2, para comparación directa.
 * No incluye pause_resume (no existe en 1.3) ni benches de zonas (no aplican).
 */

#include <genesis.h>
#include <stdint.h>
#include "darken-1.3.0_dev.h"

#define BENCH_CAP     64
#define BENCH_TICKS   120

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

static DARKEN_DECLARE(bench_storage, BENCH_CAP, sizeof(bench_payload));
static darken_t g_ctx;

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

void bench_darken_1_3(void)
{
    kprintf("=== darken-1.3 ===");
    g_ctx = DARKEN_BIND(bench_storage);

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

    kprintf("=== end 1.3 ===");
}