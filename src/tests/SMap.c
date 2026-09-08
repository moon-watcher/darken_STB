#include <genesis.h>

#include "../SMap.h"

typedef struct
{
    uint16_t id;
} Entity;

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

static void SMap_dump(SMap_t *pool)
{
    kprintf("SMap_t DUMP: count=%d capacity=%d", pool->count, pool->capacity);

    for (uint16_t i = 0; i < pool->count; ++i)
    {
        Entity *entity = SMap_data(pool, pool->pool[i].handle);
        kprintf("  [%d] handle=%d ptr=%x id=%d", i, pool->pool[i].handle, entity, entity->id);
    }
}

/* ============================================================================
 * TESTS
 * ============================================================================ */

static void test_SMap_bind(void)
{
    SMap_DECLARE(storage, 6);
    SMap_t pool = SMap_BIND(storage);

    Entity entities[6] = { {0}, {1}, {2}, {3}, {4}, {5}, };

    int a = SMap_add(&pool, &entities[0]);
    int b = SMap_add(&pool, &entities[1]);
    int c = SMap_add(&pool, &entities[2]);
    int d = SMap_add(&pool, &entities[3]);
    int e = SMap_add(&pool, &entities[4]);
    int f = SMap_add(&pool, &entities[5]);

    kprintf("SMap_t BIND: add");

    SMap_dump(&pool);

    if (SMap_data(&pool, a) != &entities[0]) kprintf("FAIL a");
    if (SMap_data(&pool, b) != &entities[1]) kprintf("FAIL b");
    if (SMap_data(&pool, c) != &entities[2]) kprintf("FAIL c");
    if (SMap_data(&pool, d) != &entities[3]) kprintf("FAIL d");
    if (SMap_data(&pool, e) != &entities[4]) kprintf("FAIL e");
    if (SMap_data(&pool, f) != &entities[5]) kprintf("FAIL f");

    /*
     * Remove c
     *
     * c = handle 2
     *
     * El elemento de f pasa al slot físico de c,
     * pero el handle de f sigue siendo válido.
     */

    Entity *entity;
    entity = SMap_data(&pool, f); kprintf(" -> %d", entity->id);

    SMap_remove(&pool, c);

    entity = SMap_data(&pool, f); kprintf(" -> %d", entity->id);

    kprintf("SMap_t BIND: remove c");

    SMap_dump(&pool);

    if (SMap_data(&pool, a) != &entities[0]) kprintf("FAIL c remove: a");
    if (SMap_data(&pool, b) != &entities[1]) kprintf("FAIL c remove: b");
    if (SMap_data(&pool, c) != NULL) kprintf("FAIL c remove: c still valid");
    if (SMap_data(&pool, d) != &entities[3]) kprintf("FAIL c remove: d");
    if (SMap_data(&pool, e) != &entities[4]) kprintf("FAIL c remove: e");
    if (SMap_data(&pool, f) != &entities[5]) kprintf("FAIL c remove: f");

    /*
     * f debe seguir apuntando a entity 5 aunque su slot físico haya cambiado.
     */

    entity = SMap_data(&pool, f);
    kprintf("HANDLE f: id=%d", entity->id);

    /*
     * Remove a
     *
     * El último elemento pasa al slot físico de a.
     * Los handles siguen siendo estables.
     */

    SMap_remove(&pool, a);

    kprintf("SMap_t BIND: remove a");

    SMap_dump(&pool);

    if (SMap_data(&pool, a) != NULL) kprintf("FAIL a remove: a still valid");
    if (SMap_data(&pool, b) != &entities[1]) kprintf("FAIL a remove: b");
    if (SMap_data(&pool, d) != &entities[3]) kprintf("FAIL a remove: d");
    if (SMap_data(&pool, e) != &entities[4]) kprintf("FAIL a remove: e");
    if (SMap_data(&pool, f) != &entities[5]) kprintf("FAIL a remove: f");

    /*
     * Remove e
     */

    SMap_remove(&pool, e);

    kprintf("SMap_t BIND: remove e");

    SMap_dump(&pool);

    if (SMap_data(&pool, b) != &entities[1]) kprintf("FAIL e remove: b");
    if (SMap_data(&pool, d) != &entities[3]) kprintf("FAIL e remove: d");
    if (SMap_data(&pool, e) != NULL) kprintf("FAIL e remove: e still valid");
    if (SMap_data(&pool, f) != &entities[5]) kprintf("FAIL e remove: f");

    /*
     * Remove f
     */

    SMap_remove(&pool, f);

    kprintf("SMap_t BIND: remove f");

    SMap_dump(&pool);

    if (SMap_data(&pool, b) != &entities[1]) kprintf("FAIL f remove: b");
    if (SMap_data(&pool, d) != &entities[3]) kprintf("FAIL f remove: d");
    if (SMap_data(&pool, f) != NULL) kprintf("FAIL f remove: f still valid");

    /*
     * Remove d
     */

    SMap_remove(&pool, d);

    kprintf("SMap_t BIND: remove d");

    SMap_dump(&pool);

    if (pool.count != 1) kprintf("FAIL final count");
    if (SMap_data(&pool, b) != &entities[1]) kprintf("FAIL final: b");
    if (SMap_data(&pool, d) != NULL) kprintf("FAIL final: d still valid");

    /*
     * Remove b
     */

    SMap_remove(&pool, b);

    kprintf("SMap_t BIND: remove b");

    SMap_dump(&pool);

    if (pool.count != 0) kprintf("FAIL empty count");
    if (SMap_data(&pool, b) != NULL) kprintf("FAIL final: b still valid");

    kprintf("SMap_t BIND: OK");
}

