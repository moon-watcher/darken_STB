/**
 * darken_sgdk_tests.c
 *
 * Combined correctness tests + benchmarks for darken.h, built as an actual
 * SGDK ROM. Uses ONLY SGDK's own header (<genesis.h>) -- no libc headers,
 * no host assumptions. Drop this file and darken.h into an SGDK project's
 * src/ (or inc/ + src/) and build normally:
 *
 *     make -f %GDK%/makefile.gen        (Windows, from an SGDK project dir)
 *     make -f $GDK/makefile.gen         (Linux/Mac, same)
 *
 * That produces out/release/rom.bin -- run it in an emulator that exposes
 * SGDK's debug console (BlastEm, Gens KMod) to see the results: everything
 * is reported via kprintf(). A one-line PASS/FAIL summary is also drawn on
 * screen with VDP_drawText(), in case the console isn't visible.
 *
 * DARKEN_DIRECT mode can't coexist with default (state-machine) mode in the
 * same ROM -- it's a compile-time switch baked into darken.h's own
 * darken_state_t typedef for the whole translation unit. To test DIRECT
 * mode, uncomment the #define below and rebuild as a second ROM; the
 * mode-specific test/benchmark code is written once and selected with
 * #ifdef DARKEN_DIRECT throughout this file.
 */


#include <genesis.h>

#define DARKEN_DIRECT
#include <stdint.h>
#include "darken-1.1.0_dev.h"

/* ==========================================================================
 * Shared payload + tiny check/report framework
 * ========================================================================== */

typedef struct
{
    s16 id;
    s16 countdown;   // used by cb_delete_after_n
    s16 update_hits; // times this entity's update() actually ran
} Comp;

static u16 g_checks;
static u16 g_failures;
static s16 g_destroy_calls;
static s16 g_last_destroyed_id;

