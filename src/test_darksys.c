#include <genesis.h>
#include "darksys-1.0.0_dev.h"

#define TEST_CAPACITY 8
#define BENCH_CAPACITY 128
#define BENCH_PARAMS 3
#define BENCH_ITERATIONS 1000

static uint16_t test_failures;
static volatile uint32_t bench_sink;

static void test_expect(uint16_t condition, const char *name)
{
    if (!condition)
    {
        kprintf("FAIL: ");
        kprintf(name);
        kprintf(" ");
        test_failures++;
    }
}

/* ============================================================================
 * BASIC
 * ========================================================================== */

static void test_basic(void)
{
    DARKSYS_POOL_DECLARE(storage, TEST_CAPACITY, 3);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;
    uint16_t b = 2;
    uint16_t c = 3;

    kprintf("=== TEST BASIC === ");

    test_expect(system.capacity == TEST_CAPACITY, "capacity");
    test_expect(system.params == 3, "params");
    test_expect(system.count == 0, "count");
    test_expect(system.next == 0, "next");
    test_expect(system.free_head == DARKSYS_INVALID_HANDLE, "free_head");

    darksys_handle_t handle = DARKSYS_ADD(&system, &a, &b, &c);

    test_expect(handle == 0, "first handle");
    test_expect(handle != DARKSYS_INVALID_HANDLE, "first valid handle");
    test_expect(system.count == 1, "count after add");
    test_expect(system.next == 1, "next after add");

    test_expect(
        darksys_valid(&system, handle),
        "valid after add"
    );

    test_expect(
        DARKSYS_DATA(&system, handle)[0] == &a,
        "data 0"
    );

    test_expect(
        DARKSYS_DATA(&system, handle)[1] == &b,
        "data 1"
    );

    test_expect(
        DARKSYS_DATA(&system, handle)[2] == &c,
        "data 2"
    );
}

/* ============================================================================
 * ADD PARAMS
 * ========================================================================== */

static void test_add_params(void)
{
    DARKSYS_POOL_DECLARE(s1_storage, 4, 1);
    DARKSYS_POOL_DECLARE(s2_storage, 4, 2);
    DARKSYS_POOL_DECLARE(s3_storage, 4, 3);
    DARKSYS_POOL_DECLARE(s4_storage, 4, 4);
    DARKSYS_POOL_DECLARE(s5_storage, 4, 5);

    darksys s1 = DARKSYS_POOL_BIND(s1_storage);
    darksys s2 = DARKSYS_POOL_BIND(s2_storage);
    darksys s3 = DARKSYS_POOL_BIND(s3_storage);
    darksys s4 = DARKSYS_POOL_BIND(s4_storage);
    darksys s5 = DARKSYS_POOL_BIND(s5_storage);

    uint16_t a = 1;
    uint16_t b = 2;
    uint16_t c = 3;
    uint16_t d = 4;
    uint16_t e = 5;

    kprintf("=== TEST ADD PARAMS === ");

    test_expect(
        DARKSYS_ADD(&s1, &a) == 0,
        "params 1"
    );

    test_expect(
        DARKSYS_ADD(&s2, &a, &b) == 0,
        "params 2"
    );

    test_expect(
        DARKSYS_ADD(&s3, &a, &b, &c) == 0,
        "params 3"
    );

    test_expect(
        DARKSYS_ADD(&s4, &a, &b, &c, &d) == 0,
        "params 4"
    );

    test_expect(
        DARKSYS_ADD(&s5, &a, &b, &c, &d, &e) == 0,
        "params 5"
    );

    {
        DARKSYS_POOL_DECLARE(invalid_storage, 4, 3);
        darksys system = DARKSYS_POOL_BIND(invalid_storage);

        test_expect(
            DARKSYS_ADD(&system, &a, &b) == DARKSYS_INVALID_HANDLE,
            "params too few handle"
        );

        test_expect(
            system.count == 0,
            "params too few count"
        );

        test_expect(
            system.next == 0,
            "params too few next"
        );

        test_expect(
            system.free_head == DARKSYS_INVALID_HANDLE,
            "params too few free head"
        );

        test_expect(
            DARKSYS_ADD(&system, &a, &b, &c, &d) == DARKSYS_INVALID_HANDLE,
            "params too many handle"
        );

        test_expect(
            system.count == 0,
            "params too many count"
        );

        test_expect(
            system.next == 0,
            "params too many next"
        );

        test_expect(
            system.free_head == DARKSYS_INVALID_HANDLE,
            "params too many free head"
        );
    }
}

