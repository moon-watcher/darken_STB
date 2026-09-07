#include <genesis.h>


#include "../darksys.h"

static u16 test_add(void)
{
    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 2);

    void *a = (void *) 1;
    void *b = (void *) 2;

    u16 ok = 1;

    ok &= DARKSYS_ADD(&system, a);
    ok &= DARKSYS_ADD(&system, b);

    ok &= system.size == 2;
    ok &= system.limit == 8;

    MEM_free(system.pool);

    return ok;
}

static u16 test_add_limit(void)
{
    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 1, 2);

    void *a = (void *) 1;
    void *b = (void *) 2;
    void *c = (void *) 3;

    u16 ok = 1;

    ok &= DARKSYS_ADD(&system, a);
    ok &= DARKSYS_ADD(&system, b);

    ok &= !DARKSYS_ADD(&system, c);

    ok &= system.size == 2;

    MEM_free(system.pool);

    return ok;
}

static u16 test_foreach(void)
{
    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 3, 2);

    void *a = (void *) 1;
    void *b = (void *) 2;
    void *c = (void *) 3;
    void *d = (void *) 4;

    DARKSYS_ADD(&system, a);
    DARKSYS_ADD(&system, b);
    DARKSYS_ADD(&system, c);
    DARKSYS_ADD(&system, d);

    void *x;
    void *y;

    u16 count = 0;
    u16 ok = 1;

    DARKSYS_FOREACH(&system, x, y,
    {
        if (count == 0)
            ok &= x == a && y == b;

        if (count == 1)
            ok &= x == c && y == d;

        count++;
    });

    ok &= count == 2;

    MEM_free(system.pool);

    return ok;
}

static u16 test_remove(void)
{
    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 2);

    void *a = (void *) 1;
    void *b = (void *) 2;
    void *c = (void *) 3;
    void *d = (void *) 4;

    DARKSYS_ADD(&system, a);
    DARKSYS_ADD(&system, b);
    DARKSYS_ADD(&system, c);
    DARKSYS_ADD(&system, d);

    u16 ok = 1;

    ok &= darksys_remove(&system, a);
    ok &= system.size == 2;

    void *x;
    void *y;

    DARKSYS_FOREACH(&system, x, y,
    {
        ok &= x == c;
        ok &= y == d;
    });

    MEM_free(system.pool);

    return ok;
}

static u16 test_clear(void)
{
    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 4, 2);

    DARKSYS_ADD(&system, (void *) 1);
    DARKSYS_ADD(&system, (void *) 2);

    darksys_clear(&system);

    u16 ok = system.size == 0;

    MEM_free(system.pool);

    return ok;
}

u16 darksys_run_tests(void)
{
    u16 passed = 0;

    passed += test_add();
    passed += test_add_limit();
    passed += test_foreach();
    passed += test_remove();
    passed += test_clear();

    return passed;
}