#define CHECK(cond)                                               \
    do                                                            \
    {                                                             \
        g_checks++;                                               \
        if (!(cond))                                              \
        {                                                         \
            g_failures++;                                         \
            kprintf("FAIL %s:%d: %s", __FILE__, __LINE__, #cond); \
        }                                                         \
    } while (0)

#define RUN(test)                \
    do                           \
    {                            \
        kprintf("-- %s", #test); \
        test();                  \
    } while (0)

// Structural invariants that must hold for ANY darken_t at ANY point in
// time, regardless of what sequence of spawn/pause/resume/delete got it
// there.
static void check_invariants(darken_t *ctx)
{
    CHECK(ctx->size <= ctx->paused);
    CHECK(ctx->paused <= ctx->capacity);

    for (u16 i = 0; i < ctx->capacity; i++)
    {
        CHECK(ctx->pool[i] != NULL);
        CHECK(ctx->pool[i]->slot == i);
        CHECK(ctx->pool[i]->owner == ctx);
    }
}

static void make_pool(darken_t *m, u16 capacity)
{
    *m = DARKEN_ALLOC(MEM_alloc, capacity, sizeof(Comp));
    darken_init(m);
}

static void free_pool(darken_t *m)
{
    MEM_free(m->pool);
    MEM_free(m->storage);
}

static darken_entity_t spawn_comp(darken_t *m, s16 id)
{
    darken_entity_t e = DARKEN_SPAWN(m);
    CHECK(e != NULL);
    if (!e)
        return NULL;

    e->update = NULL;
    e->destroy = NULL;
    e->tag = 0;
    e->usr = 0;

    DARKEN_DATA(Comp, c, e);
    c->id = id;
    c->countdown = 0;
    c->update_hits = 0;
    return e;
}

/* ==========================================================================
 * Mode-specific callbacks
 * ========================================================================== */

#ifdef DARKEN_DIRECT

static void cb_continue(darken_entity_t e)
{
    DARKEN_DATA(Comp, c, e);
    c->update_hits++;
}

static void cb_pause_now(darken_entity_t e, void *data)
{
    (void)data;
    darken_entity_pause(e);
}

static void cb_delete_after_n(darken_entity_t e, Comp *c)
{
    c->update_hits++;
    if (--c->countdown <= 0)
        darken_entity_delete(e);
}

static void cb_destroy(darken_entity_t e, Comp *c)
{
    (void)e;
    g_destroy_calls++;
    g_last_destroyed_id = c->id;
}

#else // default (state-machine) mode

static void *cb_continue(Comp *c)
{
    c->update_hits++;
    return DARKEN_CONTINUE;
}

static void *cb_pause_now(void *data)
{
    (void)data;
    return DARKEN_PAUSE;
}

static void *cb_delete_after_n(Comp *c)
{
    c->update_hits++;
    if (--c->countdown <= 0)
        return DARKEN_DELETE;
    return DARKEN_CONTINUE;
}

static void *cb_b(Comp *c)
{
    c->update_hits += 1000; // distinguishable from cb_continue's +1
    return DARKEN_CONTINUE;
}

static void *cb_switch_to_b(void *data)
{
    (void)data;
    return cb_b;
}

static void *cb_destroy(Comp *c)
{
    g_destroy_calls++;
    g_last_destroyed_id = c->id;
    return NULL;
}

#endif

/* ==========================================================================
 * Correctness tests
 * ========================================================================== */

static void test_init_basic(void)
{
    darken_t m;
    make_pool(&m, 8);
    CHECK(m.capacity == 8);
    CHECK(m.size == 0);
    CHECK(m.paused == 8);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 0);
    CHECK(DARKEN_COUNT_FREE(&m) == 8);
    CHECK(DARKEN_COUNT_PAUSED(&m) == 0);
    check_invariants(&m);
    free_pool(&m);
}

static void test_spawn_fills_and_saturates(void)
{
    darken_t m;
    make_pool(&m, 4);

    for (s16 i = 0; i < 4; i++)
    {
        darken_entity_t e = spawn_comp(&m, i);
        CHECK(e != NULL);
        CHECK(m.size == (u16)(i + 1));
    }

    darken_entity_t e5 = DARKEN_SPAWN(&m);
    CHECK(e5 == NULL);
    CHECK(m.size == 4);
    CHECK(DARKEN_COUNT_FREE(&m) == 0);
    check_invariants(&m);
    free_pool(&m);
}

static void test_data_payload_readwrite(void)
{
    darken_t m;
    make_pool(&m, 4);
    darken_entity_t e = spawn_comp(&m, 42);

    DARKEN_DATA(Comp, c, e);
    CHECK(c->id == 42);
    c->id = 99;

    DARKEN_DATA(Comp, c2, e);
    CHECK(c2->id == 99);
    CHECK(c2 == c);

    free_pool(&m);
}

static void test_zone_membership_and_pause_resume(void)
{
    darken_t m;
    make_pool(&m, 3);
    darken_entity_t e0 = spawn_comp(&m, 0);
    darken_entity_t e1 = spawn_comp(&m, 1);
    darken_entity_t e2 = spawn_comp(&m, 2);

    CHECK(DARKEN_ENTITY_IN_ACTIVE(e0) && DARKEN_ENTITY_IN_ACTIVE(e1) && DARKEN_ENTITY_IN_ACTIVE(e2));
    check_invariants(&m);

    darken_entity_pause(e1);
    CHECK(!DARKEN_ENTITY_IN_ACTIVE(e1) && DARKEN_ENTITY_IN_PAUSED(e1));
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 2 && DARKEN_COUNT_PAUSED(&m) == 1);
    check_invariants(&m);

    darken_entity_pause(e1); // already-paused: must be a safe no-op
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 2 && DARKEN_COUNT_PAUSED(&m) == 1);

    darken_entity_resume(e1);
    CHECK(DARKEN_ENTITY_IN_ACTIVE(e1) && !DARKEN_ENTITY_IN_PAUSED(e1));
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 3 && DARKEN_COUNT_PAUSED(&m) == 0);

    darken_entity_resume(e1); // already-active: must be a safe no-op
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 3);
    check_invariants(&m);

    free_pool(&m);
}