/* ============================================================================
 * DIRECT ADD
 * ========================================================================== */

static void test_direct_add(void)
{
    DARKSYS_POOL_DECLARE(storage, 4, 2);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;
    uint16_t b = 2;

    kprintf("=== TEST DIRECT ADD === ");

    darksys_handle_t handle = darksys_add(&system);

    test_expect(handle == 0, "direct add handle");
    test_expect(handle != DARKSYS_INVALID_HANDLE, "direct add valid");
    test_expect(system.count == 1, "direct add count");
    test_expect(
        darksys_valid(&system, handle),
        "direct add valid lookup"
    );

    system.pool[0] = &a;
    system.pool[1] = &b;

    test_expect(
        DARKSYS_DATA(&system, handle)[0] == &a,
        "direct add data 0"
    );

    test_expect(
        DARKSYS_DATA(&system, handle)[1] == &b,
        "direct add data 1"
    );
}

/* ============================================================================
 * VALID
 * ========================================================================== */

static void test_valid(void)
{
    DARKSYS_POOL_DECLARE(storage, 4, 2);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;
    uint16_t b = 2;

    kprintf("=== TEST VALID === ");

    test_expect(
        !darksys_valid(&system, 0),
        "empty invalid"
    );

    darksys_handle_t handle = DARKSYS_ADD(&system, &a, &b);

    test_expect(
        darksys_valid(&system, handle),
        "active valid"
    );

    test_expect(
        !darksys_valid(&system, 1),
        "unassigned invalid"
    );

    test_expect(
        !darksys_valid(&system, DARKSYS_INVALID_HANDLE),
        "invalid handle"
    );

    darksys_remove(&system, handle);

    test_expect(
        !darksys_valid(&system, handle),
        "removed invalid"
    );
}

/* ============================================================================
 * DATA
 * ========================================================================== */

static void test_data(void)
{
    DARKSYS_POOL_DECLARE(storage, 4, 3);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 10;
    uint16_t b = 20;
    uint16_t c = 30;

    kprintf("=== TEST DATA === ");

    darksys_handle_t handle = DARKSYS_ADD(&system, &a, &b, &c);

    test_expect(
        DARKSYS_DATA(&system, handle)[0] == &a,
        "data a"
    );

    test_expect(
        DARKSYS_DATA(&system, handle)[1] == &b,
        "data b"
    );

    test_expect(
        DARKSYS_DATA(&system, handle)[2] == &c,
        "data c"
    );
}

/* ============================================================================
 * REMOVE / COMPACTION
 * ========================================================================== */

static void test_remove(void)
{
    DARKSYS_POOL_DECLARE(storage, 4, 3);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;
    uint16_t b = 2;
    uint16_t c = 3;
    uint16_t d = 4;
    uint16_t e = 5;
    uint16_t f = 6;

    kprintf("=== TEST REMOVE === ");

    darksys_handle_t h0 = DARKSYS_ADD(&system, &a, &b, &c);
    darksys_handle_t h1 = DARKSYS_ADD(&system, &b, &c, &d);
    darksys_handle_t h2 = DARKSYS_ADD(&system, &c, &d, &e);
    darksys_handle_t h3 = DARKSYS_ADD(&system, &d, &e, &f);

    test_expect(system.count == 4, "remove setup count");

    darksys_remove(&system, h1);

    test_expect(system.count == 3, "remove count");
    test_expect(!darksys_valid(&system, h1), "removed invalid");

    test_expect(darksys_valid(&system, h0), "h0 survives");
    test_expect(darksys_valid(&system, h2), "h2 survives");
    test_expect(darksys_valid(&system, h3), "h3 survives");

    test_expect(
        DARKSYS_DATA(&system, h3)[0] == &d,
        "moved data 0"
    );

    test_expect(
        DARKSYS_DATA(&system, h3)[1] == &e,
        "moved data 1"
    );

    test_expect(
        DARKSYS_DATA(&system, h3)[2] == &f,
        "moved data 2"
    );
}

