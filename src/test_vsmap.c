#include <genesis.h>
#include "vsmap.h"

#define TEST_CAPACITY 8
#define BENCH_CAPACITY 128
#define BENCH_ITERATIONS 1000

typedef struct
{
    uint16_t id;
    uint16_t value;
} TestObject;

static TestObject test_objects[TEST_CAPACITY];
static volatile uint32_t bench_sink;
static uint16_t test_failures;

static void test_expect(uint16_t condition, const char *name)
{
    if (!condition)
    {
        kprintf("FAIL: %s", name);
        test_failures++;
    }
}

static void test_setup_objects(void)
{
    for (uint16_t i = 0; i < TEST_CAPACITY; i++)
    {
        test_objects[i].id = i;
        test_objects[i].value = 100 + i;
    }
}

/* ============================================================================
 * BASIC
 * ========================================================================== */

static void test_basic(void)
{
    VSMAP_DECLARE(storage, TEST_CAPACITY);
    vsmap_t map = VSMAP_BIND(storage);
    vsmap_handle_t handles[TEST_CAPACITY];

    kprintf("=== TEST BASIC ===");

    test_expect(map.capacity == TEST_CAPACITY, "capacity");
    test_expect(map.count == 0, "count=0");
    test_expect(
        map.free_head == VSMAP_INVALID_HANDLE,
        "free_head before init"
    );

    vsmap_init(&map);

    test_expect(map.count == 0, "init count");
    test_expect(map.free_head == 0, "init free_head");

    for (uint16_t i = 0; i < TEST_CAPACITY; i++)
    {
        handles[i] = vsmap_add(&map, &test_objects[i]);

        test_expect(handles[i] != VSMAP_INVALID_HANDLE, "add handle");
        test_expect(vsmap_valid(&map, handles[i]), "valid after add");

        test_expect(
            VSMAP_DATA(&map, handles[i]).value == &test_objects[i],
            "data"
        );

        test_expect(
            VSMAP_DATA(&map, handles[i]).index == handles[i],
            "data index"
        );
    }

    test_expect(map.count == TEST_CAPACITY, "count full");

    test_expect(
        vsmap_add(&map, &test_objects[0]) == VSMAP_INVALID_HANDLE,
        "add full"
    );

    test_expect(
        !vsmap_valid(&map, VSMAP_INVALID_HANDLE),
        "invalid handle"
    );

    test_expect(
        !vsmap_valid(&map, TEST_CAPACITY),
        "out of range handle"
    );

    test_expect(
        !vsmap_valid(&map, 0xFFFEu),
        "0xfffe invalid"
    );

    {
        uint16_t foreach_count = 0;
        uint32_t foreach_sum = 0;

        VSMAP_FOREACH(&map,
        {
            TestObject *obj = (TestObject *)item->value;

            foreach_count++;
            foreach_sum += obj->value;
        });

        test_expect(
            foreach_count == TEST_CAPACITY,
            "foreach count"
        );

        test_expect(
            foreach_sum == 828,
            "foreach sum"
        );
    }

    kprintf("count=%u foreach=%u", map.count, TEST_CAPACITY);
}

/* ============================================================================
 * MAPPING
 * ========================================================================== */

static void test_mapping(void)
{
    VSMAP_DECLARE(storage, TEST_CAPACITY);
    vsmap_t map = VSMAP_BIND(storage);
    vsmap_handle_t handles[TEST_CAPACITY];

    kprintf("=== TEST MAPPING ===");

    vsmap_init(&map);

    for (uint16_t i = 0; i < TEST_CAPACITY; i++)
        handles[i] = vsmap_add(&map, &test_objects[i]);

    for (uint16_t i = 0; i < TEST_CAPACITY; i++)
    {
        vsmap_item_t *item = &VSMAP_DATA(&map, handles[i]);

        test_expect(
            item->value == &test_objects[i],
            "mapping value"
        );

        test_expect(
            item->index == handles[i],
            "mapping index"
        );

        test_expect(
            vsmap_valid(&map, item->index),
            "mapping valid"
        );
    }
}

/* ============================================================================
 * REMOVE / COMPACTION
 * ========================================================================== */

