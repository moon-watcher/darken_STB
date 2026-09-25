#include <genesis.h>
#include "../darken-1.4.0_dev.h"

/*
 * Darken 1.4 SGDK test / benchmark suite.
 *
 * Target: Sega Mega Drive / Genesis, Motorola 68000, SGDK.
 *
 * This suite intentionally uses only SGDK facilities plus darken.h:
 *   - genesis.h / SGDK integer types
 *   - kprintf() for KDebug output
 *   - VDP_drawText() for a small on-screen status
 *   - SYS_doVBlankProcess() for frame pacing
 *   - getSubTick() for timing
 *
 * No stdio, stdlib, string.h, or other C library headers are used.
 */

#define TEST_CAPACITY 32
#define BENCH_CAPACITY 64
#define BENCH_ROUNDS 512

typedef struct test_payload
{
    u16 id;
    u16 updates;
    u32 value;
} test_payload;

static u16 test_failures;
static u16 destroy_calls;
static u16 update_calls;

static void test_reset_counters(void)
{
    test_failures = 0;
    destroy_calls = 0;
    update_calls = 0;
}

static void test_check(u16 condition, const char *name)
{
    if (condition)
        kprintf("PASS: %s", name);
    else
    {
        kprintf("FAIL: %s", name);
        test_failures++;
    }
}

static void destroy_test(void *data)
{
    test_payload *p = (test_payload *)data;
    destroy_calls++;
    p->value = 0xD00D;
}

static void *state_continue(void *data)
{
    test_payload *p = (test_payload *)data;
    update_calls++;
    p->updates++;
    p->value++;
    return DARKEN_CONTINUE;
}

static void *state_delete(void *data)
{
    test_payload *p = (test_payload *)data;
    update_calls++;
    p->value++;
    return DARKEN_DELETE;
}

static void *state_switch(void *data)
{
    test_payload *p = (test_payload *)data;
    update_calls++;
    p->value += 10;
    return (darken_state_t)state_continue;
}

static void *bench_update_callback(void *data)
{
    test_payload *p = (test_payload *)data;
    p->value++;
    return DARKEN_CONTINUE;
}

DARKEN_DECLARE(test_storage, TEST_CAPACITY, sizeof(test_payload));
DARKEN_DECLARE(bench_storage_a, BENCH_CAPACITY, sizeof(test_payload));
DARKEN_DECLARE(bench_storage_b, BENCH_CAPACITY, sizeof(test_payload));

static darken_t test_ctx;
static darken_t bench_a;
static darken_t bench_b;

static void test_init_and_spawn(void)
{
    u16 i;
    darken_entity_t e;

    kprintf("== init / spawn ==");

    test_ctx = DARKEN_BIND(test_storage);
    darken_init(&test_ctx);

    test_check(test_ctx.size == 0, "init size == 0");
    test_check(test_ctx.capacity == TEST_CAPACITY, "init capacity");

    for (i = 0; i < TEST_CAPACITY; i++)
    {
        e = DARKEN_SPAWN(&test_ctx);
        test_check(e != 0, "spawn returns entity");
        if (e)
        {
            test_payload *p = (test_payload *)e->data;
            p->id = i;
            p->updates = 0;
            p->value = i + 1;
            e->update = (darken_state_t)state_continue;
            e->destroy = (darken_state_t)destroy_test;
            e->tag = 0x1000 + i;
            e->usr = i;
        }
    }

    test_check(test_ctx.size == TEST_CAPACITY, "spawn fills active zone");
    test_check(DARKEN_SPAWN(&test_ctx) == 0, "spawn on full returns 0");
    test_check(DARKEN_COUNT_ACTIVE(&test_ctx) == TEST_CAPACITY, "count active full");
    test_check(DARKEN_COUNT_FREE(&test_ctx) == 0, "count free empty");
}

static void test_update_and_state_change(void)
{
    darken_entity_t e;
    test_payload *p;

    kprintf("== update / state machine ==");

    test_ctx.size = 0;
    darken_init(&test_ctx);

    e = DARKEN_SPAWN(&test_ctx);
    p = (test_payload *)e->data;
    p->value = 7;
    e->update = (darken_state_t)state_switch;
    e->destroy = (darken_state_t)destroy_test;

    update_calls = 0;
    darken_update(&test_ctx);

    test_check(update_calls == 1, "update callback called");
    test_check(p->value == 17, "state callback changed payload");
    test_check(e->update == (darken_state_t)state_continue, "returned callback installed");

    darken_update(&test_ctx);
    test_check(update_calls == 2, "installed callback runs next update");
    test_check(p->updates == 1, "continue callback updates payload");
}

