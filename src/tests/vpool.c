#include <genesis.h>

#include "../vpool.h"

typedef struct
{
    uint16_t id;
} Entity;

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

static void vpool_dump(Vpool *pool)
{
    kprintf("VPOOL DUMP: count=%d capacity=%d", pool->count, pool->capacity);

    for (uint16_t i = 0; i < pool->count; ++i)
    {
        Entity *entity = vpool_data(pool, pool->pool[i].handle);
        kprintf("  [%d] handle=%d ptr=%x id=%d", i, pool->pool[i].handle, entity, entity->id);
    }
}

/* ============================================================================
 * TESTS
 * ============================================================================ */

static void test_vpool_bind(void)
{
    VPOOL_DECLARE(storage, 6);
    Vpool pool = VPOOL_BIND(storage);

    Entity entities[6] = { {0}, {1}, {2}, {3}, {4}, {5}, };

    int a = vpool_add(&pool, &entities[0]);
    int b = vpool_add(&pool, &entities[1]);
    int c = vpool_add(&pool, &entities[2]);
    int d = vpool_add(&pool, &entities[3]);
    int e = vpool_add(&pool, &entities[4]);
    int f = vpool_add(&pool, &entities[5]);

    kprintf("VPOOL BIND: add");

    vpool_dump(&pool);

    if (vpool_data(&pool, a) != &entities[0]) kprintf("FAIL a");
    if (vpool_data(&pool, b) != &entities[1]) kprintf("FAIL b");
    if (vpool_data(&pool, c) != &entities[2]) kprintf("FAIL c");
    if (vpool_data(&pool, d) != &entities[3]) kprintf("FAIL d");
    if (vpool_data(&pool, e) != &entities[4]) kprintf("FAIL e");
    if (vpool_data(&pool, f) != &entities[5]) kprintf("FAIL f");

    /*
     * Remove c
     *
     * c = handle 2
     *
     * El elemento de f pasa al slot físico de c,
     * pero el handle de f sigue siendo válido.
     */


    Entity *entity;
    entity = vpool_data(&pool, f); kprintf(" -> %d", entity->id);

    vpool_remove(&pool, c);

    entity = vpool_data(&pool, f); kprintf(" -> %d", entity->id);

    



    kprintf("VPOOL BIND: remove c");

    vpool_dump(&pool);

    if (vpool_data(&pool, a) != &entities[0]) kprintf("FAIL c remove: a");
    if (vpool_data(&pool, b) != &entities[1]) kprintf("FAIL c remove: b");
    if (vpool_data(&pool, c) != NULL) kprintf("FAIL c remove: c still valid");
    if (vpool_data(&pool, d) != &entities[3]) kprintf("FAIL c remove: d");
    if (vpool_data(&pool, e) != &entities[4]) kprintf("FAIL c remove: e");
    if (vpool_data(&pool, f) != &entities[5]) kprintf("FAIL c remove: f");

    /*
     * f debe seguir apuntando a entity 5 aunque su slot físico haya cambiado.
     */

    

    entity = vpool_data(&pool, f);
    kprintf("HANDLE f: id=%d", entity->id);

    /*
     * Remove a
     *
     * El último elemento pasa al slot físico de a.
     * Los handles siguen siendo estables.
     */

    vpool_remove(&pool, a);

    kprintf("VPOOL BIND: remove a");

    vpool_dump(&pool);

    if (vpool_data(&pool, a) != NULL) kprintf("FAIL a remove: a still valid");
    if (vpool_data(&pool, b) != &entities[1]) kprintf("FAIL a remove: b");
    if (vpool_data(&pool, d) != &entities[3]) kprintf("FAIL a remove: d");
    if (vpool_data(&pool, e) != &entities[4]) kprintf("FAIL a remove: e");
    if (vpool_data(&pool, f) != &entities[5]) kprintf("FAIL a remove: f");

    /*
     * Remove e
     */

    vpool_remove(&pool, e);

    kprintf("VPOOL BIND: remove e");

    vpool_dump(&pool);

    if (vpool_data(&pool, b) != &entities[1]) kprintf("FAIL e remove: b");
    if (vpool_data(&pool, d) != &entities[3]) kprintf("FAIL e remove: d");
    if (vpool_data(&pool, e) != NULL) kprintf("FAIL e remove: e still valid");
    if (vpool_data(&pool, f) != &entities[5]) kprintf("FAIL e remove: f");

    /*
     * Remove f
     */

    vpool_remove(&pool, f);

    kprintf("VPOOL BIND: remove f");

    vpool_dump(&pool);

    if (vpool_data(&pool, b) != &entities[1]) kprintf("FAIL f remove: b");
    if (vpool_data(&pool, d) != &entities[3]) kprintf("FAIL f remove: d");
    if (vpool_data(&pool, f) != NULL) kprintf("FAIL f remove: f still valid");

    /*
     * Remove d
     */

    vpool_remove(&pool, d);

    kprintf("VPOOL BIND: remove d");

    vpool_dump(&pool);

    if (pool.count != 1) kprintf("FAIL final count");
    if (vpool_data(&pool, b) != &entities[1]) kprintf("FAIL final: b");
    if (vpool_data(&pool, d) != NULL) kprintf("FAIL final: d still valid");

    /*
     * Remove b
     */

    vpool_remove(&pool, b);

    kprintf("VPOOL BIND: remove b");

    vpool_dump(&pool);

    if (pool.count != 0) kprintf("FAIL empty count");
    if (vpool_data(&pool, b) != NULL) kprintf("FAIL final: b still valid");

    kprintf("VPOOL BIND: OK");
}