static void test_SMap_alloc(void)
{
    SMap_t pool = SMap_ALLOC(MEM_alloc, 6);

    Entity entities[6] = { {0}, {1}, {2}, {3}, {4}, {5}, };

    int a = SMap_add(&pool, &entities[0]);
    int b = SMap_add(&pool, &entities[1]);
    int c = SMap_add(&pool, &entities[2]);
    int d = SMap_add(&pool, &entities[3]);
    int e = SMap_add(&pool, &entities[4]);
    int f = SMap_add(&pool, &entities[5]);

    kprintf("SMap_t ALLOC: add");

    SMap_dump(&pool);

    if (pool.capacity != 6) kprintf("FAIL capacity");
    if (pool.count != 6) kprintf("FAIL count");

    if (SMap_data(&pool, a) != &entities[0]) kprintf("FAIL a");
    if (SMap_data(&pool, b) != &entities[1]) kprintf("FAIL b");
    if (SMap_data(&pool, c) != &entities[2]) kprintf("FAIL c");
    if (SMap_data(&pool, d) != &entities[3]) kprintf("FAIL d");
    if (SMap_data(&pool, e) != &entities[4]) kprintf("FAIL e");
    if (SMap_data(&pool, f) != &entities[5]) kprintf("FAIL f");

    /*
     * Remove c
     *
     * f ocupa el slot físico de c,
     * pero f sigue siendo accesible mediante su handle.
     */

    SMap_remove(&pool, c);

    kprintf("SMap_t ALLOC: remove c");

    SMap_dump(&pool);

    if (SMap_data(&pool, a) != &entities[0]) kprintf("FAIL c remove: a");
    if (SMap_data(&pool, b) != &entities[1]) kprintf("FAIL c remove: b");
    if (SMap_data(&pool, c) != NULL) kprintf("FAIL c remove: c still valid");
    if (SMap_data(&pool, d) != &entities[3]) kprintf("FAIL c remove: d");
    if (SMap_data(&pool, e) != &entities[4]) kprintf("FAIL c remove: e");
    if (SMap_data(&pool, f) != &entities[5]) kprintf("FAIL c remove: f");

    Entity *entity;

    entity = SMap_data(&pool, f);
    kprintf("HANDLE f: id=%d", entity->id);

    /*
     * Remove e
     */

    SMap_remove(&pool, e);

    kprintf("SMap_t ALLOC: remove e");

    SMap_dump(&pool);

    if (SMap_data(&pool, a) != &entities[0]) kprintf("FAIL e remove: a");
    if (SMap_data(&pool, b) != &entities[1]) kprintf("FAIL e remove: b");
    if (SMap_data(&pool, c) != NULL) kprintf("FAIL e remove: c still valid");
    if (SMap_data(&pool, d) != &entities[3]) kprintf("FAIL e remove: d");
    if (SMap_data(&pool, e) != NULL) kprintf("FAIL e remove: e still valid");
    if (SMap_data(&pool, f) != &entities[5]) kprintf("FAIL e remove: f");

    /*
     * Remove a
     */

    SMap_remove(&pool, a);

    kprintf("SMap_t ALLOC: remove a");

    SMap_dump(&pool);

    if (SMap_data(&pool, a) != NULL) kprintf("FAIL a remove: a still valid");
    if (SMap_data(&pool, b) != &entities[1]) kprintf("FAIL a remove: b");
    if (SMap_data(&pool, d) != &entities[3]) kprintf("FAIL a remove: d");
    if (SMap_data(&pool, f) != &entities[5]) kprintf("FAIL a remove: f");

    /*
     * Remove b
     */

    SMap_remove(&pool, b);

    kprintf("SMap_t ALLOC: remove b");

    SMap_dump(&pool);

    if (SMap_data(&pool, b) != NULL) kprintf("FAIL b remove: b still valid");
    if (SMap_data(&pool, d) != &entities[3]) kprintf("FAIL b remove: d");
    if (SMap_data(&pool, f) != &entities[5]) kprintf("FAIL b remove: f");

    /*
     * Remove d
     */

    SMap_remove(&pool, d);

    kprintf("SMap_t ALLOC: remove d");

    SMap_dump(&pool);

    if (SMap_data(&pool, d) != NULL) kprintf("FAIL d remove: d still valid");
    if (SMap_data(&pool, f) != &entities[5]) kprintf("FAIL d remove: f");

    /*
     * Remove f
     */

    SMap_remove(&pool, f);

    kprintf("SMap_t ALLOC: remove f");

    SMap_dump(&pool);

    if (pool.count != 0) kprintf("FAIL final count");
    if (SMap_data(&pool, f) != NULL) kprintf("FAIL f still valid");

    /*
     * Clear
     */

    SMap_reset(&pool);

    kprintf("SMap_t ALLOC: clear");

    SMap_dump(&pool);

    if (pool.count != 0) kprintf("FAIL clear");

    MEM_free(pool.lookup);
    MEM_free(pool.pool);

    kprintf("SMap_t ALLOC: OK");
}