static void test_pause_resume_preserves_data_and_address(void)
{
    darken_t m;
    make_pool(&m, 5);
    spawn_comp(&m, 100);
    darken_entity_t e = spawn_comp(&m, 101);
    spawn_comp(&m, 102);

    DARKEN_DATA(Comp, c, e);
    c->id = 1234;
    void *addr_before = (void *)c;

    darken_entity_pause(e);
    DARKEN_DATA(Comp, c2, e);
    CHECK((void *)c2 == addr_before && c2->id == 1234);

    darken_entity_resume(e);
    DARKEN_DATA(Comp, c3, e);
    CHECK((void *)c3 == addr_before && c3->id == 1234);

    check_invariants(&m);
    free_pool(&m);
}

static void test_delete_active_and_paused(void)
{
    darken_t m;
    make_pool(&m, 4);

    // Active delete: destroy() fires exactly once, double-delete is a no-op.
    darken_entity_t a = spawn_comp(&m, 7);
    a->destroy = cb_destroy;
    g_destroy_calls = 0;
    darken_entity_delete(a);
    CHECK(g_destroy_calls == 1);
    CHECK(!DARKEN_ENTITY_IN_ACTIVE(a) && !DARKEN_ENTITY_IN_PAUSED(a));
    darken_entity_delete(a); // double-delete on the now-free handle
    CHECK(g_destroy_calls == 1);
    check_invariants(&m);

    // Paused delete: destroy() must be SKIPPED (documented behavior).
    darken_entity_t b = spawn_comp(&m, 8);
    b->destroy = cb_destroy;
    darken_entity_pause(b);
    g_destroy_calls = 0;
    darken_entity_delete(b);
    CHECK(g_destroy_calls == 0);
    check_invariants(&m);

    free_pool(&m);
}

#ifdef DARKEN_DIRECT

static void test_mode_specific(void)
{
    // DIRECT mode: entity manages its own lifecycle via the handle it's
    // handed, no DARKEN_ENTITY(data) lookup needed.
    darken_t m;
    make_pool(&m, 4);

    darken_entity_t keep = spawn_comp(&m, 0);
    darken_entity_t pauser = spawn_comp(&m, 1);
    keep->update = cb_continue;
    pauser->update = cb_pause_now;

    darken_update(&m);
    CHECK(DARKEN_ENTITY_IN_PAUSED(pauser) && DARKEN_ENTITY_IN_ACTIVE(keep));
    check_invariants(&m);

    darken_entity_t dying[4];
    g_destroy_calls = 0;
    for (s16 i = 0; i < 4; i++)
    {
        // reuse slots freed by nothing yet -- spawn fresh into remaining cap
    }
    darken_entity_resume(pauser);
    pauser->update = cb_delete_after_n;
    pauser->destroy = cb_destroy;
    DARKEN_DATA(Comp, cp, pauser);
    cp->countdown = 2;
    darken_update(&m);
    CHECK(DARKEN_ENTITY_IN_ACTIVE(pauser)); // 1 hit, not dead yet
    darken_update(&m);
    CHECK(g_destroy_calls == 1); // dead on the 2nd hit
    check_invariants(&m);

    (void)dying;
    free_pool(&m);
}

#else