static void test_remove(void)
{
    VSMAP_DECLARE(storage, TEST_CAPACITY);
    vsmap_t map = VSMAP_BIND(storage);

    kprintf("=== TEST REMOVE ===");

    vsmap_init(&map);

    vsmap_handle_t h0 = vsmap_add(&map, &test_objects[0]);
    vsmap_handle_t h1 = vsmap_add(&map, &test_objects[1]);
    vsmap_handle_t h2 = vsmap_add(&map, &test_objects[2]);
    vsmap_handle_t h3 = vsmap_add(&map, &test_objects[3]);

    vsmap_remove(&map, h1);

    test_expect(!vsmap_valid(&map, h1), "removed invalid");
    test_expect(map.count == 3, "count after remove");

    test_expect(vsmap_valid(&map, h0), "h0 survives");
    test_expect(vsmap_valid(&map, h2), "h2 survives");
    test_expect(vsmap_valid(&map, h3), "h3 survives");

    test_expect(
        VSMAP_DATA(&map, h0).value == &test_objects[0],
        "h0 data"
    );

    test_expect(
        VSMAP_DATA(&map, h2).value == &test_objects[2],
        "h2 data"
    );

    test_expect(
        VSMAP_DATA(&map, h3).value == &test_objects[3],
        "h3 data"
    );

    test_expect(
        VSMAP_DATA(&map, h3).index == h3,
        "moved item index"
    );
}

/* ============================================================================
 * REMOVE CASES
 * ========================================================================== */