/* ============================================================================
 * REMOVE CASES
 * ========================================================================== */

static void test_remove_cases(void)
{
    kprintf("=== TEST REMOVE CASES === ");

    {
        DARKSYS_POOL_DECLARE(storage, 1, 2);
        darksys system = DARKSYS_POOL_BIND(storage);

        uint16_t a = 1;
        uint16_t b = 2;

        darksys_handle_t handle = DARKSYS_ADD(&system, &a, &b);

        darksys_remove(&system, handle);

        test_expect(system.count == 0, "single count");
        test_expect(
            !darksys_valid(&system, handle),
            "single invalid"
        );
    }

    {
        DARKSYS_POOL_DECLARE(storage, 4, 2);
        darksys system = DARKSYS_POOL_BIND(storage);

        uint16_t a = 1;
        uint16_t b = 2;
        uint16_t c = 3;
        uint16_t d = 4;

        darksys_handle_t ha = DARKSYS_ADD(&system, &a, &b);
        darksys_handle_t hb = DARKSYS_ADD(&system, &b, &c);
        darksys_handle_t hc = DARKSYS_ADD(&system, &c, &d);
        darksys_handle_t hd = DARKSYS_ADD(&system, &d, &a);

        darksys_remove(&system, ha);

        test_expect(!darksys_valid(&system, ha), "first invalid");
        test_expect(darksys_valid(&system, hb), "first hb");
        test_expect(darksys_valid(&system, hc), "first hc");
        test_expect(darksys_valid(&system, hd), "first hd");

        test_expect(
            DARKSYS_DATA(&system, hd)[0] == &d,
            "first moved 0"
        );

        test_expect(
            DARKSYS_DATA(&system, hd)[1] == &a,
            "first moved 1"
        );
    }

    {
        DARKSYS_POOL_DECLARE(storage, 4, 2);
        darksys system = DARKSYS_POOL_BIND(storage);

        uint16_t a = 1;
        uint16_t b = 2;
        uint16_t c = 3;
        uint16_t d = 4;

        darksys_handle_t ha = DARKSYS_ADD(&system, &a, &b);
        darksys_handle_t hb = DARKSYS_ADD(&system, &b, &c);
        darksys_handle_t hc = DARKSYS_ADD(&system, &c, &d);
        darksys_handle_t hd = DARKSYS_ADD(&system, &d, &a);

        darksys_remove(&system, hb);

        test_expect(darksys_valid(&system, ha), "middle ha");
        test_expect(!darksys_valid(&system, hb), "middle hb");
        test_expect(darksys_valid(&system, hc), "middle hc");
        test_expect(darksys_valid(&system, hd), "middle hd");

        test_expect(
            DARKSYS_DATA(&system, hd)[0] == &d,
            "middle moved 0"
        );

        test_expect(
            DARKSYS_DATA(&system, hd)[1] == &a,
            "middle moved 1"
        );
    }

    {
        DARKSYS_POOL_DECLARE(storage, 4, 2);
        darksys system = DARKSYS_POOL_BIND(storage);

        uint16_t a = 1;
        uint16_t b = 2;
        uint16_t c = 3;
        uint16_t d = 4;

        darksys_handle_t ha = DARKSYS_ADD(&system, &a, &b);
        darksys_handle_t hb = DARKSYS_ADD(&system, &b, &c);
        darksys_handle_t hc = DARKSYS_ADD(&system, &c, &d);
        darksys_handle_t hd = DARKSYS_ADD(&system, &d, &a);

        darksys_remove(&system, hd);

        test_expect(darksys_valid(&system, ha), "last ha");
        test_expect(darksys_valid(&system, hb), "last hb");
        test_expect(darksys_valid(&system, hc), "last hc");
        test_expect(!darksys_valid(&system, hd), "last hd");
    }

    {
        DARKSYS_POOL_DECLARE(storage, 4, 2);
        darksys system = DARKSYS_POOL_BIND(storage);

        uint16_t a = 1;
        uint16_t b = 2;
        uint16_t c = 3;
        uint16_t d = 4;

        darksys_handle_t ha = DARKSYS_ADD(&system, &a, &b);
        darksys_handle_t hb = DARKSYS_ADD(&system, &b, &c);
        darksys_handle_t hc = DARKSYS_ADD(&system, &c, &d);
        darksys_handle_t hd = DARKSYS_ADD(&system, &d, &a);

        darksys_remove(&system, hb);
        darksys_remove(&system, hd);
        darksys_remove(&system, ha);
        darksys_remove(&system, hc);

        test_expect(system.count == 0, "all count");

        test_expect(
            DARKSYS_ADD(&system, &a, &b) != DARKSYS_INVALID_HANDLE,
            "all reuse 1"
        );

        test_expect(
            DARKSYS_ADD(&system, &b, &c) != DARKSYS_INVALID_HANDLE,
            "all reuse 2"
        );

        test_expect(
            DARKSYS_ADD(&system, &c, &d) != DARKSYS_INVALID_HANDLE,
            "all reuse 3"
        );

        test_expect(
            DARKSYS_ADD(&system, &d, &a) != DARKSYS_INVALID_HANDLE,
            "all reuse 4"
        );

        test_expect(
            DARKSYS_ADD(&system, &a, &b) == DARKSYS_INVALID_HANDLE,
            "all full"
        );
    }
}

