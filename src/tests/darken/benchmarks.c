#include <genesis.h>

#include "../../darken.h"
#include "benchmarks.h"

struct MyComponent
{
    int x, y;
    uint8_t health;
};

static volatile u32 g_frameCounter = 0;
static void de_bench_vblank_cb(void) { g_frameCounter++; }
static u32 bench_start(void) { return g_frameCounter; }
static u32 bench_frames_elapsed(u32 start) { return g_frameCounter - start; }
#define BENCH_REPS 2000

static void de_bench_create_destroy(void)
{
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 32, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS; ++r)
    {
        for (u16 i = 0; i < 32; ++i)
            de_manager_new(&m);
        de_manager_reset(&m);
    }
    u32 frames = bench_frames_elapsed(t0);
    kprintf("create+reset 32 entidades x%d reps: %ld frames", BENCH_REPS, frames);
}

static void *de_bench_state_fn(void *data)
{
    (void)data;
    return DE_STATE_LOOP;
}

static void de_bench_update(void)
{
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 32, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    for (u16 i = 0; i < 32; ++i)
    {
        de_entity e = de_manager_new(&m);
        e->state = (de_state)de_bench_state_fn;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS; ++r)
        de_manager_update(&m);
    u32 frames = bench_frames_elapsed(t0);
    kprintf("de_manager_update, 32 entidades x%d reps: %ld frames", BENCH_REPS, frames);
}

static void de_bench_apply(void)
{
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 32, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS; ++r)
    {
        for (u16 i = 0; i < 32; ++i)
        {
            de_entity e = de_manager_new(&m);
            e->tag = i;
        }
        // DE_MANAGER_APPLY(&m, (ENTITY->tag % 2) == 0, de_entity_delete);
        DE_MANAGER_FOREACH(&m, {
            if (ENTITY->tag % 2 == 0)
                de_entity_delete(ENTITY);
        });
        de_manager_reset(&m);
    }
    u32 frames = bench_frames_elapsed(t0);
    kprintf("create32+applyAll(borrar pares)+reset x%d reps: %ld frames", BENCH_REPS, frames);
}

static void de_bench_create_destroy_128(void)
{
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 128, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    u32 t0 = bench_start();
    for (u32 r = 0; r < 500; ++r)
    {
        for (u16 i = 0; i < 128; ++i)
            de_manager_new(&m);
        de_manager_reset(&m);
    }
    u32 frames = bench_frames_elapsed(t0);
    kprintf("create+reset 128 entidades x500 reps: %ld frames", frames);
}

static void de_bench_create_destroy_256(void)
{
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 256, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    u32 t0 = bench_start();
    for (u32 r = 0; r < 250; ++r)
    {
        for (u16 i = 0; i < 256; ++i)
            de_manager_new(&m);
        de_manager_reset(&m);
    }
    u32 frames = bench_frames_elapsed(t0);
    kprintf("create+reset 256 entidades x250 reps: %ld frames", frames);
}

static void de_bench_update_128(void)
{
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 128, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    for (u16 i = 0; i < 128; ++i)
    {
        de_entity e = de_manager_new(&m);
        e->state = (de_state)de_bench_state_fn;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < 500; ++r)
        de_manager_update(&m);
    u32 frames = bench_frames_elapsed(t0);
    kprintf("de_manager_update, 128 entidades x500 reps: %ld frames", frames);
}

static void de_bench_update_256(void)
{
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 256, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    for (u16 i = 0; i < 256; ++i)
    {
        de_entity e = de_manager_new(&m);
        e->state = (de_state)de_bench_state_fn;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < 250; ++r)
        de_manager_update(&m);
    u32 frames = bench_frames_elapsed(t0);
    kprintf("de_manager_update, 256 entidades x250 reps: %ld frames", frames);
}

static void de_bench_swap(void)
{
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    u32 t0 = bench_start();
    (void)a;
    (void)b;
    u32 frames = bench_frames_elapsed(t0);
    kprintf("de_entity_swap x50000: %ld frames (comentado)", frames);
}

static void de_bench_memory_overhead(void)
{
    kprintf("========== MEMORIA ==========");
    u16 stride16 = _DE_ENTITY_STRIDE(16);
    u16 stride32 = _DE_ENTITY_STRIDE(32);
    u16 stride1 = _DE_ENTITY_STRIDE(1);
    u16 stride9 = _DE_ENTITY_STRIDE(9);
    kprintf("sizeof(de_entity) base: %d bytes", (int)sizeof(struct de_entity));
    kprintf("stride payload=1:  %d bytes/entidad", stride1);
    kprintf("stride payload=9:  %d bytes/entidad (impar)", stride9);
    kprintf("stride payload=16: %d bytes/entidad", stride16);
    kprintf("stride payload=32: %d bytes/entidad", stride32);
    struct de_manager m32;
    DE_MANAGER_STORAGE(m32_storage, 32, sizeof(struct MyComponent));
    de_manager_init(&m32, DE_MANAGER_ARGS(m32_storage));
    u32 bytes32 = 32 * _DE_ENTITY_STRIDE(sizeof(struct MyComponent));
    kprintf("Manager 32 entidades (payload %d): %ld bytes en storage", (int)sizeof(struct MyComponent), bytes32);
    struct de_manager m128;
    DE_MANAGER_STORAGE(m128_storage, 128, sizeof(struct MyComponent));
    de_manager_init(&m128, DE_MANAGER_ARGS(m128_storage));
    u32 bytes128 = 128 * _DE_ENTITY_STRIDE(sizeof(struct MyComponent));
    kprintf("Manager 128 entidades (payload %d): %ld bytes en storage", (int)sizeof(struct MyComponent), bytes128);
    u16 overhead = _DE_ENTITY_STRIDE(sizeof(struct MyComponent)) - sizeof(struct MyComponent);
    kprintf("Overhead por entidad: %d bytes (header + padding)", overhead);
    u32 ramAvailable = 64 * 1024;
    u32 entidadesEn64k = ramAvailable / _DE_ENTITY_STRIDE(sizeof(struct MyComponent));
    kprintf("Entidades de MyComponent que caben en 64 KB: ~%ld", entidadesEn64k);
    kprintf("==============================");
}

void de_run_benchmarks(void)
{
    SYS_setVIntCallback(de_bench_vblank_cb);
    de_bench_memory_overhead();
    kprintf("========== BENCHMARKS ==========");
    de_bench_create_destroy();
    de_bench_update();
    de_bench_apply();
    de_bench_create_destroy_128();
    de_bench_create_destroy_256();
    de_bench_update_128();
    de_bench_update_256();
    de_bench_swap();
    kprintf("=================================");
}