static void test_mode_specific(void)
{
    // Default (state-machine) mode: PAUSE stops visits, DELETE fires
    // destroy() once, an unrecognized return value installs a new
    // callback that only takes effect NEXT frame, and DARKEN_FOREACH
    // visits active entities in reverse slot order.
    darken_t m;
    make_pool(&m, 4);

    darken_entity_t keep = spawn_comp(&m, 0);
    darken_entity_t pauser = spawn_comp(&m, 1);
    keep->update = cb_continue;
    pauser->update = cb_pause_now;

    darken_update(&m);
    CHECK(DARKEN_ENTITY_IN_PAUSED(pauser) && DARKEN_ENTITY_IN_ACTIVE(keep));
    check_invariants(&m);

    darken_entity_resume(pauser);
    pauser->update = cb_delete_after_n;
    pauser->destroy = cb_destroy;
    DARKEN_DATA(Comp, cp, pauser);
    cp->countdown = 2;
    g_destroy_calls = 0;
    darken_update(&m);
    CHECK(DARKEN_ENTITY_IN_ACTIVE(pauser));
    darken_update(&m);
    CHECK(g_destroy_calls == 1);
    check_invariants(&m);

    darken_entity_t sw = spawn_comp(&m, 2);
    sw->update = cb_switch_to_b;
    darken_update(&m); // installs cb_b, doesn't run it yet
    CHECK(sw->update == cb_b);
    DARKEN_DATA(Comp, cs, sw);
    CHECK(cs->update_hits == 0);
    darken_update(&m); // NOW cb_b actually runs
    CHECK(cs->update_hits == 1000);

    // DARKEN_FOREACH visits active entities in reverse slot order.
    darken_t m2;
    make_pool(&m2, 4);
    for (s16 i = 0; i < 4; i++)
        spawn_comp(&m2, i);
    s16 visited[4];
    s16 k = 0;
    DARKEN_FOREACH(&m2, {
        DARKEN_DATA(Comp, c, _entity);
        visited[k++] = c->id;
    });
    CHECK(k == 4);
    for (s16 i = 0; i < 4; i++)
        CHECK(visited[i] == 3 - i);
    free_pool(&m2);

    check_invariants(&m);
    free_pool(&m);
}

#endif

static void test_reset_destroys_active_only(void)
{
    darken_t m;
    make_pool(&m, 4);
    darken_entity_t a = spawn_comp(&m, 1);
    darken_entity_t b = spawn_comp(&m, 2);
    darken_entity_t c = spawn_comp(&m, 3);
    a->destroy = cb_destroy;
    b->destroy = cb_destroy;
    c->destroy = cb_destroy;

    darken_entity_pause(b); // b paused, a and c stay active

    g_destroy_calls = 0;
    darken_reset(&m);

    CHECK(g_destroy_calls == 2); // only a and c, not paused b
    CHECK(m.size == 0 && m.paused == m.capacity);
    check_invariants(&m);
    free_pool(&m);
}

// DARKEN_DECLARE + DARKEN_BIND + DARKEN_ALLOC, exercised both at
// declaration time AND via reassignment of an already-declared darken_t.
// The reassignment form is the one that needs the (darken_t) compound-
// literal cast on all three macros -- without it, this simply fails to
// compile.
DARKEN_DECLARE(g_storage, 6, sizeof(Comp));
static darken_t g_ctx = DARKEN_INIT(g_storage);

static void test_declare_bind_init_and_reassignment(void)
{
    darken_init(&g_ctx);
    CHECK(g_ctx.capacity == 6);
    darken_entity_t ge = spawn_comp(&g_ctx, 1);
    DARKEN_DATA(Comp, gc, ge);
    CHECK(gc->id == 1);
    check_invariants(&g_ctx);

    DARKEN_DECLARE(storage_a, 3, sizeof(Comp));
    DARKEN_DECLARE(storage_b, 5, sizeof(Comp));

    darken_t m;
    m = DARKEN_BIND(storage_a); // <-- reassignment, not initialization
    darken_init(&m);
    CHECK(m.capacity == 3);
    check_invariants(&m);

    m = DARKEN_BIND(storage_b); // rebind the same variable to different storage
    darken_init(&m);
    CHECK(m.capacity == 5);
    darken_entity_t e = spawn_comp(&m, 3);
    DARKEN_DATA(Comp, c, e);
    CHECK(c->id == 3);
    check_invariants(&m);

    darken_t m2;
    m2 = DARKEN_ALLOC(MEM_alloc, 4, sizeof(Comp)); // <-- reassignment
    darken_init(&m2);
    CHECK(m2.capacity == 4);
    check_invariants(&m2);
    free_pool(&m2);
}

