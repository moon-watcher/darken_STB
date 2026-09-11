#include <genesis.h>

#include "dsmap.h"
#define DSMAP3_STABLE
#include "dsmap3.h"


/* ================================================================
 * CONFIG
 * ================================================================ */

#define BENCH_CAPACITY    256
#define BENCH_ITERATIONS  1000


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

DSMAP_DECLARE(
    storage_dsmap,
    BENCH_CAPACITY,
    Entity
);

DSMAP3_DECLARE(
    storage_dsmap3,
    BENCH_CAPACITY,
    Entity
);


/* ================================================================
 * GLOBAL DATA
 * ================================================================ */

static dsmap_handle_t handles[BENCH_CAPACITY];

static volatile uint32_t bench_sink;


/* ================================================================
 * PREPARE DSMAP
 * ================================================================ */

static void
bench_prepare_dsmap(dsmap_t *map)
{
    *map = (dsmap_t)DSMAP_INIT(
        storage_dsmap,
        Entity
    );

    dsmap_init(map);
}


/* ================================================================
 * PREPARE DSMAP3
 * ================================================================ */

static void
bench_prepare_dsmap3(dsmap3_t *map)
{
    *map = (dsmap3_t)DSMAP3_INIT(storage_dsmap3);

    dsmap3_init(map);
}


/* ================================================================
 * FILL DSMAP
 * ================================================================ */

static void
bench_fill_dsmap(dsmap_t *map)
{
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap_handle_t handle =
            dsmap_alloc(map);

        handles[i] = handle;

        Entity *entity =
            dsmap_data(map, handle);

        entity->x = i;
        entity->y = i + 1;
        entity->vx = i + 2;
        entity->vy = i + 3;
    }
}


/* ================================================================
 * FILL DSMAP3
 * ================================================================ */

static void
bench_fill_dsmap3(dsmap3_t *map)
{
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap3_handle_t handle =
            dsmap3_alloc(map);

        handles[i] = handle;

        Entity *entity =
            dsmap3_data(map, handle);

        entity->x = i;
        entity->y = i + 1;
        entity->vx = i + 2;
        entity->vy = i + 3;
    }
}


/* ================================================================
 * ALLOC
 *
 * Exactly BENCH_CAPACITY allocations.
 *
 * No remove().
 * No reset().
 * No other operation inside the timed section.
 * ================================================================ */

static void
bench_dsmap_alloc(void)
{
    dsmap_t map;

    bench_prepare_dsmap(&map);

    uint32_t sum = 0;


    BLASTEM_PROFIL_START;

    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap_handle_t handle =
            dsmap_alloc(&map);

        sum += handle;
    }

    BLASTEM_PROFIL_END;


    bench_sink = sum;
}


static void
bench_dsmap3_alloc(void)
{
    dsmap3_t map;

    bench_prepare_dsmap3(&map);

    uint32_t sum = 0;


    BLASTEM_PROFIL_START;

    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        dsmap3_handle_t handle =
            dsmap3_alloc(&map);

        sum += handle;
    }

    BLASTEM_PROFIL_END;


    bench_sink = sum;
}


/* ================================================================
 * VALID
 *
 * Same handles.
 *
 * 10000 * 256 validations.
 * ================================================================ */

static void
bench_dsmap_valid(void)
{
    dsmap_t map;

    bench_prepare_dsmap(&map);
    bench_fill_dsmap(&map);

    uint32_t sum = 0;


    BLASTEM_PROFIL_START;

    for (uint16_t n = 0;
         n < BENCH_ITERATIONS;
         n++)
    {
        for (uint16_t i = 0;
             i < BENCH_CAPACITY;
             i++)
        {
            sum += dsmap_valid(
                &map,
                handles[i]
            );
        }
    }

    BLASTEM_PROFIL_END;


    bench_sink = sum;
}


static void
bench_dsmap3_valid(void)
{
    dsmap3_t map;

    bench_prepare_dsmap3(&map);
    bench_fill_dsmap3(&map);

    uint32_t sum = 0;


    BLASTEM_PROFIL_START;

    for (uint16_t n = 0;
         n < BENCH_ITERATIONS;
         n++)
    {
        for (uint16_t i = 0;
             i < BENCH_CAPACITY;
             i++)
        {
            sum += dsmap3_valid(
                &map,
                handles[i]
            );
        }
    }

    BLASTEM_PROFIL_END;


    bench_sink = sum;
}


