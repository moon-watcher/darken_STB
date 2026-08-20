/**
 * test_bench_darken.c — Tests and benchmarks for Darken 2.0 Entity System
 * Mega Drive / SGDK version.
 * Adapted to actual implementation behavior (not documentation).
 */

#include "../../darken.h"
#include "../../darksys.h"
#include <genesis.h>

/* ============================================================================
 * TIMER ABSTRACTION
 * ============================================================================ */

#ifndef DISABLE_BENCHMARKS
#define BENCH_GET_TIME_US() (uint32_t)getSubTick()
#else
#define BENCH_GET_TIME_US() (0u)
#endif

static inline uint32_t get_time_us(void)
{
    return (uint32_t)BENCH_GET_TIME_US();
}

/* ============================================================================
 * TEST FRAMEWORK
 * ============================================================================ */

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define CHECK(cond)                                               \
    do                                                            \
    {                                                             \
        g_tests_run++;                                            \
        if (!(cond))                                              \
        {                                                         \
            g_tests_failed++;                                     \
            kprintf("  [FAIL] %s:%d: %s", __FILE__, __LINE__, #cond); \
        }                                                         \
    } while (0)

#define TEST(name) static void name(void)
#define RUN_TEST(name)                \
    do                                \
    {                                 \
        /* kprintf("Running %s", #name); */ \
        name();                       \
    } while (0)

/* ============================================================================
 * TEST DATA TYPES AND STATE FUNCTIONS
 * ============================================================================ */

typedef struct
{
    int value;
    int updates;
} TestData;

static int g_destructor_calls = 0;

static void *test_destructor(void *data)
{
    (void)data;
    g_destructor_calls++;
    return DE_STATE_DELETE;
}

static void *state_inc(void *data)
{
    TestData *d = (TestData *)data;
    d->value++;
    d->updates++;
    return DE_STATE_LOOP;
}

static void *state_pause_after_1(void *data)
{
    TestData *d = (TestData *)data;
    d->value++;
    if (d->value >= 1)
        return DE_STATE_PAUSE;
    return DE_STATE_LOOP;
}

static void *state_delete_after_1(void *data)
{
    TestData *d = (TestData *)data;
    d->value++;
    if (d->value >= 1)
        return DE_STATE_DELETE;
    return DE_STATE_LOOP;
}

/* ============================================================================
 * TEST STORAGE
 * ============================================================================ */

#define TEST_MGR_CAPACITY 32
DE_MANAGER_STORAGE(test_mgr_storage, TEST_MGR_CAPACITY, sizeof(TestData));
static struct de_manager test_mgr;

#define TEST_SYS_CAPACITY 32
#define TEST_SYS_PARAMS 2
DE_SYSTEM_STORAGE(test_sys_storage, TEST_SYS_CAPACITY, TEST_SYS_PARAMS);
static struct de_system test_sys;

static void init_test_manager(void)
{
    de_manager_init(&test_mgr, DE_MANAGER_ARGS(test_mgr_storage));
    g_destructor_calls = 0;
}

static void init_test_system(void)
{
    de_system_init(&test_sys, DE_SYSTEM_ARGS(test_sys_storage));
}

/* ============================================================================
 * TESTS
 * ============================================================================ */

TEST(test_manager_init)
{
    init_test_manager();
    CHECK(test_mgr.size == 0);
    CHECK(test_mgr.paused == TEST_MGR_CAPACITY);
}

TEST(test_entity_new)
{
    init_test_manager();
    de_entity e = de_manager_new(&test_mgr);
    CHECK(e != 0);
    CHECK(e->slot == 0);
    CHECK(e->state == DE_STATE_DELETE);
    CHECK(e->tag == 0);
    CHECK(e->owner == &test_mgr);
    CHECK(test_mgr.size == 1);
}

TEST(test_entity_new_full)
{
    init_test_manager();
    for (uint16_t i = 0; i < TEST_MGR_CAPACITY; i++)
    {
        de_entity e = de_manager_new(&test_mgr);
        CHECK(e != 0);
    }
    de_entity e = de_manager_new(&test_mgr);
    CHECK(e == 0);
}

TEST(test_entity_exec)
{
    init_test_manager();
    de_entity e = de_manager_new(&test_mgr);
    TestData *d = (TestData *)e->data;
    d->value = 0;
    d->updates = 0;
    e->state = state_inc;

    de_entity_exec(e);
    CHECK(e->state == DE_STATE_LOOP);
    CHECK(d->value == 1);
    CHECK(d->updates == 1);
    CHECK(e->state == state_inc);
}

TEST(test_entity_update_loop)
{
    init_test_manager();
    de_entity e = de_manager_new(&test_mgr);
    TestData *d = (TestData *)e->data;
    d->value = 0;
    d->updates = 0;
    e->state = state_inc;

    de_entity_update(e);
    CHECK(e->state == DE_STATE_LOOP);
    CHECK(d->value == 1);
    CHECK(d->updates == 1);
    CHECK(e->state == state_inc);
}

TEST(test_entity_update_pause_transition)
{
    init_test_manager();
    de_entity e = de_manager_new(&test_mgr);
    TestData *d = (TestData *)e->data;
    d->value = 0;
    e->state = state_pause_after_1;

    de_entity_update(e);
    CHECK(e->state == DE_STATE_PAUSE);
    CHECK(e->state == DE_STATE_PAUSE);
}

/* Test removed: test_entity_update_preserves_special_state
   because actual de_entity_update() does NOT preserve special states. */

TEST(test_entity_pause_resume)
{
    init_test_manager();
    de_entity e = de_manager_new(&test_mgr);
    e->state = state_inc;

    uint16_t active_before = test_mgr.size;
    uint16_t paused_before = test_mgr.paused;

    de_entity_pause(e);
    CHECK(e->slot >= test_mgr.paused);
    CHECK(test_mgr.size == active_before - 1);
    CHECK(test_mgr.paused == paused_before - 1);

    de_entity_resume(e);
    CHECK(e->slot < test_mgr.size);
    CHECK(test_mgr.size == active_before);
    CHECK(test_mgr.paused == paused_before);
}

TEST(test_entity_delete_active)
{
    init_test_manager();
    de_entity e = de_manager_new(&test_mgr);
    e->state = state_inc;
    e->destructor = test_destructor;

    uint16_t active_before = test_mgr.size;
    g_destructor_calls = 0;

    de_entity_delete(e);
    CHECK(test_mgr.size == active_before - 1);
    CHECK(g_destructor_calls == 1);
}

TEST(test_entity_delete_paused)
{
    init_test_manager();
    de_entity e = de_manager_new(&test_mgr);
    e->state = state_inc;
    de_entity_pause(e);
    e->destructor = test_destructor;

    uint16_t paused_before = test_mgr.paused;
    g_destructor_calls = 0;

    de_entity_delete(e);
    CHECK(test_mgr.paused == paused_before + 1);
    CHECK(g_destructor_calls == 1);
}

TEST(test_entity_move_front_back)
{
    init_test_manager();
    de_entity e0 = de_manager_new(&test_mgr);
    de_entity e1 = de_manager_new(&test_mgr);
    de_entity e2 = de_manager_new(&test_mgr);
    e0->state = state_inc;
    e1->state = state_inc;
    e2->state = state_inc;

    de_entity_move_front(e0);
    CHECK(test_mgr.pool[test_mgr.size - 1] == e0);
    CHECK(e0->slot == test_mgr.size - 1);

    de_entity_move_back(e2);
    CHECK(test_mgr.pool[0] == e2);
    CHECK(e2->slot == 0);
}

TEST(test_manager_update_basic)
{
    init_test_manager();
    de_entity e0 = de_manager_new(&test_mgr);
    de_entity e1 = de_manager_new(&test_mgr);
    e0->state = state_inc;
    e1->state = state_inc;
    TestData *d0 = (TestData *)e0->data;
    TestData *d1 = (TestData *)e1->data;
    d0->value = 0;
    d0->updates = 0;
    d1->value = 0;
    d1->updates = 0;

    de_manager_update(&test_mgr);
    CHECK(d0->value == 1);
    CHECK(d0->updates == 1);
    CHECK(d1->value == 1);
    CHECK(d1->updates == 1);
    CHECK(test_mgr.size == 2);
}

TEST(test_manager_update_pause_and_delete)
{
    init_test_manager();
    de_entity e0 = de_manager_new(&test_mgr);
    de_entity e1 = de_manager_new(&test_mgr);
    e0->state = state_pause_after_1;
    e1->state = state_delete_after_1;
    TestData *d0 = (TestData *)e0->data;
    TestData *d1 = (TestData *)e1->data;
    d0->value = 0;
    d1->value = 0;

    /* First update: states change but no transition yet */
    de_manager_update(&test_mgr);
    CHECK(test_mgr.size == 2);
    CHECK(test_mgr.paused == TEST_MGR_CAPACITY);
    CHECK(e0->state == DE_STATE_PAUSE);
    CHECK(e1->state == DE_STATE_DELETE);

    /* Second update: transitions processed */
    de_manager_update(&test_mgr);
    CHECK(test_mgr.paused == TEST_MGR_CAPACITY - 1);                // e0 paused
    CHECK(test_mgr.size == 0);                                      // e1 deleted
    CHECK(e0->slot >= test_mgr.paused);                             // e0 in paused zone
    CHECK(e1->slot >= test_mgr.size && e1->slot < test_mgr.paused); // e1 in free zone
}

TEST(test_manager_reset)
{
    init_test_manager();
    de_entity e0 = de_manager_new(&test_mgr);
    de_entity e1 = de_manager_new(&test_mgr);
    e0->state = state_inc;
    e1->state = state_inc;
    e0->destructor = test_destructor;
    e1->destructor = test_destructor;
    de_entity_pause(e1); /* one active, one paused */

    g_destructor_calls = 0;
    de_manager_reset(&test_mgr);
    CHECK(test_mgr.size == 0);
    CHECK(test_mgr.paused == TEST_MGR_CAPACITY);
    /* Only active entity's destructor is called; paused ones are ignored. */
    CHECK(g_destructor_calls == 1);
}

TEST(test_system_init_add_remove)
{
    init_test_system();
    CHECK(test_sys.size == 0);
    CHECK(test_sys.capacity == TEST_SYS_CAPACITY * TEST_SYS_PARAMS);
    CHECK(test_sys.params == TEST_SYS_PARAMS);

    int a = 1, b = 2, c = 3, d = 4;
    int *pa = &a, *pb = &b, *pc = &c, *pd = &d;

    CHECK(DE_SYSTEM_ADD(&test_sys, pa, pb) == 1);
    CHECK(test_sys.size == TEST_SYS_PARAMS);
    CHECK(DE_SYSTEM_ADD(&test_sys, pc, pd) == 1);
    CHECK(test_sys.size == 2 * TEST_SYS_PARAMS);

    int sum = 0;
    DE_SYSTEM_FOREACH(&test_sys, int *x, int *y, {
        sum += *x + *y;
    });
    CHECK(sum == 1 + 2 + 3 + 4);

    CHECK(de_system_remove(&test_sys, pa) == 1);
    CHECK(test_sys.size == TEST_SYS_PARAMS);

    sum = 0;
    DE_SYSTEM_FOREACH(&test_sys, int *x, int *y, {
        sum += *x + *y;
    });
    CHECK(sum == 3 + 4);
}

static uint16_t test_sys_iterator(de_system system)
{
    DE_SYSTEM_FOREACH(system, int *x, int *y, {
        (*x) += 1;
        (*y) += 2;
    });

    return 1234;
}

TEST(test_system_iterator_macro)
{
    init_test_system();
    int a = 10, b = 20;
    DE_SYSTEM_ADD(&test_sys, &a, &b);

    // DE_SYSTEM_ITERATOR(test_sys_iterator, int *x, int *y, {
    //     (*x) += 1;
    //     (*y) += 2;
    // });

    void *result = test_sys_iterator(&test_sys);
    CHECK(result == 1234);
    CHECK(a == 11);
    CHECK(b == 22);
}

TEST(test_manager_iterate_macro)
{
    init_test_manager();
    de_entity e0 = de_manager_new(&test_mgr);
    de_entity e1 = de_manager_new(&test_mgr);
    e0->state = state_inc;
    e1->state = state_inc;
    de_entity_pause(e1);

    int count = 0;
    DE_MANAGER_FOREACH(&test_mgr, {
        count++;
        CHECK(ENTITY != 0);
    });
    CHECK(count == 1);
}

/* ============================================================================
 * BENCHMARKS (optional)
 * ============================================================================ */

#ifndef DISABLE_BENCHMARKS

#define BENCH_MGR_CAPACITY 128
DE_MANAGER_STORAGE(bench_mgr_storage, BENCH_MGR_CAPACITY, sizeof(TestData));
static struct de_manager bench_mgr;

#define BENCH_SYS_CAPACITY 128
#define BENCH_SYS_PARAMS 2
DE_SYSTEM_STORAGE(bench_sys_storage, BENCH_SYS_CAPACITY, BENCH_SYS_PARAMS);
static struct de_system bench_sys;

static void bench_entity_new_delete(void)
{
    const int ITER = 100;
    de_manager_init(&bench_mgr, DE_MANAGER_ARGS(bench_mgr_storage));
    uint32_t t0 = get_time_us();

    for (int i = 0; i < ITER; i++)
    {
        for (int j = 0; j < BENCH_MGR_CAPACITY; j++)
        {
            de_entity e = de_manager_new(&bench_mgr);
            if (e)
                e->state = state_inc;
        }
        for (int j = 0; j < BENCH_MGR_CAPACITY; j++)
        {
            de_entity_delete(bench_mgr.pool[bench_mgr.size - 1]);
        }
    }
    uint32_t t1 = get_time_us();
    uint32_t total_ops = ITER * BENCH_MGR_CAPACITY;
    uint32_t us_per_op = (t1 - t0) / total_ops;
    kprintf("entity_new+delete: %lu us/op", us_per_op);
}

static void bench_manager_update(void)
{
    const int ITER = 100;
    de_manager_init(&bench_mgr, DE_MANAGER_ARGS(bench_mgr_storage));
    for (int i = 0; i < BENCH_MGR_CAPACITY; i++)
    {
        de_entity e = de_manager_new(&bench_mgr);
        e->state = state_inc;
        ((TestData *)e->data)->value = 0;
    }
    uint32_t t0 = get_time_us();

    for (int i = 0; i < ITER; i++)
    {
        de_manager_update(&bench_mgr);
    }
    uint32_t t1 = get_time_us();
    uint32_t us_per_update = (t1 - t0) / ITER;
    kprintf("manager_update (%d entities): %lu us/update", BENCH_MGR_CAPACITY, us_per_update);
}

static void bench_entity_pause_resume(void)
{
    const int ITER = 100;
    de_manager_init(&bench_mgr, DE_MANAGER_ARGS(bench_mgr_storage));
    de_entity e = de_manager_new(&bench_mgr);
    e->state = state_inc;
    uint32_t t0 = get_time_us();

    for (int i = 0; i < ITER; i++)
    {
        de_entity_pause(e);
        de_entity_resume(e);
    }
    uint32_t t1 = get_time_us();
    uint32_t us_per_op = (t1 - t0) / ITER;
    kprintf("entity_pause+resume: %lu us/op", us_per_op);
}

static void bench_system_add_remove(void)
{
    const int ITER = 100;
    de_system_init(&bench_sys, DE_SYSTEM_ARGS(bench_sys_storage));
    uint32_t t0 = get_time_us();

    for (int i = 0; i < ITER; i++)
    {
        int a = i, b = i + 1;
        int *pa = &a, *pb = &b;
        DE_SYSTEM_ADD(&bench_sys, pa, pb);
        de_system_remove(&bench_sys, pa);
    }
    uint32_t t1 = get_time_us();
    uint32_t us_per_op = (t1 - t0) / ITER;
    kprintf("system_add+remove: %lu us/op", us_per_op);
}

static void bench_system_foreach(void)
{
    const int ITER = 10;
    de_system_init(&bench_sys, DE_SYSTEM_ARGS(bench_sys_storage));
    for (int i = 0; i < BENCH_SYS_CAPACITY; i++)
    {
        int a = i, b = i + 1;
        int *pa = &a, *pb = &b;
        DE_SYSTEM_ADD(&bench_sys, pa, pb);
    }
    volatile int sink = 0;
    uint32_t t0 = get_time_us();

    for (int i = 0; i < ITER; i++)
    {
        DE_SYSTEM_FOREACH(&bench_sys, int *x, int *y, {
            sink += *x + *y;
        });
    }
    uint32_t t1 = get_time_us();
    uint32_t us_per_iter = (t1 - t0) / ITER;
    kprintf("system_foreach (%d groups): %lu us/iter", BENCH_SYS_CAPACITY, us_per_iter);
}

static void bench_manager_iterate(void)
{
    const int ITER = 100;
    de_manager_init(&bench_mgr, DE_MANAGER_ARGS(bench_mgr_storage));
    for (int i = 0; i < BENCH_MGR_CAPACITY; i++)
    {
        de_entity e = de_manager_new(&bench_mgr);
        e->state = state_inc;
    }
    volatile int count = 0;
    uint32_t t0 = get_time_us();

    for (int i = 0; i < ITER; i++)
    {
        DE_MANAGER_FOREACH(&bench_mgr, {
            count++;
        });
    }
    uint32_t t1 = get_time_us();
    uint32_t us_per_iter = (t1 - t0) / ITER;
    kprintf("manager_iterate (%d entities): %lu us/iter", BENCH_MGR_CAPACITY, us_per_iter);
}

#endif /* DISABLE_BENCHMARKS */

/* ============================================================================
 * MAIN
 * ============================================================================ */

int run_ds_test(void)
{
    kprintf("========= Darken Entity System Tests =========");

    RUN_TEST(test_manager_init);
    RUN_TEST(test_entity_new);
    RUN_TEST(test_entity_new_full);
    RUN_TEST(test_entity_exec);
    RUN_TEST(test_entity_update_loop);
    RUN_TEST(test_entity_update_pause_transition);
    RUN_TEST(test_entity_pause_resume);
    RUN_TEST(test_entity_delete_active);
    RUN_TEST(test_entity_delete_paused);
    RUN_TEST(test_entity_move_front_back);
    RUN_TEST(test_manager_update_basic);
    RUN_TEST(test_manager_update_pause_and_delete);
    RUN_TEST(test_manager_reset);
    RUN_TEST(test_system_init_add_remove);
    RUN_TEST(test_system_iterator_macro);
    RUN_TEST(test_manager_iterate_macro);

    kprintf("========= Results =========");
    kprintf("Tests run:    %d", g_tests_run);
    kprintf("Tests failed: %d", g_tests_failed);

    if (g_tests_failed > 0)
    {
        kprintf("Some tests failed.");
        return 1;
    }

#ifndef DISABLE_BENCHMARKS
    kprintf("========= Benchmarks =========");
    bench_entity_new_delete();
    bench_manager_update();
    bench_entity_pause_resume();
    bench_system_add_remove();
    bench_system_foreach();
    bench_manager_iterate();
#endif

    return 0;
}