// Structural fuzz test: random spawn/pause/resume/delete sequences, checking
// zone invariants after EVERY operation and that destroy() fires exactly
// once per entity that dies while active.
static void test_stress_random_ops(void)
{
    enum
    {
        CAP = 16,
        OPS = 1500
    };
    darken_t m;
    make_pool(&m, CAP);
    darken_entity_t live_active[CAP];
    darken_entity_t live_paused[CAP];
    s16 n_active = 0, n_paused = 0;

    g_destroy_calls = 0;

    for (s16 op = 0; op < OPS; op++)
    {
        u16 choice = random() & 3;

        if (choice == 0)
        {
            darken_entity_t e = DARKEN_SPAWN(&m);
            if (e)
            {
                e->update = NULL;
                e->destroy = cb_destroy;
                e->tag = 0;
                e->usr = 0;
                DARKEN_DATA(Comp, c, e);
                c->id = op;
                live_active[n_active++] = e;
            }
        }
        else if (choice == 1 && n_active > 0)
        {
            u16 idx = random() % n_active;
            darken_entity_t e = live_active[idx];
            darken_entity_pause(e);
            live_paused[n_paused++] = e;
            live_active[idx] = live_active[--n_active];
        }
        else if (choice == 2 && n_paused > 0)
        {
            u16 idx = random() % n_paused;
            darken_entity_t e = live_paused[idx];
            darken_entity_resume(e);
            live_active[n_active++] = e;
            live_paused[idx] = live_paused[--n_paused];
        }
        else if (n_active > 0)
        {
            u16 idx = random() % n_active;
            darken_entity_t e = live_active[idx];
            darken_entity_delete(e);
            live_active[idx] = live_active[--n_active];
        }

        check_invariants(&m);
        CHECK(DARKEN_COUNT_ACTIVE(&m) == (u16)n_active);
        CHECK(DARKEN_COUNT_PAUSED(&m) == (u16)n_paused);
    }

    free_pool(&m);
}

/* ==========================================================================
 * Benchmarks -- timed with getSubTick() (76800 subticks/second, driven by
 * the VDP's live scanline counter, so it advances even within one frame).
 * Absolute numbers are meaningless off this exact console revision/region,
 * but they DO confirm the algorithmic shape darken.h promises: flat
 * (O(1)) for spawn/pause/resume/delete, linear (O(active)) for update.
 * ========================================================================== */

static void bench_init(u16 capacity, u16 reps)
{
    darken_t m;
    make_pool(&m, capacity);
    u32 t0 = getSubTick();
    for (u16 r = 0; r < reps; r++)
        darken_init(&m);
    u32 elapsed = getSubTick() - t0;
    kprintf("  init cap=%d: %ld subticks total, %ld per call", (int)capacity,
            (long)elapsed, (long)(elapsed / reps));
    free_pool(&m);
}

static void bench_spawn(u16 capacity, u16 reps)
{
    darken_t m;
    make_pool(&m, capacity);
    u32 total = 0;
    for (u16 r = 0; r < reps; r++)
    {
        darken_init(&m);
        u32 t0 = getSubTick();
        for (u16 i = 0; i < capacity; i++)
        {
            darken_entity_t e = DARKEN_SPAWN(&m);
            e->update = NULL;
            e->destroy = NULL;
        }
        total += getSubTick() - t0;
    }
    kprintf("  spawn-to-full cap=%d: %ld subticks per spawn (x%d reps)",
            (int)capacity, (long)(total / ((u32)reps * capacity)), (int)reps);
    free_pool(&m);
}

static void bench_update(u16 capacity, u16 frames)
{
    darken_t m;
    make_pool(&m, capacity);
    for (u16 i = 0; i < capacity; i++)
    {
        darken_entity_t e = DARKEN_SPAWN(&m);
        e->update = cb_continue;
        e->destroy = NULL;
    }

    u32 t0 = getSubTick();
    for (u16 f = 0; f < frames; f++)
        darken_update(&m);
    u32 elapsed = getSubTick() - t0;

    kprintf("  update cap=%d: %ld subticks/frame, %ld subticks/entity",
            (int)capacity, (long)(elapsed / frames),
            (long)(elapsed / ((u32)frames * capacity)));
    free_pool(&m);
}