static void test_SMap_invalid(void)
{
    SMap_t pool = SMap_ALLOC(MEM_alloc, 2);

    Entity entities[3] = { {0}, {1}, {2}, };

    kprintf("SMap_t: invalid / capacity");

    SMap_dump(&pool);

    if (SMap_data(&pool, 0) != NULL) kprintf("FAIL empty data");
    if (SMap_remove(&pool, 0) != -1) kprintf("FAIL empty remove");

    int a = SMap_add(&pool, &entities[0]);
    int b = SMap_add(&pool, &entities[1]);

    if (a != 0) kprintf("FAIL first handle");
    if (b != 1) kprintf("FAIL second handle");

    kprintf("SMap_t: full");

    SMap_dump(&pool);

    if (SMap_add(&pool, &entities[2]) != -1) kprintf("FAIL full pool");
    if (pool.count != 2) kprintf("FAIL full count");

    if (SMap_data(&pool, 2) != NULL) kprintf("FAIL invalid data");
    if (SMap_remove(&pool, 2) != -1) kprintf("FAIL invalid remove");

    SMap_remove(&pool, a);

    kprintf("SMap_t: remove a");

    SMap_dump(&pool);

    if (SMap_data(&pool, a) != NULL) kprintf("FAIL removed a");
    if (SMap_data(&pool, b) != &entities[1]) kprintf("FAIL b after a remove");

    MEM_free(pool.lookup);
    MEM_free(pool.pool);

    kprintf("SMap_t: invalid / capacity OK");
}

/* ============================================================================
 * RUN
 * ============================================================================ */

void smap_run_tests(void)
{
    kprintf("--------------------------------");
    kprintf("SMap_t TESTS");
    kprintf("--------------------------------");

    test_SMap_bind();
    test_SMap_alloc();
    test_SMap_invalid();

    kprintf("--------------------------------");
    kprintf("SMap_t TESTS DONE");
    kprintf("--------------------------------");
}