/* ================================================================
 * DATA
 *
 * 10000 * 256 lookups.
 * ================================================================ */

static void
bench_dsmap_data(void)
{
    dsmap_t map;

    bench_prepare_dsmap(&map);
    bench_fill_dsmap(&map);

    uint32_t sum = 0;


    BLASTEM_PROFIL_START;

    for (uint16_t n = 0;
         n < BENCH_ITERATIONS;
         n++)
    {
        for (uint16_t i = 0;
             i < BENCH_CAPACITY;
             i++)
        {
            Entity *entity =
                dsmap_data(
                    &map,
                    handles[i]
                );

            sum += entity->x;
        }
    }

    BLASTEM_PROFIL_END;


    bench_sink = sum;
}


static void
bench_dsmap3_data(void)
{
    dsmap3_t map;

    bench_prepare_dsmap3(&map);
    bench_fill_dsmap3(&map);

    uint32_t sum = 0;


    BLASTEM_PROFIL_START;

    for (uint16_t n = 0;
         n < BENCH_ITERATIONS;
         n++)
    {
        for (uint16_t i = 0;
             i < BENCH_CAPACITY;
             i++)
        {
            Entity *entity =
                dsmap3_data(
                    &map,
                    handles[i]
                );

            sum += entity->x;
        }
    }

    BLASTEM_PROFIL_END;


    bench_sink = sum;
}


/* ================================================================
 * ITERATION
 *
 * DSMAP:
 *
 *     pool[0]
 *     pool[1]
 *     pool[2]
 *     ...
 *
 *
 * DSMAP3:
 *
 *     active[0] -> pool[handle]
 *     active[1] -> pool[handle]
 *     ...
 * ================================================================ */

static void
bench_dsmap_iteration(void)
{
    dsmap_t map;

    bench_prepare_dsmap(&map);
    bench_fill_dsmap(&map);

    uint32_t sum = 0;


    BLASTEM_PROFIL_START;

    for (uint16_t n = 0;
         n < BENCH_ITERATIONS;
         n++)
    {
        for (uint16_t i = 0;
             i < map.count;
             i++)
        {
            Entity *entity =
                (Entity *)(
                    map.pool +
                    ((uint32_t)i * map.size)
                );

            sum += entity->x;
        }
    }

    BLASTEM_PROFIL_END;


    bench_sink = sum;
}


static void
bench_dsmap3_iteration(void)
{
    dsmap3_t map;

    bench_prepare_dsmap3(&map);
    bench_fill_dsmap3(&map);

    uint32_t sum = 0;


    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
    {
        // for (uint16_t i = 0; i < map.count; i++) {
        //     Entity *entity =(Entity *)(map.pool +((uint32_t)i * map.size));
        //     sum += entity->x;
        // }
    
        for (uint16_t i = 0; i < map.count; i++){
            dsmap3_handle_t handle = map.handles[i];
            Entity *entity =(Entity *)(map.pool +((uint32_t)handle * map.size));
            sum += entity->x;
        }
    }

    BLASTEM_PROFIL_END;


    bench_sink = sum;
}


/* ================================================================
 * REMOVE
 *
 * Removes all objects using exactly the same handle sequence.
 *
 * This measures remove() itself.
 * ================================================================ */

static void
bench_dsmap_remove(void)
{
    dsmap_t map;

    bench_prepare_dsmap(&map);
    bench_fill_dsmap(&map);


    BLASTEM_PROFIL_START;

    for (uint16_t i = 0;
         i < BENCH_CAPACITY;
         i++)
    {
        /*
         * 37 is coprime with 256, therefore this visits
         * every handle exactly once.
         */
        uint16_t index =
            (uint16_t)((i * 37u) &
                       (BENCH_CAPACITY - 1));

        dsmap_remove(
            &map,
            handles[index]
        );
    }

    BLASTEM_PROFIL_END;
}


