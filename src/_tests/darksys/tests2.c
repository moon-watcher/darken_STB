#include <genesis.h>

#include "../../darksys.h"

typedef struct
{
    uint16_t id;
} Entity;

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

static void darksys_dump(darksys *system)
{
    return;
    kprintf("DARKSYS DUMP: count=%d capacity=%d params=%d size=%d",
            system->count,
            system->capacity,
            system->params,
            system->size);

    for (uint16_t i = 0; i < system->count; ++i)
    {
        uint16_t handle = system->handles[i];
        void **data = darksys_data(system, handle);

        kprintf("  [%d] handle=%d", i, handle);

        for (uint16_t p = 0; p < system->params; ++p)
            kprintf("    ptr[%d]=%x", p, data[p]);
    }
}

/* ============================================================================
 * TEST ADD
 * ============================================================================ */

static void test_darksys_add_1(void)
{
    kprintf("TEST: ADD 1");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 1);

    Entity a = {10};
    int16_t handle = DARKSYS_ADD(&system, &a);

    void **data = darksys_data(&system, handle);

    if (handle != 0) kprintf("FAIL handle");
    if (system.count != 1) kprintf("FAIL count");
    if (system.size != 1) kprintf("FAIL size");
    if (data[0] != &a) kprintf("FAIL data");

    kprintf("handle=%d id=%d", handle, ((Entity *)data[0])->id);

    darksys_dump(&system);

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

static void test_darksys_add_2(void)
{
    kprintf("TEST: ADD 2");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 2);

    Entity a = {10};
    Entity b = {20};

    int16_t handle = DARKSYS_ADD(&system, &a, &b);

    void **data = darksys_data(&system, handle);

    if (handle != 0) kprintf("FAIL handle");
    if (system.count != 1) kprintf("FAIL count");
    if (system.size != 2) kprintf("FAIL size");
    if (data[0] != &a) kprintf("FAIL data 0");
    if (data[1] != &b) kprintf("FAIL data 1");

    kprintf("handle=%d ids=%d,%d",
            handle,
            ((Entity *)data[0])->id,
            ((Entity *)data[1])->id);

    darksys_dump(&system);

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

static void test_darksys_add_3(void)
{
    kprintf("TEST: ADD 3");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 3);

    Entity a = {10};
    Entity b = {20};
    Entity c = {30};

    int16_t handle = DARKSYS_ADD(&system, &a, &b, &c);

    void **data = darksys_data(&system, handle);

    if (handle != 0) kprintf("FAIL handle");
    if (system.count != 1) kprintf("FAIL count");
    if (system.size != 3) kprintf("FAIL size");
    if (data[0] != &a) kprintf("FAIL data 0");
    if (data[1] != &b) kprintf("FAIL data 1");
    if (data[2] != &c) kprintf("FAIL data 2");

    kprintf("handle=%d ids=%d,%d,%d",
            handle,
            ((Entity *)data[0])->id,
            ((Entity *)data[1])->id,
            ((Entity *)data[2])->id);

    darksys_dump(&system);

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

static void test_darksys_add_4(void)
{
    kprintf("TEST: ADD 4");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 4);

    Entity a = {10};
    Entity b = {20};
    Entity c = {30};
    Entity d = {40};

    int16_t handle = DARKSYS_ADD(&system, &a, &b, &c, &d);

    void **data = darksys_data(&system, handle);

    if (handle != 0) kprintf("FAIL handle");
    if (system.count != 1) kprintf("FAIL count");
    if (system.size != 4) kprintf("FAIL size");
    if (data[0] != &a) kprintf("FAIL data 0");
    if (data[1] != &b) kprintf("FAIL data 1");
    if (data[2] != &c) kprintf("FAIL data 2");
    if (data[3] != &d) kprintf("FAIL data 3");

    kprintf("handle=%d ids=%d,%d,%d,%d",
            handle,
            ((Entity *)data[0])->id,
            ((Entity *)data[1])->id,
            ((Entity *)data[2])->id,
            ((Entity *)data[3])->id);

    darksys_dump(&system);

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

