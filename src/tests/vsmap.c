#include <genesis.h>

#include "../vsmap.h"

typedef struct
{
    uint16_t id;
} Entity;

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

static void vsmap_dump(vsmap_t *pool)
{
    return ;
    kprintf("vsmap_t DUMP: count=%d capacity=%d", pool->count, pool->capacity);

    for (uint16_t i = 0; i < pool->count; ++i)
    {
        Entity *entity = vsmap_data(pool, pool->pool[i].handle);
        kprintf("  [%d] handle=%d ptr=%x id=%d", i, pool->pool[i].handle, entity, entity->id);
    }
}

/* ============================================================================
 * TESTS
 * ============================================================================ */

static void test_VSMAP_BIND(void)
{
    VSMAP_DECLARE(storage, 6);
    vsmap_t pool = VSMAP_BIND(storage);

    Entity entities[6] = { {0}, {1}, {2}, {3}, {4}, {5}, };

    int a = vsmap_add(&pool, &entities[0]);
    int b = vsmap_add(&pool, &entities[1]);
    int c = vsmap_add(&pool, &entities[2]);
    int d = vsmap_add(&pool, &entities[3]);
    int e = vsmap_add(&pool, &entities[4]);
    int f = vsmap_add(&pool, &entities[5]);

    kprintf("vsmap_t BIND: add");

    vsmap_dump(&pool);

    if (vsmap_data(&pool, a) != &entities[0]) kprintf("FAIL a");
    if (vsmap_data(&pool, b) != &entities[1]) kprintf("FAIL b");
    if (vsmap_data(&pool, c) != &entities[2]) kprintf("FAIL c");
    if (vsmap_data(&pool, d) != &entities[3]) kprintf("FAIL d");
    if (vsmap_data(&pool, e) != &entities[4]) kprintf("FAIL e");
    if (vsmap_data(&pool, f) != &entities[5]) kprintf("FAIL f");

    /*
     * Remove c
     *
     * c = handle 2
     *
     * El elemento de f pasa al slot físico de c,
     * pero el handle de f sigue siendo válido.
     */

    Entity *entity;
    entity = vsmap_data(&pool, f); kprintf(" -> %d", entity->id);

    vsmap_remove(&pool, c);

    entity = vsmap_data(&pool, f); kprintf(" -> %d", entity->id);

    kprintf("vsmap_t BIND: remove c");

    vsmap_dump(&pool);

    if (vsmap_data(&pool, a) != &entities[0]) kprintf("FAIL c remove: a");
    if (vsmap_data(&pool, b) != &entities[1]) kprintf("FAIL c remove: b");
    if (vsmap_data(&pool, c) != NULL) kprintf("FAIL c remove: c still valid");
    if (vsmap_data(&pool, d) != &entities[3]) kprintf("FAIL c remove: d");
    if (vsmap_data(&pool, e) != &entities[4]) kprintf("FAIL c remove: e");
    if (vsmap_data(&pool, f) != &entities[5]) kprintf("FAIL c remove: f");

    /*
     * f debe seguir apuntando a entity 5 aunque su slot físico haya cambiado.
     */

    entity = vsmap_data(&pool, f);
    kprintf("HANDLE f: id=%d", entity->id);

    /*
     * Remove a
     *
     * El último elemento pasa al slot físico de a.
     * Los handles siguen siendo estables.
     */

    vsmap_remove(&pool, a);

    kprintf("vsmap_t BIND: remove a");

    vsmap_dump(&pool);

    if (vsmap_data(&pool, a) != NULL) kprintf("FAIL a remove: a still valid");
    if (vsmap_data(&pool, b) != &entities[1]) kprintf("FAIL a remove: b");
    if (vsmap_data(&pool, d) != &entities[3]) kprintf("FAIL a remove: d");
    if (vsmap_data(&pool, e) != &entities[4]) kprintf("FAIL a remove: e");
    if (vsmap_data(&pool, f) != &entities[5]) kprintf("FAIL a remove: f");

    /*
     * Remove e
     */

    vsmap_remove(&pool, e);

    kprintf("vsmap_t BIND: remove e");

    vsmap_dump(&pool);

    if (vsmap_data(&pool, b) != &entities[1]) kprintf("FAIL e remove: b");
    if (vsmap_data(&pool, d) != &entities[3]) kprintf("FAIL e remove: d");
    if (vsmap_data(&pool, e) != NULL) kprintf("FAIL e remove: e still valid");
    if (vsmap_data(&pool, f) != &entities[5]) kprintf("FAIL e remove: f");

    /*
     * Remove f
     */

    vsmap_remove(&pool, f);

    kprintf("vsmap_t BIND: remove f");

    vsmap_dump(&pool);

    if (vsmap_data(&pool, b) != &entities[1]) kprintf("FAIL f remove: b");
    if (vsmap_data(&pool, d) != &entities[3]) kprintf("FAIL f remove: d");
    if (vsmap_data(&pool, f) != NULL) kprintf("FAIL f remove: f still valid");

    /*
     * Remove d
     */

    vsmap_remove(&pool, d);

    kprintf("vsmap_t BIND: remove d");

    vsmap_dump(&pool);

    if (pool.count != 1) kprintf("FAIL final count");
    if (vsmap_data(&pool, b) != &entities[1]) kprintf("FAIL final: b");
    if (vsmap_data(&pool, d) != NULL) kprintf("FAIL final: d still valid");

    /*
     * Remove b
     */

    vsmap_remove(&pool, b);

    kprintf("vsmap_t BIND: remove b");

    vsmap_dump(&pool);

    if (pool.count != 0) kprintf("FAIL empty count");
    if (vsmap_data(&pool, b) != NULL) kprintf("FAIL final: b still valid");

    kprintf("vsmap_t BIND: OK");
}