/* ============================================================================
 * FREE LIST
 * ========================================================================== */

static void test_free_list(void)
{
    DARKSYS_POOL_DECLARE(storage, 8, 2);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;
    uint16_t b = 2;
    uint16_t c = 3;
    uint16_t d = 4;

    kprintf("=== TEST FREE LIST === ");

    darksys_handle_t ha = DARKSYS_ADD(&system, &a, &b);
    darksys_handle_t hb = DARKSYS_ADD(&system, &b, &c);
    darksys_handle_t hc = DARKSYS_ADD(&system, &c, &d);
    darksys_handle_t hd = DARKSYS_ADD(&system, &d, &a);

    darksys_remove(&system, hb);
    test_expect(system.free_head == hb, "free head 1");

    darksys_remove(&system, hd);
    test_expect(system.free_head == hd, "free head 2");

    darksys_remove(&system, ha);
    test_expect(system.free_head == ha, "free head 3");

    {
        darksys_handle_t h = DARKSYS_ADD(&system, &a, &b);
        test_expect(h == ha, "free reuse 1");
    }

    {
        darksys_handle_t h = DARKSYS_ADD(&system, &b, &c);
        test_expect(h == hd, "free reuse 2");
    }

    {
        darksys_handle_t h = DARKSYS_ADD(&system, &c, &d);
        test_expect(h == hb, "free reuse 3");
    }

    test_expect(darksys_valid(&system, hc), "remaining handle");
}

/* ============================================================================
 * CLEAR
 * ========================================================================== */

static void test_clear(void)
{
    DARKSYS_POOL_DECLARE(storage, 8, 3);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;
    uint16_t b = 2;
    uint16_t c = 3;

    kprintf("=== TEST CLEAR === ");

    DARKSYS_ADD(&system, &a, &b, &c);
    DARKSYS_ADD(&system, &b, &c, &a);
    DARKSYS_ADD(&system, &c, &a, &b);

    test_expect(system.count == 3, "clear setup count");
    test_expect(system.next == 3, "clear setup next");

    darksys_clear(&system);

    test_expect(system.count == 0, "clear count");
    test_expect(system.next == 0, "clear next");
    test_expect(system.free_head == DARKSYS_INVALID_HANDLE, "clear free head");

    test_expect(
        DARKSYS_ADD(&system, &a, &b, &c) == 0,
        "clear first handle"
    );
}

/* ============================================================================
 * FULL
 * ========================================================================== */

static void test_full(void)
{
    DARKSYS_POOL_DECLARE(storage, 4, 1);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;

    kprintf("=== TEST FULL === ");

    test_expect(DARKSYS_ADD(&system, &a) == 0, "full h0");
    test_expect(DARKSYS_ADD(&system, &a) == 1, "full h1");
    test_expect(DARKSYS_ADD(&system, &a) == 2, "full h2");
    test_expect(DARKSYS_ADD(&system, &a) == 3, "full h3");

    test_expect(
        DARKSYS_ADD(&system, &a) == DARKSYS_INVALID_HANDLE,
        "full invalid"
    );

    test_expect(system.count == 4, "full count");
    test_expect(system.next == 4, "full next");

    darksys_remove(&system, 1);

    test_expect(
        DARKSYS_ADD(&system, &a) == 1,
        "full reuse"
    );
}