static void test_darksys_add_5(void)
{
    kprintf("TEST: ADD 5");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 5);

    Entity a = {10};
    Entity b = {20};
    Entity c = {30};
    Entity d = {40};
    Entity e = {50};

    int16_t handle = DARKSYS_ADD(&system, &a, &b, &c, &d, &e);

    void **data = darksys_data(&system, handle);

    if (handle != 0) kprintf("FAIL handle");
    if (system.count != 1) kprintf("FAIL count");
    if (system.size != 5) kprintf("FAIL size");
    if (data[0] != &a) kprintf("FAIL data 0");
    if (data[1] != &b) kprintf("FAIL data 1");
    if (data[2] != &c) kprintf("FAIL data 2");
    if (data[3] != &d) kprintf("FAIL data 3");
    if (data[4] != &e) kprintf("FAIL data 4");

    kprintf("handle=%d ids=%d,%d,%d,%d,%d",
            handle,
            ((Entity *)data[0])->id,
            ((Entity *)data[1])->id,
            ((Entity *)data[2])->id,
            ((Entity *)data[3])->id,
            ((Entity *)data[4])->id);

    darksys_dump(&system);

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

/* ============================================================================
 * STABLE HANDLES
 * ============================================================================ */

static void test_darksys_stable_handle(void)
{
    kprintf("TEST: stable handle");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 6, 3);

    Entity a = {10};
    Entity b = {20};
    Entity c = {30};
    Entity d = {40};
    Entity e = {50};
    Entity f = {60};

    int16_t ha = DARKSYS_ADD(&system, &a, &b, &c);
    int16_t hb = DARKSYS_ADD(&system, &d, &e, &f);

    kprintf("ha=%d hb=%d", ha, hb);

    void **data;

    data = darksys_data(&system, hb);

    if (data[0] != &d) kprintf("FAIL before 0");
    if (data[1] != &e) kprintf("FAIL before 1");
    if (data[2] != &f) kprintf("FAIL before 2");

    kprintf("before -> %d,%d,%d",
            ((Entity *)data[0])->id,
            ((Entity *)data[1])->id,
            ((Entity *)data[2])->id);

    /*
     * Remove the first group.
     *
     * The second group moves from slot 1 to slot 0.
     * hb must still point to d,e,f.
     */

    darksys_remove(&system, ha);

    data = darksys_data(&system, hb);

    if (data[0] != &d) kprintf("FAIL after 0");
    if (data[1] != &e) kprintf("FAIL after 1");
    if (data[2] != &f) kprintf("FAIL after 2");

    kprintf("after  -> %d,%d,%d",
            ((Entity *)data[0])->id,
            ((Entity *)data[1])->id,
            ((Entity *)data[2])->id);

    if (darksys_data(&system, ha) != 0)
        kprintf("FAIL removed handle still valid");

    darksys_dump(&system);

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

/* ============================================================================
 * HANDLE REUSE
 * ============================================================================ */

static void test_darksys_handle_reuse(void)
{
    kprintf("TEST: handle reuse");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 3, 2);

    Entity a = {10};
    Entity b = {20};
    Entity c = {30};
    Entity d = {40};

    int16_t ha = DARKSYS_ADD(&system, &a, &b);
    int16_t hb = DARKSYS_ADD(&system, &c, &d);

    kprintf("before ha=%d hb=%d", ha, hb);

    darksys_remove(&system, ha);

    int16_t hc = DARKSYS_ADD(&system, &a, &b);

    kprintf("new hc=%d old ha=%d", hc, ha);

    if (hc != ha)
        kprintf("FAIL handle not reused");

    darksys_dump(&system);

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

/* ============================================================================
 * REMOVE
 * ============================================================================ */

static void test_darksys_remove_last(void)
{
    kprintf("TEST: remove last");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 2);

    Entity a = {10};
    Entity b = {20};
    Entity c = {30};
    Entity d = {40};

    int16_t ha = DARKSYS_ADD(&system, &a, &b);
    int16_t hb = DARKSYS_ADD(&system, &c, &d);

    darksys_remove(&system, hb);

    if (system.count != 1)
        kprintf("FAIL count");

    if (system.size != 2)
        kprintf("FAIL size");

    if (darksys_data(&system, hb) != 0)
        kprintf("FAIL removed handle");

    if (darksys_data(&system, ha)[0] != &a)
        kprintf("FAIL a");

    darksys_dump(&system);

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