static void test_remove_cases(void)
{
    kprintf("=== TEST REMOVE CASES ===");

    /* Single */

    {
        VSMAP_DECLARE(storage, 1);
        vsmap_t map = VSMAP_BIND(storage);
        TestObject a = {1, 11};

        vsmap_init(&map);

        vsmap_handle_t h = vsmap_add(&map, &a);

        test_expect(h == 0, "single handle");
        test_expect(map.count == 1, "single count");
        test_expect(vsmap_valid(&map, h), "single valid");

        vsmap_remove(&map, h);

        test_expect(map.count == 0, "single empty");
        test_expect(!vsmap_valid(&map, h), "single invalid");
        test_expect(map.free_head == h, "single free head");
    }

    /* First */

    {
        VSMAP_DECLARE(storage, 4);
        vsmap_t map = VSMAP_BIND(storage);

        TestObject a = {1, 11};
        TestObject b = {2, 22};
        TestObject c = {3, 33};
        TestObject d = {4, 44};

        vsmap_init(&map);

        vsmap_handle_t ha = vsmap_add(&map, &a);
        vsmap_handle_t hb = vsmap_add(&map, &b);
        vsmap_handle_t hc = vsmap_add(&map, &c);
        vsmap_handle_t hd = vsmap_add(&map, &d);

        vsmap_remove(&map, ha);

        test_expect(map.count == 3, "first count");
        test_expect(!vsmap_valid(&map, ha), "first invalid");

        test_expect(vsmap_valid(&map, hb), "first hb valid");
        test_expect(vsmap_valid(&map, hc), "first hc valid");
        test_expect(vsmap_valid(&map, hd), "first hd valid");

        test_expect(
            VSMAP_DATA(&map, hd).value == &d,
            "first moved last"
        );

        test_expect(
            VSMAP_DATA(&map, hd).index == hd,
            "first moved index"
        );
    }

    /* Middle */

    {
        VSMAP_DECLARE(storage, 4);
        vsmap_t map = VSMAP_BIND(storage);

        TestObject a = {1, 11};
        TestObject b = {2, 22};
        TestObject c = {3, 33};
        TestObject d = {4, 44};

        vsmap_init(&map);

        vsmap_handle_t ha = vsmap_add(&map, &a);
        vsmap_handle_t hb = vsmap_add(&map, &b);
        vsmap_handle_t hc = vsmap_add(&map, &c);
        vsmap_handle_t hd = vsmap_add(&map, &d);

        vsmap_remove(&map, hb);

        test_expect(map.count == 3, "middle count");
        test_expect(!vsmap_valid(&map, hb), "middle invalid");

        test_expect(vsmap_valid(&map, ha), "middle ha valid");
        test_expect(vsmap_valid(&map, hc), "middle hc valid");
        test_expect(vsmap_valid(&map, hd), "middle hd valid");

        test_expect(
            VSMAP_DATA(&map, hd).value == &d,
            "middle moved last"
        );

        test_expect(
            VSMAP_DATA(&map, hd).index == hd,
            "middle moved index"
        );
    }

    /* Last */

    {
        VSMAP_DECLARE(storage, 4);
        vsmap_t map = VSMAP_BIND(storage);

        TestObject a = {1, 11};
        TestObject b = {2, 22};
        TestObject c = {3, 33};
        TestObject d = {4, 44};

        vsmap_init(&map);

        vsmap_handle_t ha = vsmap_add(&map, &a);
        vsmap_handle_t hb = vsmap_add(&map, &b);
        vsmap_handle_t hc = vsmap_add(&map, &c);
        vsmap_handle_t hd = vsmap_add(&map, &d);

        vsmap_remove(&map, hd);

        test_expect(map.count == 3, "last count");
        test_expect(!vsmap_valid(&map, hd), "last invalid");

        test_expect(vsmap_valid(&map, ha), "last ha valid");
        test_expect(vsmap_valid(&map, hb), "last hb valid");
        test_expect(vsmap_valid(&map, hc), "last hc valid");
    }

    /* All */

    {
        VSMAP_DECLARE(storage, 4);
        vsmap_t map = VSMAP_BIND(storage);

        TestObject a = {1, 11};
        TestObject b = {2, 22};
        TestObject c = {3, 33};
        TestObject d = {4, 44};

        vsmap_init(&map);

        vsmap_handle_t ha = vsmap_add(&map, &a);
        vsmap_handle_t hb = vsmap_add(&map, &b);
        vsmap_handle_t hc = vsmap_add(&map, &c);
        vsmap_handle_t hd = vsmap_add(&map, &d);

        vsmap_remove(&map, hb);
        vsmap_remove(&map, hd);
        vsmap_remove(&map, ha);
        vsmap_remove(&map, hc);

        test_expect(map.count == 0, "all count");

        test_expect(!vsmap_valid(&map, ha), "all ha invalid");
        test_expect(!vsmap_valid(&map, hb), "all hb invalid");
        test_expect(!vsmap_valid(&map, hc), "all hc invalid");
        test_expect(!vsmap_valid(&map, hd), "all hd invalid");

        test_expect(
            vsmap_add(&map, &a) != VSMAP_INVALID_HANDLE,
            "all reuse a"
        );

        test_expect(
            vsmap_add(&map, &b) != VSMAP_INVALID_HANDLE,
            "all reuse b"
        );

        test_expect(
            vsmap_add(&map, &c) != VSMAP_INVALID_HANDLE,
            "all reuse c"
        );

        test_expect(
            vsmap_add(&map, &d) != VSMAP_INVALID_HANDLE,
            "all reuse d"
        );

        test_expect(
            vsmap_add(&map, &a) == VSMAP_INVALID_HANDLE,
            "all full"
        );
    }
}

/* ============================================================================
 * FREE LIST
 * ========================================================================== */

static void test_free_list(void)
{
    VSMAP_DECLARE(storage, 8);
    vsmap_t map = VSMAP_BIND(storage);

    TestObject a = {1, 1};
    TestObject b = {2, 2};
    TestObject c = {3, 3};
    TestObject d = {4, 4};

    kprintf("=== TEST FREE LIST ===");

    vsmap_init(&map);

    vsmap_handle_t ha = vsmap_add(&map, &a);
    vsmap_handle_t hb = vsmap_add(&map, &b);
    vsmap_handle_t hc = vsmap_add(&map, &c);
    vsmap_handle_t hd = vsmap_add(&map, &d);

    vsmap_remove(&map, hb);
    test_expect(map.free_head == hb, "free head first");

    vsmap_remove(&map, hd);
    test_expect(map.free_head == hd, "free head second");

    vsmap_remove(&map, ha);
    test_expect(map.free_head == ha, "free head third");

    {
        vsmap_handle_t h = vsmap_add(&map, &a);
        test_expect(h == ha, "free reuse first");
    }

    {
        vsmap_handle_t h = vsmap_add(&map, &b);
        test_expect(h == hd, "free reuse second");
    }

    {
        vsmap_handle_t h = vsmap_add(&map, &c);
        test_expect(h == hb, "free reuse third");
    }

    test_expect(vsmap_valid(&map, hc), "remaining live handle");
}