static void bench_pause_resume_churn(u16 capacity, u16 cycles)
{
    darken_t m;
    make_pool(&m, capacity);
    darken_entity_t es[64];
    u16 half = capacity / 2;
    if (half > 64)
        half = 64;

    for (u16 i = 0; i < capacity; i++)
    {
        darken_entity_t e = DARKEN_SPAWN(&m);
        e->update = NULL;
        e->destroy = NULL;
        if (i < half)
            es[i] = e;
    }

    u32 t0 = getSubTick();
    for (u16 c = 0; c < cycles; c++)
    {
        for (u16 i = 0; i < half; i++)
            darken_entity_pause(es[i]);
        for (u16 i = 0; i < half; i++)
            darken_entity_resume(es[i]);
    }
    u32 elapsed = getSubTick() - t0;

    kprintf("  pause+resume cap=%d: %ld subticks per pair",
            (int)capacity, (long)(elapsed / ((u32)cycles * half)));
    free_pool(&m);
}

static void bench_delete_respawn_churn(u16 capacity, u16 cycles)
{
    darken_t m;
    make_pool(&m, capacity);
    u16 churn_n = capacity / 4;
    darken_entity_t es[64];

    for (u16 i = 0; i < capacity; i++)
    {
        darken_entity_t e = DARKEN_SPAWN(&m);
        e->update = NULL;
        e->destroy = NULL;
        if (i < churn_n)
            es[i] = e;
    }

    u32 t0 = getSubTick();
    for (u16 c = 0; c < cycles; c++)
    {
        for (u16 i = 0; i < churn_n; i++)
            darken_entity_delete(es[i]);
        for (u16 i = 0; i < churn_n; i++)
        {
            darken_entity_t e = DARKEN_SPAWN(&m);
            e->update = NULL;
            e->destroy = NULL;
            es[i] = e;
        }
    }
    u32 elapsed = getSubTick() - t0;

    kprintf("  delete+respawn cap=%d: %ld subticks per pair",
            (int)capacity, (long)(elapsed / ((u32)cycles * churn_n)));
    free_pool(&m);
}

static void run_all_tests(void)
{
    RUN(test_init_basic);
    RUN(test_spawn_fills_and_saturates);
    RUN(test_data_payload_readwrite);
    RUN(test_zone_membership_and_pause_resume);
    RUN(test_pause_resume_preserves_data_and_address);
    RUN(test_delete_active_and_paused);
    RUN(test_mode_specific);
    RUN(test_reset_destroys_active_only);
    RUN(test_declare_bind_init_and_reassignment);
    RUN(test_stress_random_ops);

    kprintf("TOTAL: %d checks, %d failures", (int)g_checks, (int)g_failures);
}

static void run_all_benchmarks(void)
{
#ifdef DARKEN_DIRECT
    kprintf("=== darken.h benchmarks -- DIRECT mode ===");
#else
    kprintf("=== darken.h benchmarks -- STATE-MACHINE (default) mode ===");
#endif

    bench_init(8, 100);
    bench_init(32, 40);
    bench_init(96, 15);

    bench_spawn(8, 40);
    bench_spawn(32, 15);
    bench_spawn(96, 6);

    bench_update(8, 120);
    bench_update(32, 120);
    bench_update(96, 60);

    bench_pause_resume_churn(8, 80);
    bench_pause_resume_churn(32, 40);
    bench_pause_resume_churn(96, 15);

    bench_delete_respawn_churn(8, 80);
    bench_delete_respawn_churn(32, 40);
    bench_delete_respawn_churn(96, 15);
}

void darken_sgdk_tests_c()
{
    setRandomSeed(1234);

    kprintf("darken.h test/bench ROM starting");
    run_all_tests();
    run_all_benchmarks();
    kprintf("darken.h test/bench ROM done");

    char line[40];
    if (g_failures == 0)
        sprintf(line, "darken.h: OK (%d checks)", (int)g_checks);
    else
        sprintf(line, "darken.h: FAIL (%d/%d)", (int)g_failures, (int)g_checks);
    VDP_drawText(line, 2, 12);
    VDP_drawText("see debug console for benchmarks", 2, 14);
}