static void test_darksys_remove_swap(void)
{
    kprintf("TEST: remove swap");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 2);

    Entity a = {10};
    Entity b = {20};
    Entity c = {30};
    Entity d = {40};
    Entity e = {50};
    Entity f = {60};

    int16_t ha = DARKSYS_ADD(&system, &a, &b);
    int16_t hb = DARKSYS_ADD(&system, &c, &d);
    int16_t hc = DARKSYS_ADD(&system, &e, &f);

    darksys_remove(&system, hb);

    if (darksys_data(&system, ha)[0] != &a)
        kprintf("FAIL a");

    if (darksys_data(&system, ha)[1] != &b)
        kprintf("FAIL b");

    if (darksys_data(&system, hc)[0] != &e)
        kprintf("FAIL e");

    if (darksys_data(&system, hc)[1] != &f)
        kprintf("FAIL f");

    if (darksys_data(&system, hb) != 0)
        kprintf("FAIL removed handle");

    if (system.count != 2)
        kprintf("FAIL count");

    if (system.size != 4)
        kprintf("FAIL size");

    darksys_dump(&system);

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

/* ============================================================================
 * INVALID HANDLES
 * ============================================================================ */

static void test_darksys_invalid(void)
{
    kprintf("TEST: invalid handles");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 2);

    Entity a = {10};
    Entity b = {20};

    int16_t handle = DARKSYS_ADD(&system, &a, &b);

    if (darksys_data(&system, 100) != 0)
        kprintf("FAIL invalid data");

    if (darksys_remove(&system, 100) != -1)
        kprintf("FAIL invalid remove");

    darksys_remove(&system, handle);

    if (darksys_data(&system, handle) != 0)
        kprintf("FAIL removed data");

    if (darksys_remove(&system, handle) != -1)
        kprintf("FAIL remove twice");

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

/* ============================================================================
 * FULL
 * ============================================================================ */

static void test_darksys_full(void)
{
    kprintf("TEST: full");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 2, 2);

    Entity a = {10};
    Entity b = {20};
    Entity c = {30};
    Entity d = {40};
    Entity e = {50};
    Entity f = {60};

    int16_t ha = DARKSYS_ADD(&system, &a, &b);
    int16_t hb = DARKSYS_ADD(&system, &c, &d);
    int16_t hc = DARKSYS_ADD(&system, &e, &f);

    if (ha != 0)
        kprintf("FAIL ha");

    if (hb != 1)
        kprintf("FAIL hb");

    if (hc != -1)
        kprintf("FAIL full");

    if (system.count != 2)
        kprintf("FAIL full count");

    if (system.size != 4)
        kprintf("FAIL full size");

    darksys_dump(&system);

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

/* ============================================================================
 * CLEAR
 * ============================================================================ */

static void test_darksys_clear(void)
{
    kprintf("TEST: clear");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 3);

    Entity a = {10};
    Entity b = {20};
    Entity c = {30};

    int16_t handle = DARKSYS_ADD(&system, &a, &b, &c);

    if (system.count != 1)
        kprintf("FAIL before count");

    if (system.size != 3)
        kprintf("FAIL before size");

    darksys_clear(&system);

    if (system.count != 0)
        kprintf("FAIL clear count");

    if (system.size != 0)
        kprintf("FAIL clear size");

    if (darksys_data(&system, handle) != 0)
        kprintf("FAIL clear handle");

    if (system.next != 0)
        kprintf("FAIL clear next");

    darksys_dump(&system);

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

/* ============================================================================
 * STATIC BIND
 * ============================================================================ */

static void test_darksys_bind(void)
{
    kprintf("TEST: static bind");

    DARKSYS_POOL_DECLARE(storage, 4, 2);

    darksys system = DARKSYS_POOL_BIND(storage);

    Entity a = {10};
    Entity b = {20};

    int16_t handle = DARKSYS_ADD(&system, &a, &b);

    void **data = darksys_data(&system, handle);

    if (system.capacity != 4)
        kprintf("FAIL capacity");

    if (system.params != 2)
        kprintf("FAIL params");

    if (data[0] != &a)
        kprintf("FAIL data a");

    if (data[1] != &b)
        kprintf("FAIL data b");

    darksys_dump(&system);
}

/* ============================================================================
 * STATIC INIT
 * ============================================================================ */