/* ============================================================================
 * REUSE
 * ========================================================================== */

static void test_reuse(void)
{
    VSMAP_DECLARE(storage, 4);
    vsmap_t map = VSMAP_BIND(storage);

    TestObject a = {10, 100};
    TestObject b = {20, 200};
    TestObject c = {30, 300};
    TestObject d = {40, 400};
    TestObject e = {50, 500};

    kprintf("=== TEST REUSE ===");

    vsmap_init(&map);

    vsmap_handle_t ha = vsmap_add(&map, &a);
    vsmap_handle_t hb = vsmap_add(&map, &b);
    vsmap_handle_t hc = vsmap_add(&map, &c);
    vsmap_handle_t hd = vsmap_add(&map, &d);

    test_expect(ha == 0, "handle 0");
    test_expect(hb == 1, "handle 1");
    test_expect(hc == 2, "handle 2");
    test_expect(hd == 3, "handle 3");

    vsmap_remove(&map, hb);

    test_expect(!vsmap_valid(&map, hb), "removed handle invalid");

    vsmap_handle_t he = vsmap_add(&map, &e);

    test_expect(he == hb, "handle reuse");
    test_expect(vsmap_valid(&map, he), "reused handle valid");

    test_expect(
        VSMAP_DATA(&map, he).value == &e,
        "reused handle data"
    );
}

/* ============================================================================
 * RESET
 * ========================================================================== */

static void test_reset(void)
{
    VSMAP_DECLARE(storage, TEST_CAPACITY);
    vsmap_t map = VSMAP_BIND(storage);

    TestObject a = {1, 10};
    TestObject b = {2, 20};
    TestObject c = {3, 30};

    kprintf("=== TEST RESET ===");

    vsmap_init(&map);

    vsmap_handle_t ha = vsmap_add(&map, &a);
    vsmap_handle_t hb = vsmap_add(&map, &b);
    vsmap_handle_t hc = vsmap_add(&map, &c);

    test_expect(vsmap_valid(&map, ha), "reset setup a");
    test_expect(vsmap_valid(&map, hb), "reset setup b");
    test_expect(vsmap_valid(&map, hc), "reset setup c");

    vsmap_init(&map);

    test_expect(map.count == 0, "reset count");
    test_expect(map.free_head == 0, "reset free head");

    test_expect(!vsmap_valid(&map, ha), "reset a invalid");
    test_expect(!vsmap_valid(&map, hb), "reset b invalid");
    test_expect(!vsmap_valid(&map, hc), "reset c invalid");

    test_expect(
        vsmap_add(&map, &a) == 0,
        "reset first handle"
    );
}

/* ============================================================================
 * FOREACH
 * ========================================================================== */

static void test_foreach(void)
{
    VSMAP_DECLARE(storage, 8);
    vsmap_t map = VSMAP_BIND(storage);

    TestObject objects[5];
    uint16_t count = 0;

    kprintf("=== TEST FOREACH ===");

    for (uint16_t i = 0; i < 5; i++)
    {
        objects[i].id = i;
        objects[i].value = 1000 + i;
    }

    vsmap_init(&map);

    for (uint16_t i = 0; i < 5; i++)
        vsmap_add(&map, &objects[i]);

    VSMAP_FOREACH(&map,
    {
        TestObject *obj = (TestObject *)item->value;

        test_expect(
            item->index < map.capacity,
            "foreach index range"
        );

        test_expect(
            obj->value >= 1000 && obj->value < 1005,
            "foreach value"
        );

        count++;
    });

    test_expect(count == 5, "foreach five");
}

/* ============================================================================
 * FOREACH REMOVE CURRENT
 * ========================================================================== */

