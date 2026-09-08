#include <genesis.h>
#include <string.h>

#include "../dpool.h"

typedef struct
{
    uint16_t id;
} Entity;

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

static void dpool_dump(dpool *pool)
{
    kprintf("DPOOL DUMP: count=%d capacity=%d", pool->count, pool->capacity);

    for (uint16_t i = 0; i < pool->count; ++i)
    {
        Entity *entity = dpool_data(pool, pool->handles[i]);

        kprintf("  [%d] handle=%d id=%d",
                i,
                pool->handles[i],
                entity->id);
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

    DPOOL_POOL_BIND(storage, memcpy);

    dpool pool;
    DPOOL_POOL_INIT(pool, storage, memcpy);

    int16_t a = dpool_alloc(&pool);
    int16_t b = dpool_alloc(&pool);
    int16_t c = dpool_alloc(&pool);

    Entity *entity;

    entity = dpool_data(&pool, c);
    kprintf("before -> %d", entity->id);

    dpool_remove(&pool, a);

    entity = dpool_data(&pool, c);
    kprintf("after  -> %d", entity->id);

    dpool_dump(&pool);
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

    DPOOL_POOL_BIND(storage, memcpy);

    dpool pool;
    DPOOL_POOL_INIT(pool, storage, memcpy);

    int16_t a = dpool_alloc(&pool);
    int16_t b = dpool_alloc(&pool);
    int16_t c = dpool_alloc(&pool);

    dpool_dump(&pool);

    dpool_remove(&pool, b);

    kprintf("after remove b=%d", b);

    Entity *entity;

    entity = dpool_data(&pool, a);
    kprintf("a -> %d", entity->id);

    entity = dpool_data(&pool, c);
    kprintf("c -> %d", entity->id);

    kprintf("b -> %x", (unsigned int)dpool_data(&pool, b));

    dpool_dump(&pool);
}

static void test_remove_last(void)
{
    kprintf("TEST: remove last");

    Entity storage[4] = {
        {10},
        {20},
        {30},
        {40},
    };

    DPOOL_POOL_BIND(storage, memcpy);

    dpool pool;
    DPOOL_POOL_INIT(pool, storage, memcpy);

    int16_t a = dpool_alloc(&pool);
    int16_t b = dpool_alloc(&pool);
    int16_t c = dpool_alloc(&pool);

    dpool_remove(&pool, c);

    Entity *entity;

    entity = dpool_data(&pool, a);
    kprintf("a -> %d", entity->id);

    entity = dpool_data(&pool, b);
    kprintf("b -> %d", entity->id);

    kprintf("c -> %x", (unsigned int)dpool_data(&pool, c));
}

static void test_clear(void)
{
    kprintf("TEST: clear");

    Entity storage[4] = {
        {10},
        {20},
        {30},
        {40},
    };

    DPOOL_POOL_BIND(storage, memcpy);

    dpool pool;
    DPOOL_POOL_INIT(pool, storage, memcpy);

    int16_t a = dpool_alloc(&pool);
    int16_t b = dpool_alloc(&pool);

    dpool_dump(&pool);

    dpool_clear(&pool);

    kprintf("count=%d", pool.count);
    kprintf("a -> %x", (unsigned int)dpool_data(&pool, a));
    kprintf("b -> %x", (unsigned int)dpool_data(&pool, b));
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================ */

void dpool_run_tests(void)
{
    kprintf("================================");
    kprintf("DPOOL TESTS");
    kprintf("================================");

    test_stable_handle();
    test_remove_swap();
    test_remove_last();
    test_clear();

    kprintf("================================");
}