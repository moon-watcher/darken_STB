#include <genesis.h>

#include "dsmap.h"

#define DSMAP3_STABLE
#define DSMAP4_STABLE

#include "dsmap3.h"
#include "dsmap4.h"
#include "dsmap5.h"

/* ================================================================
 * CONFIG
 * ================================================================ */

#define BENCH_CAPACITY 256
#define BENCH_ITERATIONS 100

/* ================================================================
 * OBJECT
 * ================================================================ */

typedef struct
{
    uint16_t x;
    uint16_t y;
    uint16_t vx;
    uint16_t vy;

} Entity;

/* ================================================================
 * STORAGE
 * ================================================================ */

DSMAP_DECLARE(storage_dsmap, BENCH_CAPACITY, Entity);
DSMAP3_DECLARE(storage_dsmap3, BENCH_CAPACITY, Entity);
DSMAP4_DECLARE(storage_dsmap4, BENCH_CAPACITY, Entity);
DSMAP5_DECLARE(storage_dsmap5, BENCH_CAPACITY, Entity);

/* ================================================================
 * GLOBAL DATA
 * ================================================================ */

static dsmap_handle_t handles[BENCH_CAPACITY];

static volatile uint32_t bench_sink;

static void bench_prepare_dsmap(dsmap_t *map)
{
    *map = (dsmap_t)DSMAP_INIT(storage_dsmap, Entity);
    dsmap_init(map);
}

static void bench_prepare_dsmap3(dsmap3_t *map)
{
    *map = (dsmap3_t)DSMAP3_INIT(storage_dsmap3);
    dsmap3_init(map);
}

static void bench_prepare_dsmap4(dsmap4_t *map)
{
    *map = (dsmap4_t)DSMAP4_INIT(storage_dsmap4);
    dsmap4_init(map);
}

static void bench_prepare_dsmap5(dsmap5_t *map)
{
    *map = (dsmap5_t)DSMAP5_INIT(storage_dsmap5);
    dsmap5_init(map);
}

static void bench_fill_dsmap(dsmap_t *map)
{
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap_handle_t handle = dsmap_alloc(map);
        handles[i] = handle;
        Entity *entity = dsmap_data(map, handle);
        entity->x = i;
        entity->y = i + 1;
        entity->vx = i + 2;
        entity->vy = i + 3;
    }
}

static void bench_fill_dsmap3(dsmap3_t *map)
{
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap3_handle_t handle = dsmap3_alloc(map);
        handles[i] = handle;
        Entity *entity = dsmap3_data(map, handle);
        entity->x = i;
        entity->y = i + 1;
        entity->vx = i + 2;
        entity->vy = i + 3;
    }
}

static void bench_fill_dsmap4(dsmap4_t *map)
{
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap4_handle_t handle = dsmap4_alloc(map);
        handles[i] = handle;
        Entity *entity = dsmap4_data(map, handle);
        entity->x = i;
        entity->y = i + 1;
        entity->vx = i + 2;
        entity->vy = i + 3;
    }
}

static void bench_fill_dsmap5(dsmap5_t *map)
{
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap5_handle_t handle = dsmap5_alloc(map);
        handles[i] = handle;
        Entity *entity = dsmap5_data(map, handle);
        entity->x = i;
        entity->y = i + 1;
        entity->vx = i + 2;
        entity->vy = i + 3;
    }
}

