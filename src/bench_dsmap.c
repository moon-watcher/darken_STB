#include <genesis.h>

#include "dsmap0.h"

#include "dsmap.h"

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

DSMAP0_DECLARE(storage_dsmap0, BENCH_CAPACITY, Entity);
DSMAP_DECLARE(storage_dsmap, BENCH_CAPACITY, Entity);

/* ================================================================
 * GLOBAL DATA
 * ================================================================ */

static dsmap0_handle_t handles[BENCH_CAPACITY];

static volatile uint32_t bench_sink;

static void bench_prepare_dsmap0(dsmap0_t *map)
{
    *map = (dsmap0_t)DSMAP0_INIT(storage_dsmap0, Entity);
    dsmap0_init(map);
}

static void bench_prepare_dsmap(dsmap_t *map)
{
    *map = DSMAP_BIND(storage_dsmap);
    dsmap_init(map);
}

static void bench_fill_dsmap0(dsmap0_t *map)
{
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap0_handle_t handle = dsmap0_alloc(map);
        handles[i] = handle;
        Entity *entity = dsmap0_data(map, handle);
        entity->x = i;
        entity->y = i + 1;
        entity->vx = i + 2;
        entity->vy = i + 3;
    }
}

static void bench_fill_dsmap(dsmap_t *map)
{
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap_handle_t handle = dsmap_alloc(map);
        handles[i] = handle;
        Entity *entity = dsmap0_data(map, handle);
        entity->x = i;
        entity->y = i + 1;
        entity->vx = i + 2;
        entity->vy = i + 3;
    }
}

static void bench_dsmap0_alloc(void)
{
    dsmap0_t map;
    bench_prepare_dsmap0(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap0_handle_t handle = dsmap0_alloc(&map);
        sum += handle;
    }

    BLASTEM_PROFIL_END;
    bench_sink = sum;
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

static void bench_dsmap0_valid(void)
{
    dsmap0_t map;
    bench_prepare_dsmap0(&map);
    bench_fill_dsmap0(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
            sum += dsmap0_valid(&map, handles[i]);

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

static void bench_dsmap0_data(void)
{
    dsmap0_t map;
    bench_prepare_dsmap0(&map);
    bench_fill_dsmap0(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
        {
            Entity *entity = dsmap0_data(&map, handles[i]);
            sum += entity->x;
        }

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
            // Entity *entity = dsmap_data(&map, handles[i]);
            Entity *entity = DSMAP_DATA(&map, handles[i]);
            
            sum += entity->x;
        }

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap0_iteration(void)
{
    dsmap0_t map;
    bench_prepare_dsmap0(&map);
    bench_fill_dsmap0(&map);
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

static void bench_DSMAP_iteration(void)
{
    dsmap_t map;
    bench_prepare_dsmap(&map);
    bench_fill_dsmap(&map);
    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        DSMAP_FOREACH(&map, sum += ((Entity *)_data)->x);

    BLASTEM_PROFIL_END;
    bench_sink = sum;
}

static void bench_dsmap0_remove(void)
{
    dsmap0_t map;
    bench_prepare_dsmap0(&map);
    bench_fill_dsmap0(&map);

    BLASTEM_PROFIL_START;

    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        uint16_t index = (uint16_t)((i * 37u) & (BENCH_CAPACITY - 1));
        dsmap0_remove(&map, handles[index]);
    }

    BLASTEM_PROFIL_END;
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

/* ================================================================
 * PUBLIC BENCHMARK
 * ================================================================ */

void bench_dsmap0_compare(void)
{
    // kprintf("=== DSMAP VS DSMAP3 VS DSMAP4 VS dsmap ===");
    kprintf("=== DSMAP VS dsmap ===");
    kprintf("capacity=%u size=%u iterations=%u", BENCH_CAPACITY, (uint16_t)sizeof(Entity), BENCH_ITERATIONS);

    kprintf("> ALLOC ---");
    bench_dsmap0_alloc();
    bench_dsmap_alloc();

    kprintf("> VALID ---");
    bench_dsmap0_valid();
    bench_dsmap_valid();

    kprintf("> DATA ---");
    bench_dsmap0_data();
    bench_dsmap_data();

    kprintf("> ITER ---");
    bench_dsmap0_iteration();
    bench_DSMAP_iteration();

    kprintf("> REMOVE ---");
    bench_dsmap0_remove();
    bench_dsmap_remove();

    kprintf("sink=%lu", bench_sink);
}