static void test_delete_and_recycle(void)
{
    darken_entity_t e0;
    darken_entity_t e1;
    test_payload *p0;
    test_payload *p1;

    kprintf("== delete / recycle ==");

    darken_init(&test_ctx);

    e0 = DARKEN_SPAWN(&test_ctx);
    e1 = DARKEN_SPAWN(&test_ctx);

    p0 = (test_payload *)e0->data;
    p1 = (test_payload *)e1->data;

    p0->value = 0x1111;
    p1->value = 0x2222;

    e0->update = (darken_state_t)state_continue;
    e0->destroy = (darken_state_t)destroy_test;
    e1->update = (darken_state_t)state_continue;
    e1->destroy = (darken_state_t)destroy_test;

    destroy_calls = 0;
    darken_entity_delete(e0);

    test_check(destroy_calls == 1, "delete calls destroy");
    test_check(test_ctx.size == 1, "delete decrements active count");
    test_check(DARKEN_COUNT_FREE(&test_ctx) == TEST_CAPACITY - 1, "delete creates free slot");

    e0 = DARKEN_SPAWN(&test_ctx);
    test_check(e0 != 0, "recycle spawn succeeds");
    if (e0)
    {
        p0 = (test_payload *)e0->data;
        test_check(p0->value == 0xD00D, "recycled slot retains old payload");
        p0->value = 0;
        e0->update = (darken_state_t)state_continue;
        e0->destroy = 0;
    }

    (void)p1;
}

static void test_foreach_delete(void)
{
    u16 i;
    u16 before;
    darken_entity_t e;

    kprintf("== reverse foreach delete ==");

    darken_init(&test_ctx);

    for (i = 0; i < 10; i++)
    {
        e = DARKEN_SPAWN(&test_ctx);
        e->update = (darken_state_t)state_continue;
        e->destroy = 0;
        ((test_payload *)e->data)->id = i;
    }

    before = test_ctx.size;
    DARKEN_FOREACH(&test_ctx, {
        if (((test_payload *)_entity->data)->id & 1)
            darken_entity_delete(_entity);
    });

    test_check(before == 10, "foreach started with ten");
    test_check(test_ctx.size == 5, "foreach delete leaves five");
    test_check(DARKEN_COUNT_FREE(&test_ctx) == TEST_CAPACITY - 5, "free count after foreach delete");
}

static void test_cross_context_swap(void)
{
    darken_entity_t a;
    darken_entity_t b;
    darken_t *owner_a;
    darken_t *owner_b;

    kprintf("== cross-context swap ==");

    darken_init(&test_ctx);
    darken_init(&bench_a);

    a = DARKEN_SPAWN(&test_ctx);
    b = DARKEN_SPAWN(&bench_a);

    a->usr = 0xAAAA;
    b->usr = 0xBBBB;

    owner_a = a->owner;
    owner_b = b->owner;

    darken_entity_swap(a, b);

    test_check(a->owner == owner_b, "swap transfers first owner");
    test_check(b->owner == owner_a, "swap transfers second owner");
    test_check(a->slot == 0 && b->slot == 0, "swap keeps slot indices consistent");
    test_check(owner_b->pool[a->slot] == a, "destination pool points to first entity");
    test_check(owner_a->pool[b->slot] == b, "source pool points to second entity");
}