/* ============================================================================
 * INVALID
 * ========================================================================== */

static void test_invalid(void)
{
    DARKSYS_POOL_DECLARE(storage, 4, 1);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;

    kprintf("=== TEST INVALID === ");

    test_expect(
        !darksys_valid(&system, 0),
        "invalid empty"
    );

    test_expect(
        !darksys_valid(&system, DARKSYS_INVALID_HANDLE),
        "invalid ffff"
    );

    DARKSYS_ADD(&system, &a);

    test_expect(
        !darksys_valid(&system, 1),
        "invalid unassigned"
    );

    darksys_remove(&system, 0);

    test_expect(
        !darksys_valid(&system, 0),
        "invalid removed"
    );
}

/* ============================================================================
 * FOREACH 1
 * ========================================================================== */

static void test_foreach_1(void)
{
    DARKSYS_POOL_DECLARE(storage, 4, 1);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;
    uint16_t b = 2;
    uint16_t c = 3;

    void *value = 0;
    uint16_t count = 0;

    kprintf("=== TEST FOREACH 1 === ");

    DARKSYS_ADD(&system, &a);
    DARKSYS_ADD(&system, &b);
    DARKSYS_ADD(&system, &c);

    DARKSYS_FOREACH(&system, value,
    {
        test_expect(
            value == &a || value == &b || value == &c,
            "foreach1 value"
        );

        count++;
    });

    test_expect(count == 3, "foreach1 count");
}

/* ============================================================================
 * FOREACH 2
 * ========================================================================== */

static void test_foreach_2(void)
{
    DARKSYS_POOL_DECLARE(storage, 4, 2);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;
    uint16_t b = 2;
    uint16_t c = 3;
    uint16_t d = 4;

    void *x = 0;
    void *y = 0;
    uint16_t count = 0;

    kprintf("=== TEST FOREACH 2 === ");

    DARKSYS_ADD(&system, &a, &b);
    DARKSYS_ADD(&system, &c, &d);

    DARKSYS_FOREACH(&system, x, y,
    {
        test_expect(
            (x == &a && y == &b) ||
            (x == &c && y == &d),
            "foreach2 values"
        );

        count++;
    });

    test_expect(count == 2, "foreach2 count");
}

/* ============================================================================
 * FOREACH 3
 * ========================================================================== */

static void test_foreach_3(void)
{
    DARKSYS_POOL_DECLARE(storage, 2, 3);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;
    uint16_t b = 2;
    uint16_t c = 3;

    void *x = 0;
    void *y = 0;
    void *z = 0;
    uint16_t count = 0;

    kprintf("=== TEST FOREACH 3 === ");

    DARKSYS_ADD(&system, &a, &b, &c);

    DARKSYS_FOREACH(&system, x, y, z,
    {
        test_expect(x == &a, "foreach3 x");
        test_expect(y == &b, "foreach3 y");
        test_expect(z == &c, "foreach3 z");
        count++;
    });

    test_expect(count == 1, "foreach3 count");
}

/* ============================================================================
 * FOREACH 4
 * ========================================================================== */

static void test_foreach_4(void)
{
    DARKSYS_POOL_DECLARE(storage, 2, 4);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;
    uint16_t b = 2;
    uint16_t c = 3;
    uint16_t d = 4;

    void *w = 0;
    void *x = 0;
    void *y = 0;
    void *z = 0;
    uint16_t count = 0;

    kprintf("=== TEST FOREACH 4 === ");

    DARKSYS_ADD(&system, &a, &b, &c, &d);

    DARKSYS_FOREACH(&system, w, x, y, z,
    {
        test_expect(w == &a, "foreach4 w");
        test_expect(x == &b, "foreach4 x");
        test_expect(y == &c, "foreach4 y");
        test_expect(z == &d, "foreach4 z");
        count++;
    });

    test_expect(count == 1, "foreach4 count");
}

/* ============================================================================
 * FOREACH 5
 * ========================================================================== */