static void test_vpool_alloc(void)
{
    Vpool pool = VPOOL_ALLOC(MEM_alloc, 6);

    Entity entities[6] = { {0}, {1}, {2}, {3}, {4}, {5}, };

    int a = vpool_add(&pool, &entities[0]);
    int b = vpool_add(&pool, &entities[1]);
    int c = vpool_add(&pool, &entities[2]);
    int d = vpool_add(&pool, &entities[3]);
    int e = vpool_add(&pool, &entities[4]);
    int f = vpool_add(&pool, &entities[5]);

    kprintf("VPOOL ALLOC: add");

    vpool_dump(&pool);

    if (pool.capacity != 6) kprintf("FAIL capacity");
    if (pool.count != 6) kprintf("FAIL count");

    if (vpool_data(&pool, a) != &entities[0]) kprintf("FAIL a");
    if (vpool_data(&pool, b) != &entities[1]) kprintf("FAIL b");
    if (vpool_data(&pool, c) != &entities[2]) kprintf("FAIL c");
    if (vpool_data(&pool, d) != &entities[3]) kprintf("FAIL d");
    if (vpool_data(&pool, e) != &entities[4]) kprintf("FAIL e");
    if (vpool_data(&pool, f) != &entities[5]) kprintf("FAIL f");

    /*
     * Remove c
     *
     * f ocupa el slot físico de c,
     * pero f sigue siendo accesible mediante su handle.
     */

    vpool_remove(&pool, c);

    kprintf("VPOOL ALLOC: remove c");

    vpool_dump(&pool);

    if (vpool_data(&pool, a) != &entities[0]) kprintf("FAIL c remove: a");
    if (vpool_data(&pool, b) != &entities[1]) kprintf("FAIL c remove: b");
    if (vpool_data(&pool, c) != NULL) kprintf("FAIL c remove: c still valid");
    if (vpool_data(&pool, d) != &entities[3]) kprintf("FAIL c remove: d");
    if (vpool_data(&pool, e) != &entities[4]) kprintf("FAIL c remove: e");
    if (vpool_data(&pool, f) != &entities[5]) kprintf("FAIL c remove: f");

    Entity *entity;

    entity = vpool_data(&pool, f);
    kprintf("HANDLE f: id=%d", entity->id);

    /*
     * Remove e
     */

    vpool_remove(&pool, e);

    kprintf("VPOOL ALLOC: remove e");

    vpool_dump(&pool);

    if (vpool_data(&pool, a) != &entities[0]) kprintf("FAIL e remove: a");
    if (vpool_data(&pool, b) != &entities[1]) kprintf("FAIL e remove: b");
    if (vpool_data(&pool, c) != NULL) kprintf("FAIL e remove: c still valid");
    if (vpool_data(&pool, d) != &entities[3]) kprintf("FAIL e remove: d");
    if (vpool_data(&pool, e) != NULL) kprintf("FAIL e remove: e still valid");
    if (vpool_data(&pool, f) != &entities[5]) kprintf("FAIL e remove: f");

    /*
     * Remove a
     */

    vpool_remove(&pool, a);

    kprintf("VPOOL ALLOC: remove a");

    vpool_dump(&pool);

    if (vpool_data(&pool, a) != NULL) kprintf("FAIL a remove: a still valid");
    if (vpool_data(&pool, b) != &entities[1]) kprintf("FAIL a remove: b");
    if (vpool_data(&pool, d) != &entities[3]) kprintf("FAIL a remove: d");
    if (vpool_data(&pool, f) != &entities[5]) kprintf("FAIL a remove: f");

    /*
     * Remove b
     */

    vpool_remove(&pool, b);

    kprintf("VPOOL ALLOC: remove b");

    vpool_dump(&pool);

    if (vpool_data(&pool, b) != NULL) kprintf("FAIL b remove: b still valid");
    if (vpool_data(&pool, d) != &entities[3]) kprintf("FAIL b remove: d");
    if (vpool_data(&pool, f) != &entities[5]) kprintf("FAIL b remove: f");

    /*
     * Remove d
     */

    vpool_remove(&pool, d);

    kprintf("VPOOL ALLOC: remove d");

    vpool_dump(&pool);

    if (vpool_data(&pool, d) != NULL) kprintf("FAIL d remove: d still valid");
    if (vpool_data(&pool, f) != &entities[5]) kprintf("FAIL d remove: f");

    /*
     * Remove f
     */

    vpool_remove(&pool, f);

    kprintf("VPOOL ALLOC: remove f");

    vpool_dump(&pool);

    if (pool.count != 0) kprintf("FAIL final count");
    if (vpool_data(&pool, f) != NULL) kprintf("FAIL f still valid");

    /*
     * Clear
     */

    vpool_clear(&pool);

    kprintf("VPOOL ALLOC: clear");

    vpool_dump(&pool);

    if (pool.count != 0) kprintf("FAIL clear");

    MEM_free(pool.lookup);
    MEM_free(pool.pool);

    kprintf("VPOOL ALLOC: OK");
}