static void test_migrate(void)
{
    darken_entity_t src_entity;
    darken_entity_t moved;

    kprintf("== migrate ==");

    darken_init(&test_ctx);
    darken_init(&bench_a);

    src_entity = DARKEN_SPAWN(&test_ctx);
    ((test_payload *)src_entity->data)->id = 0x55AA;
    ((test_payload *)src_entity->data)->value = 0x12345678;
    src_entity->usr = 0x4321;
    src_entity->tag = 0xA5A5A5A5UL;
    src_entity->update = (darken_state_t)state_continue;

    moved = darken_entity_migrate(src_entity, &bench_a);

    test_check(moved != 0, "migrate returns destination entity");
    if (moved)
    {
        test_payload *p = (test_payload *)moved->data;
        test_check(p->id == 0x55AA, "migrate copies payload");
        test_check(p->value == 0x12345678UL, "migrate copies 32-bit payload value");
        test_check(moved->usr == 0x4321, "migrate copies common header");
        test_check(moved->tag == 0xA5A5A5A5UL, "migrate copies tag");
        test_check(moved->owner == &bench_a, "migrate fixes owner");
        test_check(moved->slot == 0, "migrate fixes destination slot");
        test_check(test_ctx.size == 0, "migrate removes active source");
        test_check(bench_a.size == 1, "migrate adds destination active");
    }
}

static void test_free_migrate(void)
{
    darken_entity_t free_entity;
    darken_entity_t moved;

    kprintf("== free-entity migrate ==");

    darken_init(&test_ctx);
    darken_init(&bench_a);

    free_entity = test_ctx.pool[TEST_CAPACITY - 1];
    ((test_payload *)free_entity->data)->value = 0xCAFEBABEUL;

    moved = darken_entity_migrate(free_entity, &bench_a);

    test_check(moved != 0, "free entity migrate returns destination");
    test_check(test_ctx.size == 0, "free source remains free");
    test_check(bench_a.size == 1, "free source migration creates destination active");
    if (moved)
        test_check(((test_payload *)moved->data)->value == 0xCAFEBABEUL,
                   "free source bytes are copied");
}

static void run_tests(void)
{
    test_reset_counters();

    test_ctx = DARKEN_BIND(test_storage);
    bench_a = DARKEN_BIND(bench_storage_a);
    bench_b = DARKEN_BIND(bench_storage_b);

    test_init_and_spawn();
    test_update_and_state_change();
    test_delete_and_recycle();
    test_foreach_delete();
    test_cross_context_swap();
    test_migrate();
    test_free_migrate();

    kprintf("== TEST RESULT ==");
    if (test_failures == 0)
        kprintf("ALL TESTS PASSED");
    else
        kprintf("TEST FAILURES: %u", test_failures);
}

static u32 bench_elapsed(u32 start)
{
    return getSubTick() - start;
}

static void bench_spawn_delete(void)
{
    u16 round;
    u16 i;
    u32 start;
    u32 elapsed;
    darken_entity_t e;

    darken_init(&bench_a);

    start = getSubTick();

    for (round = 0; round < BENCH_ROUNDS; round++)
    {
        for (i = 0; i < BENCH_CAPACITY; i++)
        {
            e = DARKEN_SPAWN(&bench_a);
            e->update = (darken_state_t)bench_update_callback;
            e->destroy = 0;
            ((test_payload *)e->data)->value = i;
        }

        for (i = 0; i < BENCH_CAPACITY; i++)
            darken_entity_delete(bench_a.pool[bench_a.size - 1]);
    }

    elapsed = bench_elapsed(start);

    kprintf("BENCH spawn+delete: %u rounds, %u ops, %u subticks, %u subticks/op",
            BENCH_ROUNDS, (u32)BENCH_ROUNDS * BENCH_CAPACITY * 2,
            elapsed, elapsed / ((u32)BENCH_ROUNDS * BENCH_CAPACITY * 2));
}

static void bench_update(void)
{
    u16 round;
    u16 i;
    u32 start;
    u32 elapsed;
    darken_entity_t e;

    darken_init(&bench_a);

    for (i = 0; i < BENCH_CAPACITY; i++)
    {
        e = DARKEN_SPAWN(&bench_a);
        e->update = (darken_state_t)bench_update_callback;
        e->destroy = 0;
        ((test_payload *)e->data)->value = i;
    }

    start = getSubTick();

    for (round = 0; round < BENCH_ROUNDS; round++)
        darken_update(&bench_a);

    elapsed = bench_elapsed(start);

    kprintf("BENCH update: %u entities x %u rounds, %u callbacks, %u subticks, %u subticks/callback",
            BENCH_CAPACITY, BENCH_ROUNDS,
            (u32)BENCH_CAPACITY * BENCH_ROUNDS,
            elapsed,
            elapsed / ((u32)BENCH_CAPACITY * BENCH_ROUNDS));
}