static void test_VSMAP_ALLOC(void)
{
    vsmap_t pool = VSMAP_ALLOC(MEM_alloc, 6);

    Entity entities[6] = { {0}, {1}, {2}, {3}, {4}, {5}, };

    int a = vsmap_add(&pool, &entities[0]);
    int b = vsmap_add(&pool, &entities[1]);
    int c = vsmap_add(&pool, &entities[2]);
    int d = vsmap_add(&pool, &entities[3]);
    int e = vsmap_add(&pool, &entities[4]);
    int f = vsmap_add(&pool, &entities[5]);

    kprintf("vsmap_t ALLOC: add");

    vsmap_dump(&pool);

    if (pool.capacity != 6) kprintf("FAIL capacity");
    if (pool.count != 6) kprintf("FAIL count");

    if (vsmap_data(&pool, a) != &entities[0]) kprintf("FAIL a");
    if (vsmap_data(&pool, b) != &entities[1]) kprintf("FAIL b");
    if (vsmap_data(&pool, c) != &entities[2]) kprintf("FAIL c");
    if (vsmap_data(&pool, d) != &entities[3]) kprintf("FAIL d");
    if (vsmap_data(&pool, e) != &entities[4]) kprintf("FAIL e");
    if (vsmap_data(&pool, f) != &entities[5]) kprintf("FAIL f");

    /*
     * Remove c
     *
     * f ocupa el slot físico de c,
     * pero f sigue siendo accesible mediante su handle.
     */

    vsmap_remove(&pool, c);

    kprintf("vsmap_t ALLOC: remove c");

    vsmap_dump(&pool);

    if (vsmap_data(&pool, a) != &entities[0]) kprintf("FAIL c remove: a");
    if (vsmap_data(&pool, b) != &entities[1]) kprintf("FAIL c remove: b");
    if (vsmap_data(&pool, c) != NULL) kprintf("FAIL c remove: c still valid");
    if (vsmap_data(&pool, d) != &entities[3]) kprintf("FAIL c remove: d");
    if (vsmap_data(&pool, e) != &entities[4]) kprintf("FAIL c remove: e");
    if (vsmap_data(&pool, f) != &entities[5]) kprintf("FAIL c remove: f");

    Entity *entity;

    entity = vsmap_data(&pool, f);
    kprintf("HANDLE f: id=%d", entity->id);

    /*
     * Remove e
     */

    vsmap_remove(&pool, e);

    kprintf("vsmap_t ALLOC: remove e");

    vsmap_dump(&pool);

    if (vsmap_data(&pool, a) != &entities[0]) kprintf("FAIL e remove: a");
    if (vsmap_data(&pool, b) != &entities[1]) kprintf("FAIL e remove: b");
    if (vsmap_data(&pool, c) != NULL) kprintf("FAIL e remove: c still valid");
    if (vsmap_data(&pool, d) != &entities[3]) kprintf("FAIL e remove: d");
    if (vsmap_data(&pool, e) != NULL) kprintf("FAIL e remove: e still valid");
    if (vsmap_data(&pool, f) != &entities[5]) kprintf("FAIL e remove: f");

    /*
     * Remove a
     */

    vsmap_remove(&pool, a);

    kprintf("vsmap_t ALLOC: remove a");

    vsmap_dump(&pool);

    if (vsmap_data(&pool, a) != NULL) kprintf("FAIL a remove: a still valid");
    if (vsmap_data(&pool, b) != &entities[1]) kprintf("FAIL a remove: b");
    if (vsmap_data(&pool, d) != &entities[3]) kprintf("FAIL a remove: d");
    if (vsmap_data(&pool, f) != &entities[5]) kprintf("FAIL a remove: f");

    /*
     * Remove b
     */

    vsmap_remove(&pool, b);

    kprintf("vsmap_t ALLOC: remove b");

    vsmap_dump(&pool);

    if (vsmap_data(&pool, b) != NULL) kprintf("FAIL b remove: b still valid");
    if (vsmap_data(&pool, d) != &entities[3]) kprintf("FAIL b remove: d");
    if (vsmap_data(&pool, f) != &entities[5]) kprintf("FAIL b remove: f");

    /*
     * Remove d
     */

    vsmap_remove(&pool, d);

    kprintf("vsmap_t ALLOC: remove d");

    vsmap_dump(&pool);

    if (vsmap_data(&pool, d) != NULL) kprintf("FAIL d remove: d still valid");
    if (vsmap_data(&pool, f) != &entities[5]) kprintf("FAIL d remove: f");

    /*
     * Remove f
     */

    vsmap_remove(&pool, f);

    kprintf("vsmap_t ALLOC: remove f");

    vsmap_dump(&pool);

    if (pool.count != 0) kprintf("FAIL final count");
    if (vsmap_data(&pool, f) != NULL) kprintf("FAIL f still valid");

    /*
     * Clear
     */

    vsmap_reset(&pool);

    kprintf("vsmap_t ALLOC: clear");

    vsmap_dump(&pool);

    if (pool.count != 0) kprintf("FAIL clear");

    MEM_free(pool.lookup);
    MEM_free(pool.pool);

    kprintf("vsmap_t ALLOC: OK");
}