static void test_darksys_init(void)
{
    kprintf("TEST: static init");

    DARKSYS_POOL_DECLARE(storage, 4, 2);

    darksys system = DARKSYS_POOL_INIT(storage, 4, 2);

    Entity a = {10};
    Entity b = {20};

    int16_t handle = DARKSYS_ADD(&system, &a, &b);

    void **data = darksys_data(&system, handle);

    if (data[0] != &a)
        kprintf("FAIL data a");

    if (data[1] != &b)
        kprintf("FAIL data b");

    if (system.count != 1)
        kprintf("FAIL count");

    darksys_dump(&system);
}

/* ============================================================================
 * FOREACH
 * ============================================================================ */

static void test_darksys_foreach_1(void)
{
    kprintf("TEST: FOREACH 1");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 1);

    Entity a = {10};
    Entity b = {20};

    DARKSYS_ADD(&system, &a);
    DARKSYS_ADD(&system, &b);

    void *x;

    DARKSYS_FOREACH(&system, x,
    {
        kprintf("  x=%d", ((Entity *)x)->id);
    });

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

static void test_darksys_foreach_2(void)
{
    kprintf("TEST: FOREACH 2");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 2);

    Entity a = {10};
    Entity b = {20};
    Entity c = {30};
    Entity d = {40};

    DARKSYS_ADD(&system, &a, &b);
    DARKSYS_ADD(&system, &c, &d);

    void *x;
    void *y;

    DARKSYS_FOREACH(&system, x, y,
    {
        kprintf("  x=%d y=%d",
                ((Entity *)x)->id,
                ((Entity *)y)->id);
    });

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

static void test_darksys_foreach_3(void)
{
    kprintf("TEST: FOREACH 3");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 3);

    Entity a = {10};
    Entity b = {20};
    Entity c = {30};

    DARKSYS_ADD(&system, &a, &b, &c);

    void *x;
    void *y;
    void *z;

    DARKSYS_FOREACH(&system, x, y, z,
    {
        kprintf("  x=%d y=%d z=%d",
                ((Entity *)x)->id,
                ((Entity *)y)->id,
                ((Entity *)z)->id);
    });

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

static void test_darksys_foreach_4(void)
{
    kprintf("TEST: FOREACH 4");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 4);

    Entity a = {10};
    Entity b = {20};
    Entity c = {30};
    Entity d = {40};

    DARKSYS_ADD(&system, &a, &b, &c, &d);

    void *x;
    void *y;
    void *z;
    void *w;

    DARKSYS_FOREACH(&system, x, y, z, w,
    {
        kprintf("  x=%d y=%d z=%d w=%d",
                ((Entity *)x)->id,
                ((Entity *)y)->id,
                ((Entity *)z)->id,
                ((Entity *)w)->id);
    });

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

static void test_darksys_foreach_5(void)
{
    kprintf("TEST: FOREACH 5");

    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 5);

    Entity a = {10};
    Entity b = {20};
    Entity c = {30};
    Entity d = {40};
    Entity e = {50};

    DARKSYS_ADD(&system, &a, &b, &c, &d, &e);

    void *x;
    void *y;
    void *z;
    void *w;
    void *v;

    DARKSYS_FOREACH(&system, x, y, z, w, v,
    {
        kprintf("  x=%d y=%d z=%d w=%d v=%d",
                ((Entity *)x)->id,
                ((Entity *)y)->id,
                ((Entity *)z)->id,
                ((Entity *)w)->id,
                ((Entity *)v)->id);
    });

    MEM_free(system.pool);
    MEM_free(system.lookup);
    MEM_free(system.handles);
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================ */

void darksys_run_tests(void)
{
    kprintf("================================");
    kprintf("DARKSYS TESTS");
    kprintf("================================");

    test_darksys_add_1();
    test_darksys_add_2();
    test_darksys_add_3();
    test_darksys_add_4();
    test_darksys_add_5();

    test_darksys_stable_handle();
    test_darksys_handle_reuse();

    test_darksys_remove_last();
    test_darksys_remove_swap();

    test_darksys_invalid();
    test_darksys_full();
    test_darksys_clear();

    test_darksys_bind();
    test_darksys_init();

    test_darksys_foreach_1();
    test_darksys_foreach_2();
    test_darksys_foreach_3();
    test_darksys_foreach_4();
    test_darksys_foreach_5();

    kprintf("================================");
    kprintf("DARKSYS TESTS DONE");
    kprintf("================================");
}