static void test_foreach_remove(void)
{
    VSMAP_DECLARE(storage, 8);
    vsmap_t map = VSMAP_BIND(storage);

    TestObject objects[5];

    kprintf("=== TEST FOREACH REMOVE ===");

    for (uint16_t i = 0; i < 5; i++)
    {
        objects[i].id = i;
        objects[i].value = 2000 + i;
    }

    vsmap_init(&map);

    for (uint16_t i = 0; i < 5; i++)
        vsmap_add(&map, &objects[i]);

    VSMAP_FOREACH(&map,
    {
        vsmap_handle_t handle = item->index;
        vsmap_remove(&map, handle);
    });

    test_expect(map.count == 0, "foreach remove all");
}

/* ============================================================================
 * INVALID HANDLES
 * ========================================================================== */

static void test_invalid(void)
{
    VSMAP_DECLARE(storage, 4);
    vsmap_t map = VSMAP_BIND(storage);

    TestObject a = {1, 1};

    kprintf("=== TEST INVALID ===");

    vsmap_init(&map);

    test_expect(
        !vsmap_valid(&map, VSMAP_INVALID_HANDLE),
        "invalid ffff"
    );

    test_expect(
        !vsmap_valid(&map, 4),
        "invalid capacity"
    );

    test_expect(
        !vsmap_valid(&map, 100),
        "invalid 100"
    );

    test_expect(
        !vsmap_valid(&map, 0xFFFEu),
        "invalid fffe"
    );

    {
        vsmap_handle_t h = vsmap_add(&map, &a);

        test_expect(
            vsmap_valid(&map, h),
            "valid after invalid tests"
        );
    }
}

/* ============================================================================
 * SMALL CAPACITIES
 * ========================================================================== */

static void test_small_capacities(void)
{
    kprintf("=== TEST SMALL CAPACITIES ===");

    /* Capacity 1 */

    {
        VSMAP_DECLARE(storage, 1);
        vsmap_t map = VSMAP_BIND(storage);
        TestObject a = {1, 1};

        vsmap_init(&map);

        test_expect(
            vsmap_add(&map, &a) == 0,
            "capacity1 add"
        );

        test_expect(
            vsmap_add(&map, &a) == VSMAP_INVALID_HANDLE,
            "capacity1 full"
        );
    }

    /* Capacity 2 */

    {
        VSMAP_DECLARE(storage, 2);
        vsmap_t map = VSMAP_BIND(storage);
        TestObject a = {1, 1};
        TestObject b = {2, 2};

        vsmap_init(&map);

        vsmap_handle_t ha = vsmap_add(&map, &a);
        vsmap_handle_t hb = vsmap_add(&map, &b);

        test_expect(ha == 0, "capacity2 h0");
        test_expect(hb == 1, "capacity2 h1");
        test_expect(map.count == 2, "capacity2 full");

        vsmap_remove(&map, ha);

        test_expect(
            vsmap_add(&map, &a) == ha,
            "capacity2 reuse"
        );
    }

    /* Capacity 4 */

    {
        VSMAP_DECLARE(storage, 4);
        vsmap_t map = VSMAP_BIND(storage);
        TestObject a = {1, 1};
        TestObject b = {2, 2};
        TestObject c = {3, 3};
        TestObject d = {4, 4};

        vsmap_init(&map);

        test_expect(vsmap_add(&map, &a) == 0, "capacity4 h0");
        test_expect(vsmap_add(&map, &b) == 1, "capacity4 h1");
        test_expect(vsmap_add(&map, &c) == 2, "capacity4 h2");
        test_expect(vsmap_add(&map, &d) == 3, "capacity4 h3");
        test_expect(map.count == 4, "capacity4 full");
    }
}

/* ============================================================================
 * DYNAMIC ALLOCATION
 * ========================================================================== */

static void test_alloc(void)
{
    vsmap_t map = VSMAP_ALLOC(MEM_alloc, TEST_CAPACITY);

    TestObject a = {100, 1000};
    TestObject b = {200, 2000};

    kprintf("=== TEST ALLOC ===");

    test_expect(map.pool != 0, "pool allocated");
    test_expect(map.lookup != 0, "lookup allocated");
    test_expect(map.capacity == TEST_CAPACITY, "allocated capacity");

    vsmap_init(&map);

    vsmap_handle_t ha = vsmap_add(&map, &a);
    vsmap_handle_t hb = vsmap_add(&map, &b);

    test_expect(vsmap_valid(&map, ha), "allocated handle a");
    test_expect(vsmap_valid(&map, hb), "allocated handle b");

    test_expect(
        VSMAP_DATA(&map, ha).value == &a,
        "allocated data a"
    );

    test_expect(
        VSMAP_DATA(&map, hb).value == &b,
        "allocated data b"
    );

    MEM_free(map.pool);
    MEM_free(map.lookup);
}