static void test_vpool_invalid(void)
{
    Vpool pool = VPOOL_ALLOC(MEM_alloc, 2);

    Entity entities[3] = { {0}, {1}, {2}, };

    kprintf("VPOOL: invalid / capacity");

    vpool_dump(&pool);

    if (vpool_data(&pool, 0) != NULL) kprintf("FAIL empty data");
    if (vpool_remove(&pool, 0) != -1) kprintf("FAIL empty remove");

    int a = vpool_add(&pool, &entities[0]);
    int b = vpool_add(&pool, &entities[1]);

    if (a != 0) kprintf("FAIL first handle");
    if (b != 1) kprintf("FAIL second handle");

    kprintf("VPOOL: full");

    vpool_dump(&pool);

    if (vpool_add(&pool, &entities[2]) != -1) kprintf("FAIL full pool");
    if (pool.count != 2) kprintf("FAIL full count");

    if (vpool_data(&pool, 2) != NULL) kprintf("FAIL invalid data");
    if (vpool_remove(&pool, 2) != -1) kprintf("FAIL invalid remove");

    vpool_remove(&pool, a);

    kprintf("VPOOL: remove a");

    vpool_dump(&pool);

    if (vpool_data(&pool, a) != NULL) kprintf("FAIL removed a");
    if (vpool_data(&pool, b) != &entities[1]) kprintf("FAIL b after a remove");

    MEM_free(pool.lookup);
    MEM_free(pool.pool);

    kprintf("VPOOL: invalid / capacity OK");
}

/* ============================================================================
 * RUN
 * ============================================================================ */

void vpool_run_tests(void)
{
    kprintf("--------------------------------");
    kprintf("VPOOL TESTS");
    kprintf("--------------------------------");

    test_vpool_bind();
    test_vpool_alloc();
    test_vpool_invalid();

    kprintf("--------------------------------");
    kprintf("VPOOL TESTS DONE");
    kprintf("--------------------------------");
}