static void test_vsmap_invalid(void)
{
    vsmap_t pool = VSMAP_ALLOC(MEM_alloc, 2);

    Entity entities[3] = { {0}, {1}, {2}, };

    kprintf("vsmap_t: invalid / capacity");

    vsmap_dump(&pool);

    if (vsmap_data(&pool, 0) != NULL) kprintf("FAIL empty data");
    if (vsmap_remove(&pool, 0) != -1) kprintf("FAIL empty remove");

    int a = vsmap_add(&pool, &entities[0]);
    int b = vsmap_add(&pool, &entities[1]);

    if (a != 0) kprintf("FAIL first handle");
    if (b != 1) kprintf("FAIL second handle");

    kprintf("vsmap_t: full");

    vsmap_dump(&pool);

    if (vsmap_add(&pool, &entities[2]) != -1) kprintf("FAIL full pool");
    if (pool.count != 2) kprintf("FAIL full count");

    if (vsmap_data(&pool, 2) != NULL) kprintf("FAIL invalid data");
    if (vsmap_remove(&pool, 2) != -1) kprintf("FAIL invalid remove");

    vsmap_remove(&pool, a);

    kprintf("vsmap_t: remove a");

    vsmap_dump(&pool);

    if (vsmap_data(&pool, a) != NULL) kprintf("FAIL removed a");
    if (vsmap_data(&pool, b) != &entities[1]) kprintf("FAIL b after a remove");

    MEM_free(pool.lookup);
    MEM_free(pool.pool);

    kprintf("vsmap_t: invalid / capacity OK");
}

/* ============================================================================
 * RUN
 * ============================================================================ */

void vsmap_run_tests(void)
{
    kprintf("--------------------------------");
    kprintf("vsmap_t TESTS");
    kprintf("--------------------------------");

    test_VSMAP_BIND();
    test_VSMAP_ALLOC();
    test_vsmap_invalid();

    kprintf("--------------------------------");
    kprintf("vsmap_t TESTS DONE");
    kprintf("--------------------------------");
}