static void bench_dsmap_alloc(void)
{
    dsmap_t map;
    bench_prepare_dsmap(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap_handle_t handle = dsmap_alloc(&map);
        sum += handle;
    }

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap3_alloc(void)
{
    dsmap3_t map;
    bench_prepare_dsmap3(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap3_handle_t handle = dsmap3_alloc(&map);
        sum += handle;
    }

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap4_alloc(void)
{
    dsmap4_t map;
    bench_prepare_dsmap4(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap4_handle_t handle = dsmap4_alloc(&map);
        sum += handle;
    }

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap5_alloc(void)
{
    dsmap5_t map;
    bench_prepare_dsmap5(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap5_handle_t handle = dsmap5_alloc(&map);
        sum += handle;
    }

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap_valid(void)
{
    dsmap_t map;
    bench_prepare_dsmap(&map);
    bench_fill_dsmap(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
            sum += dsmap_valid(&map, handles[i]);

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap3_valid(void)
{
    dsmap3_t map;
    bench_prepare_dsmap3(&map);
    bench_fill_dsmap3(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
            sum += dsmap3_valid(&map, handles[i]);

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap4_valid(void)
{
    dsmap4_t map;
    bench_prepare_dsmap4(&map);
    bench_fill_dsmap4(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
            sum += dsmap4_valid(&map, handles[i]);

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap5_valid(void)
{
    dsmap5_t map;
    bench_prepare_dsmap5(&map);
    bench_fill_dsmap5(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
            sum += dsmap5_valid(&map, handles[i]);

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap_data(void)
{
    dsmap_t map;
    bench_prepare_dsmap(&map);
    bench_fill_dsmap(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
        {
            Entity *entity = dsmap_data(&map, handles[i]);
            sum += entity->x;
        }

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap3_data(void)
{
    dsmap3_t map;
    bench_prepare_dsmap3(&map);
    bench_fill_dsmap3(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
        {
            Entity *entity = dsmap3_data(&map, handles[i]);
            sum += entity->x;
        }

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap4_data(void)
{
    dsmap4_t map;
    bench_prepare_dsmap4(&map);
    bench_fill_dsmap4(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
        {
            Entity *entity = dsmap4_data(&map, handles[i]);
            sum += entity->x;
        }

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap5_data(void)
{
    dsmap5_t map;
    bench_prepare_dsmap5(&map);
    bench_fill_dsmap5(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
        {
            Entity *entity = dsmap5_data(&map, handles[i]);
            sum += entity->x;
        }

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap_iteration(void)
{
    dsmap_t map;
    bench_prepare_dsmap(&map);
    bench_fill_dsmap(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < map.count; i++)
        {
            Entity *entity = (Entity *)(map.pool + ((uint32_t)i * map.size));
            sum += entity->x;
        }

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap3_iteration(void)
{
    dsmap3_t map;
    bench_prepare_dsmap3(&map);
    bench_fill_dsmap3(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < map.count; i++)
        {
            dsmap3_handle_t handle = map.handles[i];
            Entity *entity = (Entity *)(map.pool + ((uint32_t)handle * map.size));
            sum += entity->x;
        }

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap4_iteration(void)
{
    dsmap4_t map;
    bench_prepare_dsmap4(&map);
    bench_fill_dsmap4(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < map.count; i++)
        {
            dsmap4_handle_t handle = map.handles[i];
            Entity *entity = (Entity *)(map.pool + ((uint32_t)handle * map.size));
            sum += entity->x;
        }

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap5_iteration(void)
{
    dsmap5_t map;
    bench_prepare_dsmap5(&map);
    bench_fill_dsmap5(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < map.count; i++)
        {
            Entity *entity = (Entity *)(map.pool + ((uint32_t)i * map.size));
            sum += entity->x;
        }

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap_remove(void)
{
    dsmap_t map;
    bench_prepare_dsmap(&map);
    bench_fill_dsmap(&map);

    BLASTEM_PROFIL_START;

    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        uint16_t index = (uint16_t)((i * 37u) & (BENCH_CAPACITY - 1));
        dsmap_remove(&map, handles[index]);
    }

    BLASTEM_PROFIL_END;
}

// static void bench_dsmap3_remove(void)
// {
//     dsmap3_t map;
//     bench_prepare_dsmap3(&map);
//     bench_fill_dsmap3(&map);

//     BLASTEM_PROFIL_START;

//     for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
//     {
//         uint16_t index = (uint16_t)((i * 37u) & (BENCH_CAPACITY - 1));
//         dsmap3_remove(&map, handles[index]);
//     }

//     BLASTEM_PROFIL_END;
// }

// static void bench_dsmap4_remove(void)
// {
//     dsmap4_t map;
//     bench_prepare_dsmap4(&map);
//     bench_fill_dsmap4(&map);

//     BLASTEM_PROFIL_START;

//     for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
//     {
//         uint16_t index = (uint16_t)((i * 37u) & (BENCH_CAPACITY - 1));
//         dsmap4_remove(&map, handles[index]);
//     }

//     BLASTEM_PROFIL_END;
// }

static void bench_dsmap5_remove(void)
{
    dsmap5_t map;
    bench_prepare_dsmap5(&map);
    bench_fill_dsmap5(&map);

    BLASTEM_PROFIL_START;

    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        uint16_t index = (uint16_t)((i * 37u) & (BENCH_CAPACITY - 1));
        dsmap5_remove(&map, handles[index]);
    }

    BLASTEM_PROFIL_END;
}

/* ================================================================
 * PUBLIC BENCHMARK
 * ================================================================ */

void bench_dsmap_compare(void)
{
    // kprintf("=== DSMAP VS DSMAP3 VS DSMAP4 VS DSMAP5 ===");
    kprintf("=== DSMAP VS DSMAP5 ===");
    kprintf("capacity=%u size=%u iterations=%u", BENCH_CAPACITY, (uint16_t)sizeof(Entity), BENCH_ITERATIONS);

// #ifdef DSMAP3_STABLE
//     kprintf("DSMAP3_STABLE: Activo ---");
// #else
//     kprintf("DSMAP3_STABLE: Desactivado ---");
// #endif

// #ifdef DSMAP4_STABLE
//     kprintf("DSMAP4_STABLE: Activo ---");
// #else
//     kprintf("DSMAP4_STABLE: Desactivado ---");
// #endif

    kprintf("> ALLOC ---");
    bench_dsmap_alloc();
    // bench_dsmap3_alloc();
    // bench_dsmap4_alloc();
    bench_dsmap5_alloc();

    kprintf("> VALID ---");
    bench_dsmap_valid();
    // bench_dsmap3_valid();
    // bench_dsmap4_valid();
    bench_dsmap5_valid();

    kprintf("> DATA ---");
    bench_dsmap_data();
    // bench_dsmap3_data();
    // bench_dsmap4_data();
    bench_dsmap5_data();

    kprintf("> ITER ---");
    bench_dsmap_iteration();
    // bench_dsmap3_iteration();
    // bench_dsmap4_iteration();
    bench_dsmap5_iteration();

    kprintf("> REMOVE ---");
    bench_dsmap_remove();
    // bench_dsmap3_remove();
    // bench_dsmap4_remove();
    bench_dsmap5_remove();

    kprintf("sink=%lu", bench_sink);
}