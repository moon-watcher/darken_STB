#include <genesis.h>
#include <string.h>

#include "../_dsmap0.h"

typedef struct
{
    uint16_t id;
} Entity;

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

static void dsmap0_dump(dsmap0_t *pool)
{
    return;

    kprintf("DSMAP DUMP: count=%d capacity=%d", pool->count, pool->capacity);

    for (uint16_t i = 0; i < pool->count; ++i)
    {
        uint16_t handle = pool->handles[i];
        Entity *entity = dsmap0_data(pool, handle);

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

    dsmap0_t pool = DSMAP0_BIND(storage, lookup, handles);

    int16_t a = dsmap0_alloc(&pool);
    int16_t b = dsmap0_alloc(&pool);
    int16_t c = dsmap0_alloc(&pool);

    Entity *entity;

    entity = dsmap0_data(&pool, c);
    kprintf("before -> %d", entity->id);

    dsmap0_remove(&pool, a);

    entity = dsmap0_data(&pool, c);
    kprintf("after  -> %d", entity->id);

    dsmap0_dump(&pool);
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

    dsmap0_t pool = DSMAP0_BIND(storage, lookup, handles);

    int16_t a = dsmap0_alloc(&pool);
    int16_t b = dsmap0_alloc(&pool);
    int16_t c = dsmap0_alloc(&pool);

    dsmap0_dump(&pool);
    dsmap0_remove(&pool, b);

    kprintf("after remove b=%d", b);

    Entity *entity;

    entity = dsmap0_data(&pool, a);
    kprintf("a -> %d", entity->id);

    entity = dsmap0_data(&pool, c);
    kprintf("c -> %d", entity->id);

    entity = dsmap0_data(&pool, b);

    if (entity == 0)
        kprintf("b -> NULL");
    else
        kprintf("b -> %d", entity->id);

    dsmap0_dump(&pool);
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

    dsmap0_t pool = DSMAP0_BIND(storage, lookup, handles);

    int16_t a = dsmap0_alloc(&pool);
    int16_t b = dsmap0_alloc(&pool);
    int16_t c = dsmap0_alloc(&pool);

    kprintf("a=%d b=%d c=%d", a, b, c);

    dsmap0_remove(&pool, b);

    int16_t d = dsmap0_alloc(&pool);

    kprintf("d=%d reused=%d", d, b);

    dsmap0_dump(&pool);
}

static void test_dynamic(void)
{
    kprintf("TEST: dynamic");

    dsmap0_t pool = DSMAP0_ALLOC(MEM_alloc, 4, sizeof(Entity));

    Entity *a = dsmap0_data(&pool, dsmap0_alloc(&pool));
    Entity *b = dsmap0_data(&pool, dsmap0_alloc(&pool));
    int16_t c = dsmap0_alloc(&pool);

    a->id = 10;
    b->id = 20;

    Entity *entity;

    entity = dsmap0_data(&pool, c);
    entity->id = 30;

    kprintf("a -> %d", a->id);
    kprintf("b -> %d", b->id);
    kprintf("c -> %d", entity->id);

    dsmap0_remove(&pool, c);

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

    dsmap0_t pool = DSMAP0_BIND(storage, lookup, handles);

    int16_t a = dsmap0_alloc(&pool);
    int16_t b = dsmap0_alloc(&pool);
    int16_t c = dsmap0_alloc(&pool);
    int16_t d = dsmap0_alloc(&pool);
    int16_t e = dsmap0_alloc(&pool);

    kprintf("a=%d b=%d c=%d d=%d e=%d", a, b, c, d, e);
    kprintf("count=%d", pool.count);

    dsmap0_dump(&pool);
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================ */

void dsmap0_run_tests(void)
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