static void bench_foreach(void)
{
    u16 round;
    u16 i;
    u32 start;
    u32 elapsed;
    darken_entity_t e;
    u32 checksum = 0;

    darken_init(&bench_a);

    for (i = 0; i < BENCH_CAPACITY; i++)
    {
        e = DARKEN_SPAWN(&bench_a);
        e->update = (darken_state_t)bench_update_callback;
        e->destroy = 0;
        ((test_payload *)e->data)->value = i;
    }

    start = getSubTick();

    for (round = 0; round < BENCH_ROUNDS; round++)
    {
        DARKEN_FOREACH(&bench_a, {
            checksum += ((test_payload *)_entity->data)->value;
        });
    }

    elapsed = bench_elapsed(start);

    kprintf("BENCH foreach: %u entities x %u rounds, %u visits, %u subticks, %u subticks/visit",
            BENCH_CAPACITY, BENCH_ROUNDS,
            (u32)BENCH_CAPACITY * BENCH_ROUNDS,
            elapsed,
            elapsed / ((u32)BENCH_CAPACITY * BENCH_ROUNDS));

    if (checksum == 0)
        kprintf("checksum guard");
}

static void bench_swap(void)
{
    u16 round;
    u16 i;
    u32 start;
    u32 elapsed;
    darken_entity_t e0;
    darken_entity_t e1;

    darken_init(&bench_a);

    for (i = 0; i < BENCH_CAPACITY; i++)
    {
        e0 = DARKEN_SPAWN(&bench_a);
        e0->update = (darken_state_t)bench_update_callback;
        e0->destroy = 0;
        ((test_payload *)e0->data)->value = i;
    }

    start = getSubTick();

    for (round = 0; round < BENCH_ROUNDS; round++)
    {
        for (i = 1; i < BENCH_CAPACITY; i++)
            darken_entity_swap(bench_a.pool[0], bench_a.pool[i]);
    }

    elapsed = bench_elapsed(start);

    kprintf("BENCH swap: %u rounds, %u swaps, %u subticks, %u subticks/swap",
            BENCH_ROUNDS, (u32)BENCH_ROUNDS * (BENCH_CAPACITY - 1),
            elapsed,
            elapsed / ((u32)BENCH_ROUNDS * (BENCH_CAPACITY - 1)));
}

static void bench_migrate(void)
{
    u16 round;
    u16 i;
    u32 start;
    u32 elapsed;
    darken_entity_t e;

    start = getSubTick();

    for (round = 0; round < BENCH_ROUNDS; round++)
    {
        darken_init(&bench_a);
        darken_init(&bench_b);

        for (i = 0; i < BENCH_CAPACITY; i++)
        {
            e = DARKEN_SPAWN(&bench_a);
            e->update = (darken_state_t)bench_update_callback;
            e->destroy = 0;
            ((test_payload *)e->data)->value = i;
        }

        for (i = 0; i < BENCH_CAPACITY; i++)
            darken_entity_migrate(bench_a.pool[0], &bench_b);
    }

    elapsed = bench_elapsed(start);

    kprintf("BENCH migrate: %u rounds, %u migrations, %u subticks, %u subticks/migration",
            BENCH_ROUNDS, (u32)BENCH_ROUNDS * BENCH_CAPACITY,
            elapsed,
            elapsed / ((u32)BENCH_ROUNDS * BENCH_CAPACITY));
}

static void run_benchmarks(void)
{
    kprintf("========================================");
    kprintf(" DARKEN 1.4 / SGDK / 68K BENCHMARKS");
    kprintf("========================================");

    bench_spawn_delete();
    bench_update();
    bench_foreach();
    bench_swap();
    bench_migrate();

    kprintf("BENCHMARKS COMPLETE");
}

int darken_sgdk_tests_main(bool hardReset)
{
    (void)hardReset;

    VDP_setScreenWidth320();
    VDP_setTextPlane(BG_A);
    VDP_clearPlane(BG_A, TRUE);

    kprintf("");
    kprintf("Darken 1.4 SGDK test/bench ROM");
    kprintf("Target: Mega Drive / 68000");

    run_tests();
    run_benchmarks();

    VDP_drawText("DARKEN 1.4 TESTS/BENCH", 7, 2);
    VDP_drawText("See KDebug output for results.", 4, 4);
    VDP_drawText("ROM is intentionally static-storage only.", 2, 6);

    return 0;
}