static void test_foreach_5(void)
{
    DARKSYS_POOL_DECLARE(storage, 2, 5);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;
    uint16_t b = 2;
    uint16_t c = 3;
    uint16_t d = 4;
    uint16_t e = 5;

    void *v0 = 0;
    void *v1 = 0;
    void *v2 = 0;
    void *v3 = 0;
    void *v4 = 0;
    uint16_t count = 0;

    kprintf("=== TEST FOREACH 5 === ");

    DARKSYS_ADD(&system, &a, &b, &c, &d, &e);

    DARKSYS_FOREACH(&system, v0, v1, v2, v3, v4,
    {
        test_expect(v0 == &a, "foreach5 v0");
        test_expect(v1 == &b, "foreach5 v1");
        test_expect(v2 == &c, "foreach5 v2");
        test_expect(v3 == &d, "foreach5 v3");
        test_expect(v4 == &e, "foreach5 v4");
        count++;
    });

    test_expect(count == 1, "foreach5 count");
}

/* ============================================================================
 * FOREACH REMOVE
 * ========================================================================== */

static void test_foreach_remove(void)
{
    DARKSYS_POOL_DECLARE(storage, 8, 3);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;
    uint16_t b = 2;
    uint16_t c = 3;

    darksys_handle_t handles[5];

    kprintf("=== TEST FOREACH REMOVE === ");

    for (uint16_t i = 0; i < 5; i++)
        handles[i] = DARKSYS_ADD(&system, &a, &b, &c);

    test_expect(system.count == 5, "foreach remove setup");

    for (uint16_t i = 0; i < 5; i++)
    {
        if (darksys_valid(&system, handles[i]))
            darksys_remove(&system, handles[i]);
    }

    test_expect(system.count == 0, "foreach remove count");
}

/* ============================================================================
 * BIND
 * ========================================================================== */

static void test_bind(void)
{
    DARKSYS_POOL_DECLARE(storage, 4, 2);
    darksys system = DARKSYS_POOL_BIND(storage);

    uint16_t a = 1;
    uint16_t b = 2;

    kprintf("=== TEST BIND === ");

    test_expect(system.pool == storage.pool, "bind pool");
    test_expect(system.lookup == storage.lookup, "bind lookup");
    test_expect(system.handles == storage.handles, "bind handles");
    test_expect(system.capacity == 4, "bind capacity");
    test_expect(system.params == 2, "bind params");

    test_expect(
        DARKSYS_ADD(&system, &a, &b) == 0,
        "bind add"
    );
}

/* ============================================================================
 * ALLOC
 * ========================================================================== */

static void test_alloc(void)
{
    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 8, 3);

    uint16_t a = 1;
    uint16_t b = 2;
    uint16_t c = 3;

    kprintf("=== TEST ALLOC === ");

    test_expect(system.pool != 0, "alloc pool");
    test_expect(system.lookup != 0, "alloc lookup");
    test_expect(system.handles != 0, "alloc handles");

    test_expect(
        DARKSYS_ADD(&system, &a, &b, &c) == 0,
        "alloc add"
    );

    test_expect(
        darksys_valid(&system, 0),
        "alloc valid"
    );

    test_expect(
        DARKSYS_DATA(&system, 0)[0] == &a,
        "alloc data 0"
    );

    test_expect(
        DARKSYS_DATA(&system, 0)[1] == &b,
        "alloc data 1"
    );

    test_expect(
        DARKSYS_DATA(&system, 0)[2] == &c,
        "alloc data 2"
    );

    DARKSYS_POOL_FREE(MEM_free, &system);
}

/* ============================================================================
 * BENCH
 * ========================================================================== */