static void
bench_dsmap3_remove(void)
{
    dsmap3_t map;

    bench_prepare_dsmap3(&map);
    bench_fill_dsmap3(&map);


    BLASTEM_PROFIL_START;

    for (uint16_t i = 0;
         i < BENCH_CAPACITY;
         i++)
    {
        uint16_t index =
            (uint16_t)((i * 37u) &
                       (BENCH_CAPACITY - 1));

        dsmap3_remove(
            &map,
            handles[index]
        );
    }

    BLASTEM_PROFIL_END;
}


/* ================================================================
 * REMOVE + ALLOC
 *
 * One active object is removed and immediately replaced.
 *
 * This isolates the real hot path:
 *
 *     remove()
 *     alloc()
 *
 * Repeated BENCH_ITERATIONS times.
 * ================================================================ */

static void
bench_dsmap_remove_alloc(void)
{
    dsmap_t map;

    bench_prepare_dsmap(&map);
    bench_fill_dsmap(&map);


    dsmap_handle_t handle =
        handles[0];


    BLASTEM_PROFIL_START;

    for (uint16_t i = 0;
         i < BENCH_ITERATIONS;
         i++)
    {
        dsmap_remove(
            &map,
            handle
        );

        handle =
            dsmap_alloc(&map);
    }

    BLASTEM_PROFIL_END;
}


static void
bench_dsmap3_remove_alloc(void)
{
    dsmap3_t map;

    bench_prepare_dsmap3(&map);
    bench_fill_dsmap3(&map);


    dsmap3_handle_t handle =
        handles[0];


    BLASTEM_PROFIL_START;

    for (uint16_t i = 0;
         i < BENCH_ITERATIONS;
         i++)
    {
        dsmap3_remove(
            &map,
            handle
        );

        handle =
            dsmap3_alloc(&map);
    }

    BLASTEM_PROFIL_END;
}


/* ================================================================
 * PUBLIC BENCHMARK
 * ================================================================ */

void
bench_dsmap_compare(void)
{
    kprintf(
        "=== DSMAP VS DSMAP3 ==="
    );

    kprintf(
        "capacity=%u size=%u iterations=%u",
        BENCH_CAPACITY,
        (uint16_t)sizeof(Entity),
        BENCH_ITERATIONS
    );


    /* ------------------------------------------------------------
     * ALLOC
     * ------------------------------------------------------------ */

    kprintf(
        "--- ALLOC DSMAP ---"
    );

    bench_dsmap_alloc();

    kprintf(
        "--- ALLOC DSMAP3 ---"
    );

    bench_dsmap3_alloc();


    /* ------------------------------------------------------------
     * VALID
     * ------------------------------------------------------------ */

    kprintf(
        "--- VALID DSMAP ---"
    );

    bench_dsmap_valid();

    kprintf(
        "--- VALID DSMAP3 ---"
    );

    bench_dsmap3_valid();


    /* ------------------------------------------------------------
     * DATA
     * ------------------------------------------------------------ */

    kprintf(
        "--- DATA DSMAP ---"
    );

    bench_dsmap_data();

    kprintf(
        "--- DATA DSMAP3 ---"
    );

    bench_dsmap3_data();


    /* ------------------------------------------------------------
     * ITERATION
     * ------------------------------------------------------------ */

    kprintf(
        "--- ITER DSMAP ---"
    );

    bench_dsmap_iteration();

    kprintf(
        "--- ITER DSMAP3 ---"
    );

    bench_dsmap3_iteration();


    /* ------------------------------------------------------------
     * REMOVE
     * ------------------------------------------------------------ */

    kprintf(
        "--- REMOVE DSMAP ---"
    );

    bench_dsmap_remove();

    kprintf(
        "--- REMOVE DSMAP3 ---"
    );

    bench_dsmap3_remove();


    /* ------------------------------------------------------------
     * REMOVE + ALLOC
     * ------------------------------------------------------------ */

    kprintf(
        "--- R+A DSMAP ---"
    );

    bench_dsmap_remove_alloc();

    kprintf(
        "--- R+A DSMAP3 ---"
    );

    bench_dsmap3_remove_alloc();


    /*
     * Keep compiler from eliminating the benchmark work.
     */
    kprintf(
        "sink=%lu",
        bench_sink
    );
}