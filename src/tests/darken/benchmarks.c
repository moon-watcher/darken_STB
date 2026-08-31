#include <genesis.h>

#include "../../darken.h"
#include "benchmarks.h"

struct MyComponent
{
    int x, y;
    uint8_t health;
};

static volatile u32 g_frameCounter = 0;
static void darken_bench_vblank_cb(void) { g_frameCounter++; }
static u32 bench_start(void) { return g_frameCounter; }
static u32 bench_frames_elapsed(u32 start) { return g_frameCounter - start; }
#define BENCH_REPS 2000



static void darken_bench_create_destroy(void)
{
    DARKEN_POOL_DECLARE(m_storage222, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(m_storage222);
    darken_init(&m);
    
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS; ++r)
    {
        for (u16 i = 0; i < 32; ++i)
            DARKEN_SPAWN(&m);
        darken_reset(&m);
    }
    u32 frames = bench_frames_elapsed(t0);
    kprintf("create+reset 32 entidades x%d reps: %ld frames", BENCH_REPS, frames);
}

static void *darken_bench_state_fn(void *data)
{
    (void)data;
    return DARKEN_LOOP;
}


static void darken_bench_update(void)
{
    DARKEN_POOL_DECLARE(m_storage3, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(m_storage3);
    darken_init(&m);
    for (u16 i = 0; i < 32; ++i)
    {
        darken_entity e = DARKEN_SPAWN(&m);
        e->update = darken_bench_state_fn;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS; ++r)
        darken_update(&m);
    u32 frames = bench_frames_elapsed(t0);
    kprintf("darken_update, 32 entidades x%d reps: %ld frames", BENCH_REPS, frames);
}

static void darken_bench_apply(void)
{
    DARKEN_POOL_DECLARE(m_storage, 32, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(m_storage);
    darken_init(&m);
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS; ++r)
    {
        for (u16 i = 0; i < 32; ++i)
        {
            darken_entity e = DARKEN_SPAWN(&m);
            e->tag = i;
        }
        // DARKEN_MANAGER_APPLY(&m, (ENTITY->tag % 2) == 0, darken_entity_delete);
        DARKEN_FOREACH(&m, {
            if (_entity->tag % 2 == 0)
                darken_entity_delete(_entity);
        });
        darken_reset(&m);
    }
    u32 frames = bench_frames_elapsed(t0);
    kprintf("create32+applyAll(borrar pares)+reset x%d reps: %ld frames", BENCH_REPS, frames);
}

static void darken_bench_create_destroy_128(void)
{
    DARKEN_POOL_DECLARE(m_storage, 128, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(m_storage);
    darken_init(&m);
    u32 t0 = bench_start();
    for (u32 r = 0; r < 500; ++r)
    {
        for (u16 i = 0; i < 128; ++i)
            DARKEN_SPAWN(&m);
        darken_reset(&m);
    }
    u32 frames = bench_frames_elapsed(t0);
    kprintf("create+reset 128 entidades x500 reps: %ld frames", frames);
}

static void darken_bench_create_destroy_256(void)
{
    DARKEN_POOL_DECLARE(m_storage, 256, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(m_storage);
    darken_init(&m);
    u32 t0 = bench_start();
    for (u32 r = 0; r < 250; ++r)
    {
        for (u16 i = 0; i < 256; ++i)
            DARKEN_SPAWN(&m);
        darken_reset(&m);
    }
    u32 frames = bench_frames_elapsed(t0);
    kprintf("create+reset 256 entidades x250 reps: %ld frames", frames);
}

static void darken_bench_update_128(void)
{
    DARKEN_POOL_DECLARE(m_storage, 128, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(m_storage);
    darken_init(&m);
    for (u16 i = 0; i < 128; ++i)
    {
        darken_entity e = DARKEN_SPAWN(&m);
        e->update = darken_bench_state_fn;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < 500; ++r)
        darken_update(&m);
    u32 frames = bench_frames_elapsed(t0);
    kprintf("darken_update, 128 entidades x500 reps: %ld frames", frames);
}

static void darken_bench_update_256(void)
{
    DARKEN_POOL_DECLARE(m_storage, 256, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(m_storage);
    darken_init(&m);
    for (u16 i = 0; i < 256; ++i)
    {
        darken_entity e = DARKEN_SPAWN(&m);
        e->update = darken_bench_state_fn;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < 250; ++r)
        darken_update(&m);
    u32 frames = bench_frames_elapsed(t0);
    kprintf("darken_update, 256 entidades x250 reps: %ld frames", frames);
}

static void darken_bench_swap(void)
{
    DARKEN_POOL_DECLARE(m_storage, 2, sizeof(struct MyComponent));
    darken m = DARKEN_POOL_BIND(m_storage);
    darken_init(&m);
    darken_entity a = DARKEN_SPAWN(&m);
    darken_entity b = DARKEN_SPAWN(&m);
    u32 t0 = bench_start();
    (void)a;
    (void)b;
    u32 frames = bench_frames_elapsed(t0);
    kprintf("darken_entity_swap x50000: %ld frames (comentado)", frames);
}

static void darken_bench_memory_overhead(void)
{
    kprintf("========== MEMORIA ==========");
    u16 stride16 = _DARKEN_ENTITY_STRIDE(16);
    u16 stride32 = _DARKEN_ENTITY_STRIDE(32);
    u16 stride1 = _DARKEN_ENTITY_STRIDE(1);
    u16 stride9 = _DARKEN_ENTITY_STRIDE(9);
    kprintf("sizeof(darken_entity) base: %d bytes", (int)sizeof(struct darken_entity));
    kprintf("stride payload=1:  %d bytes/entidad", stride1);
    kprintf("stride payload=9:  %d bytes/entidad (impar)", stride9);
    kprintf("stride payload=16: %d bytes/entidad", stride16);
    kprintf("stride payload=32: %d bytes/entidad", stride32);
    DARKEN_POOL_DECLARE(m32_storage, 32, sizeof(struct MyComponent));
    darken m32  = DARKEN_POOL_BIND(m32_storage);
    darken_init(&m32);
    u32 bytes32 = 32 * _DARKEN_ENTITY_STRIDE(sizeof(struct MyComponent));
    kprintf("Manager 32 entidades (payload %d): %ld bytes en storage", (int)sizeof(struct MyComponent), bytes32);
    DARKEN_POOL_DECLARE(m128_storage, 128, sizeof(struct MyComponent));
    darken m128  = DARKEN_POOL_BIND(m128_storage);
    darken_init(&m128);
    u32 bytes128 = 128 * _DARKEN_ENTITY_STRIDE(sizeof(struct MyComponent));
    kprintf("Manager 128 entidades (payload %d): %ld bytes en storage", (int)sizeof(struct MyComponent), bytes128);
    u16 overhead = _DARKEN_ENTITY_STRIDE(sizeof(struct MyComponent)) - sizeof(struct MyComponent);
    kprintf("Overhead por entidad: %d bytes (header + padding)", overhead);
    u32 ramAvailable = 64 * 1024;
    u32 entidadesEn64k = ramAvailable / _DARKEN_ENTITY_STRIDE(sizeof(struct MyComponent));
    kprintf("Entidades de MyComponent que caben en 64 KB: ~%ld", entidadesEn64k);
    kprintf("==============================");
}

void darken_run_benchmarks(void)
{
    SYS_setVIntCallback(darken_bench_vblank_cb);
    darken_bench_memory_overhead();
    kprintf("========== BENCHMARKS ==========");
    darken_bench_create_destroy();
    darken_bench_update();
    darken_bench_apply();
    darken_bench_create_destroy_128();
    darken_bench_create_destroy_256();
    darken_bench_update_128();
    darken_bench_update_256();
    darken_bench_swap();
    kprintf("=================================");
}
