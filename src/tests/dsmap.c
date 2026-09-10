#include <genesis.h>
#include <string.h>

#include "../dsmap.h"

typedef struct
{
    uint16_t id;
} Entity;

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

static void dsmap_dump(dsmap_t *pool)
{
    return;

    kprintf("DSMAP DUMP: count=%d capacity=%d", pool->count, pool->capacity);

    for (uint16_t i = 0; i < pool->count; ++i)
    {
        uint16_t handle = pool->handles[i];
        Entity *entity = dsmap_data(pool, handle);

        kprintf("  [%d] handle=%d id=%d", i, handle, entity->id);
    }
}

/* ============================================================================
 * TESTS
 * ============================================================================ */

static void test_stable_handle(void)
{
    kprintf("TEST: stable handle");

    Entity storage[4] = {
        {10},
        {20},
        {30},
        {40},
    };

    uint16_t lookup[4];
    uint16_t handles[4];

    dsmap_t pool = DSMAP_BIND(storage, lookup, handles, memcpy);

    int16_t a = dsmap_alloc(&pool);
    int16_t b = dsmap_alloc(&pool);
    int16_t c = dsmap_alloc(&pool);

    Entity *entity;

    entity = dsmap_data(&pool, c);
    kprintf("before -> %d", entity->id);

    dsmap_remove(&pool, a);

    entity = dsmap_data(&pool, c);
    kprintf("after  -> %d", entity->id);

    dsmap_dump(&pool);
}

static void test_remove_swap(void)
{
    kprintf("TEST: remove swap");

    Entity storage[4] = {
        {10},
        {20},
        {30},
        {40},
    };

    uint16_t lookup[4];
    uint16_t handles[4];

    dsmap_t pool = DSMAP_BIND(storage, lookup, handles, memcpy);

    int16_t a = dsmap_alloc(&pool);
    int16_t b = dsmap_alloc(&pool);
    int16_t c = dsmap_alloc(&pool);

    dsmap_dump(&pool);
    dsmap_remove(&pool, b);

    kprintf("after remove b=%d", b);

    Entity *entity;

    entity = dsmap_data(&pool, a);
    kprintf("a -> %d", entity->id);

    entity = dsmap_data(&pool, c);
    kprintf("c -> %d", entity->id);

    entity = dsmap_data(&pool, b);

    if (entity == 0)
        kprintf("b -> NULL");
    else
        kprintf("b -> %d", entity->id);

    dsmap_dump(&pool);
}

static void test_handle_reuse(void)
{
    kprintf("TEST: handle reuse");

    Entity storage[4] = {
        {10},
        {20},
        {30},
        {40},
    };

    uint16_t lookup[4];
    uint16_t handles[4];

    dsmap_t pool = DSMAP_BIND(storage, lookup, handles, memcpy);

    int16_t a = dsmap_alloc(&pool);
    int16_t b = dsmap_alloc(&pool);
    int16_t c = dsmap_alloc(&pool);

    kprintf("a=%d b=%d c=%d", a, b, c);

    dsmap_remove(&pool, b);

    int16_t d = dsmap_alloc(&pool);

    kprintf("d=%d reused=%d", d, b);

    dsmap_dump(&pool);
}

static void test_dynamic(void)
{
    kprintf("TEST: dynamic");

    dsmap_t pool = DSMAP_ALLOC(MEM_alloc, 4, sizeof(Entity), memcpy);

    Entity *a = dsmap_data(&pool, dsmap_alloc(&pool));
    Entity *b = dsmap_data(&pool, dsmap_alloc(&pool));
    int16_t c = dsmap_alloc(&pool);

    a->id = 10;
    b->id = 20;

    Entity *entity;

    entity = dsmap_data(&pool, c);
    entity->id = 30;

    kprintf("a -> %d", a->id);
    kprintf("b -> %d", b->id);
    kprintf("c -> %d", entity->id);

    dsmap_remove(&pool, c);

    MEM_free(pool.pool);
    MEM_free(pool.lookup);
    MEM_free(pool.handles);
}

static void test_full(void)
{
    kprintf("TEST: full");

    Entity storage[4] = {
        {10},
        {20},
        {30},
        {40},
    };

    uint16_t lookup[4];
    uint16_t handles[4];

    dsmap_t pool = DSMAP_BIND(storage, lookup, handles, memcpy);

    int16_t a = dsmap_alloc(&pool);
    int16_t b = dsmap_alloc(&pool);
    int16_t c = dsmap_alloc(&pool);
    int16_t d = dsmap_alloc(&pool);
    int16_t e = dsmap_alloc(&pool);

    kprintf("a=%d b=%d c=%d d=%d e=%d", a, b, c, d, e);
    kprintf("count=%d", pool.count);

    dsmap_dump(&pool);
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================ */

void dsmap_run_tests(void)
{
    kprintf("================================");
    kprintf("DSMAP TESTS");
    kprintf("================================");

    test_stable_handle();
    test_remove_swap();
    test_handle_reuse();
    test_dynamic();
    test_full();

    kprintf("================================");
}