/* ============================================================================
 * BENCH PREP
 * ========================================================================== */

static void bench_prepare(
    vsmap_t *map,
    vsmap_handle_t *handles,
    TestObject *objects)
{
    vsmap_init(map);

    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        objects[i].id = i;
        objects[i].value = i + 1;
        handles[i] = vsmap_add(map, &objects[i]);
    }
}

/* ============================================================================
 * BENCH
 * ========================================================================== */

static void bench(void)
{
    VSMAP_DECLARE(storage, BENCH_CAPACITY);
    vsmap_t map = VSMAP_BIND(storage);

    static TestObject objects[BENCH_CAPACITY];
    static vsmap_handle_t handles[BENCH_CAPACITY];

    uint32_t timer;
    uint32_t total;
    uint32_t sink = 0;

    kprintf("=== BENCH ===");
    kprintf(
        "capacity=%u iterations=%u",
        BENCH_CAPACITY,
        BENCH_ITERATIONS
    );

    /* INIT */

    total = 0;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
    {
        timer = getSubTick();

        vsmap_init(&map);

        total += getSubTick() - timer;
    }

    kprintf("init=%lu", total);

    /* ADD */

    total = 0;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
    {
        bench_prepare(&map, handles, objects);

        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
            vsmap_remove(&map, handles[i]);

        timer = getSubTick();

        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
            handles[i] = vsmap_add(&map, &objects[i]);

        total += getSubTick() - timer;
    }

    kprintf("add=%lu", total);

    /* VALID */

    bench_prepare(&map, handles, objects);

    total = 0;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
    {
        timer = getSubTick();

        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
            sink += vsmap_valid(&map, handles[i]);

        total += getSubTick() - timer;
    }

    kprintf("valid=%lu", total);

    /* DATA */

    bench_prepare(&map, handles, objects);

    total = 0;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
    {
        timer = getSubTick();

        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
        {
            TestObject *obj =
                (TestObject *)VSMAP_DATA(&map, handles[i]).value;

            sink += obj->value;
        }

        total += getSubTick() - timer;
    }

    kprintf("data=%lu", total);

    /* FOREACH */

    bench_prepare(&map, handles, objects);

    total = 0;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
    {
        timer = getSubTick();

        VSMAP_FOREACH(&map,
        {
            sink += ((TestObject *)item->value)->value;
        });

        total += getSubTick() - timer;
    }

    kprintf("foreach=%lu", total);

    /* REMOVE */

    total = 0;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
    {
        bench_prepare(&map, handles, objects);

        timer = getSubTick();

        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
            vsmap_remove(&map, handles[i]);

        total += getSubTick() - timer;
    }

    kprintf("remove=%lu", total);

    /* REMOVE + ADD */

    bench_prepare(&map, handles, objects);

    total = 0;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
    {
        timer = getSubTick();

        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
        {
            vsmap_remove(&map, handles[i]);
            handles[i] = vsmap_add(&map, &objects[i]);

            sink += handles[i];
        }

        total += getSubTick() - timer;
    }

    kprintf("remove_add=%lu", total);

    bench_sink = sink;

    kprintf("sink=%lu", bench_sink);
}

/* ============================================================================
 * ENTRY
 * ========================================================================== */

void test_vsmap_main(void)
{
    kprintf("==============================");
    kprintf("VSMAP TEST");
    kprintf("==============================");

    test_failures = 0;

    test_setup_objects();

    test_basic();
    test_mapping();
    test_remove();
    test_remove_cases();
    test_free_list();
    test_reuse();
    test_reset();
    test_foreach();
    test_foreach_remove();
    test_invalid();
    test_small_capacities();
    test_alloc();

    kprintf("FAILURES=%u", test_failures);

    bench();

    kprintf("==============================");
    kprintf("DONE");
    kprintf("==============================");
}