static void bench(void)
{
    DARKSYS_POOL_DECLARE(storage, BENCH_CAPACITY, BENCH_PARAMS);
    darksys system = DARKSYS_POOL_BIND(storage);

    static uint16_t values[BENCH_CAPACITY * BENCH_PARAMS];
    static darksys_handle_t handles[BENCH_CAPACITY];

    uint32_t timer;
    uint32_t total;
    uint32_t sink = 0;

    void *a;
    void *b;
    void *c;

    kprintf("=== BENCH === ");
    kprintf(
        "capacity=%u params=%u iterations=%u ",
        BENCH_CAPACITY,
        BENCH_PARAMS,
        BENCH_ITERATIONS
    );

    for (uint16_t i = 0; i < BENCH_CAPACITY * BENCH_PARAMS; i++)
        values[i] = i + 1;

    /* ADD */

    total = 0;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
    {
        darksys_clear(&system);

        timer = getSubTick();

        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
        {
            handles[i] = DARKSYS_ADD(
                &system,
                &values[i * BENCH_PARAMS + 0],
                &values[i * BENCH_PARAMS + 1],
                &values[i * BENCH_PARAMS + 2]
            );

            sink += handles[i];
        }

        total += getSubTick() - timer;
    }

    kprintf("add=%lu ", total);

    /* VALID */

    darksys_clear(&system);

    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        handles[i] = DARKSYS_ADD(
            &system,
            &values[i * BENCH_PARAMS + 0],
            &values[i * BENCH_PARAMS + 1],
            &values[i * BENCH_PARAMS + 2]
        );
    }

    total = 0;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
    {
        timer = getSubTick();

        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
            sink += darksys_valid(&system, handles[i]);

        total += getSubTick() - timer;
    }

    kprintf("valid=%lu ", total);

    /* DATA */

    total = 0;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
    {
        timer = getSubTick();

        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
        {
            a = DARKSYS_DATA(&system, handles[i])[0];
            b = DARKSYS_DATA(&system, handles[i])[1];
            c = DARKSYS_DATA(&system, handles[i])[2];

            sink += *((uint16_t *)a);
            sink += *((uint16_t *)b);
            sink += *((uint16_t *)c);
        }

        total += getSubTick() - timer;
    }

    kprintf("data=%lu ", total);

    /* REMOVE */

    total = 0;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
    {
        darksys_clear(&system);

        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
        {
            handles[i] = DARKSYS_ADD(
                &system,
                &values[i * BENCH_PARAMS + 0],
                &values[i * BENCH_PARAMS + 1],
                &values[i * BENCH_PARAMS + 2]
            );
        }

        timer = getSubTick();

        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
            darksys_remove(&system, handles[i]);

        total += getSubTick() - timer;
    }

    kprintf("remove=%lu ", total);

    /* REMOVE + ADD */

    darksys_clear(&system);

    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        handles[i] = DARKSYS_ADD(
            &system,
            &values[i * BENCH_PARAMS + 0],
            &values[i * BENCH_PARAMS + 1],
            &values[i * BENCH_PARAMS + 2]
        );
    }

    total = 0;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
    {
        timer = getSubTick();

        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
        {
            darksys_remove(&system, handles[i]);

            handles[i] = DARKSYS_ADD(
                &system,
                &values[i * BENCH_PARAMS + 0],
                &values[i * BENCH_PARAMS + 1],
                &values[i * BENCH_PARAMS + 2]
            );

            sink += handles[i];
        }

        total += getSubTick() - timer;
    }

    kprintf("remove_add=%lu ", total);

    /* FOREACH */

    total = 0;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
    {
        timer = getSubTick();

        DARKSYS_FOREACH(&system, a, b, c,
        {
            sink += *((uint16_t *)a);
            sink += *((uint16_t *)b);
            sink += *((uint16_t *)c);
        });

        total += getSubTick() - timer;
    }

    kprintf("foreach=%lu ", total);

    bench_sink = sink;

    kprintf("sink=%lu", bench_sink);
}

/* ============================================================================
 * ENTRY
 * ========================================================================== */

void test_darksys_main(void)
{
    kprintf("============================== ");
    kprintf("DARKSYS TEST ");
    kprintf("============================== ");

    test_failures = 0;

    test_basic();
    test_add_params();
    test_direct_add();
    test_valid();
    test_data();
    test_remove();
    test_remove_cases();
    test_free_list();
    test_clear();
    test_full();
    test_invalid();
    test_foreach_1();
    test_foreach_2();
    test_foreach_3();
    test_foreach_4();
    test_foreach_5();
    test_foreach_remove();
    test_bind();
    test_alloc();

    kprintf("FAILURES=%u ", test_failures);

    bench();

    kprintf("============================== ");
    kprintf("DONE ");
    kprintf("============================== ");
}

