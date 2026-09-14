// test_darksys_sgdk.c
//
// SGDK / Sega Genesis (68000) ROM: correctness test suite for darksys.h (original) and
// darksys8.h (the vsmap-inspired refactor).
//
// BUILD: drop this file, darksys.h and darksys8.h into a normal SGDK project's src/
// folder and build with the regular SGDK toolchain (e.g. `make -f %GDK%/makefile.gen`,
// the SGDK docker image, or the VS Code SGDK task -- whatever your setup normally uses).
// Output is a .bin ROM you run in an emulator or on real hardware via flashcart.
//
// OUTPUT: everything goes through kprintf(), not the screen. To see it, run the ROM in
// an emulator with debug-console support: Gens KMod (Options > Debug > enable "Active
// Development Features") or BlastEm's kdebug support.
//
// I have NOT been able to compile this against the real SGDK library or m68k-elf-gcc in
// the environment I wrote it in (no SGDK checkout / cross-compiler available there) --
// every SGDK call below (kprintf, MEM_alloc/MEM_free, random/setRandomSeed, main's
// signature) was checked against SGDK's own GitHub source (tools.h, memory.h, sys.c)
// rather than guessed, but please treat this as "should build" rather than "verified to
// build", and let me know what the compiler says if it doesn't.
//
// WHY THIS IS SMALLER IN SCOPE THAN THE HOST-SIDE TEST SUITE:
// The Genesis has exactly 64KB of 68k-addressable work RAM, shared by the stack, every
// global, and MEM_alloc's whole heap. darksys's per-slot bookkeeping alone (one uint16_t
// in `lookup[]` + one uint16_t in `handles[]`, before even counting `pool[]`) means you
// physically cannot get anywhere near the 32768-handle mark where the original's
// int16_t truncation bug bites: `lookup[]` alone, at capacity=32768, would already be
// 65536 bytes -- ALL of RAM, with nothing left for `handles[]`, `pool[]`, the stack, or
// anything else. So the large-scale bug reproduction from the host-side test suite isn't
// here: it cannot physically run on real hardware. What IS here: the normal-scale
// correctness/equivalence checks (the ones that actually matter on this platform), plus
// a tiny arithmetic illustration of *why* the fix matters, for the record.

#include <genesis.h>
#include <stdint.h> // void * only -- portable, not host-specific; SGDK ships on real gcc

#define DARKSYS_IMPLEMENTATION
#include "darksys.h"
#include "darksys8.h"

static u16 g_checks = 0;
static u16 g_passed = 0;

#define CHECK(cond, msg)                     \
    do                                        \
    {                                          \
        g_checks++;                            \
        if (cond)                               \
        {                                        \
            g_passed++;                          \
            kprintf("  [OK]   %s", msg);          \
        }                                          \
        else                                        \
        {                                            \
            kprintf("  [FAIL] %s", msg);              \
        }                                              \
    } while (0)

/* ============================================================================
 * Basic smoke tests (capacity=8, params=3)
 * ============================================================================ */

static void test_original_basic(void)
{
    kprintf("");
    kprintf("=== Original darksys: basic smoke test (capacity=8, params=3) ===");
    darksys sys = DARKSYS_POOL_ALLOC(MEM_alloc, 8, 3);

    int16_t a = DARKSYS_ADD(&sys, (void *)1, (void *)2, (void *)3);
    int16_t b = DARKSYS_ADD(&sys, (void *)4, (void *)5, (void *)6);
    int16_t c = DARKSYS_ADD(&sys, (void *)7, (void *)8, (void *)9);
    CHECK(a == 0 && b == 1 && c == 2, "handles assigned in order 0,1,2");

    void **db = darksys_data(&sys, b);
    CHECK(db && db[0] == (void *)4 && db[1] == (void *)5 && db[2] == (void *)6, "darksys_data(b) returns B's group");

    int16_t rr = darksys_remove(&sys, a);
    CHECK(rr >= 0, "remove(a) reports success");

    void **dc = darksys_data(&sys, c);
    CHECK(dc && dc[0] == (void *)7, "c is still valid after removing a (compaction)");
    CHECK(darksys_data(&sys, a) == 0, "a is no longer valid after removal");

    int16_t d = DARKSYS_ADD(&sys, (void *)10, (void *)11, (void *)12);
    CHECK(d == a, "removed handle a is recycled for the next add (LIFO free list)");

    void *p0, *p1, *p2;
    int16_t sum = 0;
    DARKSYS_FOREACH(&sys, p0, p1, p2, { sum += (int16_t)(void *)p0 + (int16_t)(void *)p1 + (int16_t)(void *)p2; });
    CHECK(sum > 0, "DARKSYS_FOREACH visits every live group");

    MEM_free(sys.pool);
    MEM_free(sys.lookup);
    MEM_free(sys.handles);
}

static void test_darksys8_basic(void)
{
    kprintf("");
    kprintf("=== darksys8: basic smoke test (capacity=8, params=3) ===");
    darksys8 sys = DARKSYS8_POOL_ALLOC(MEM_alloc, 8, 3);
    darksys8_init(&sys);

    darksys8_handle_t a = DARKSYS8_ADD(&sys, (void *)1, (void *)2, (void *)3);
    darksys8_handle_t b = DARKSYS8_ADD(&sys, (void *)4, (void *)5, (void *)6);
    darksys8_handle_t c = DARKSYS8_ADD(&sys, (void *)7, (void *)8, (void *)9);
    CHECK(a == 0 && b == 1 && c == 2, "handles assigned in order 0,1,2");

    void **db = darksys8_data(&sys, b);
    CHECK(db && db[0] == (void *)4 && db[1] == (void *)5 && db[2] == (void *)6, "darksys8_data(b) returns B's group");

    int16_t rr = darksys8_remove(&sys, a);
    CHECK(rr == 0, "remove(a) reports success (0)");

    void **dc = darksys8_data(&sys, c);
    CHECK(dc && dc[0] == (void *)7, "c is still valid after removing a (compaction)");
    CHECK(darksys8_data(&sys, a) == 0, "a is no longer valid after removal");

    darksys8_handle_t d = DARKSYS8_ADD(&sys, (void *)10, (void *)11, (void *)12);
    CHECK(d == a, "removed handle a is recycled for the next add (LIFO free list)");

    void *p0, *p1, *p2;
    int16_t sum = 0;
    DARKSYS8_FOREACH(&sys, p0, p1, p2, { sum += (int16_t)(void *)p0 + (int16_t)(void *)p1 + (int16_t)(void *)p2; });
    CHECK(sum > 0, "DARKSYS8_FOREACH visits every live group");

    MEM_free(sys.pool);
    MEM_free(sys.lookup);
    MEM_free(sys.handles);
}

/* ============================================================================
 * Capacity exhaustion + wrong param count (capacity=3, params=3)
 * ============================================================================ */

static void test_original_capacity_and_paramcount(void)
{
    kprintf("");
    kprintf("=== Original darksys: capacity exhaustion + wrong param count ===");
    darksys sys = DARKSYS_POOL_ALLOC(MEM_alloc, 3, 3);

    int16_t a = DARKSYS_ADD(&sys, (void *)1, (void *)1, (void *)1);
    int16_t b = DARKSYS_ADD(&sys, (void *)2, (void *)2, (void *)2);
    int16_t c = DARKSYS_ADD(&sys, (void *)3, (void *)3, (void *)3);
    int16_t full = DARKSYS_ADD(&sys, (void *)4, (void *)4, (void *)4);
    CHECK(a >= 0 && b >= 0 && c >= 0, "first three adds (matching capacity) succeed");
    CHECK(full == -1, "fourth add on a full pool reports -1");

    int16_t bad = DARKSYS_ADD(&sys, (void *)5, (void *)5); // wrong arg count for params=3
    CHECK(bad == -3, "wrong argument count reports -3");

    MEM_free(sys.pool);
    MEM_free(sys.lookup);
    MEM_free(sys.handles);
}

static void test_darksys8_capacity_and_paramcount(void)
{
    kprintf("");
    kprintf("=== darksys8: capacity exhaustion + wrong param count ===");
    darksys8 sys = DARKSYS8_POOL_ALLOC(MEM_alloc, 3, 3);
    darksys8_init(&sys);

    darksys8_handle_t a = DARKSYS8_ADD(&sys, (void *)1, (void *)1, (void *)1);
    darksys8_handle_t b = DARKSYS8_ADD(&sys, (void *)2, (void *)2, (void *)2);
    darksys8_handle_t c = DARKSYS8_ADD(&sys, (void *)3, (void *)3, (void *)3);
    darksys8_handle_t full = DARKSYS8_ADD(&sys, (void *)4, (void *)4, (void *)4);
    CHECK(a != DARKSYS8_INVALID_HANDLE && b != DARKSYS8_INVALID_HANDLE && c != DARKSYS8_INVALID_HANDLE,
          "first three adds (matching capacity) succeed");
    CHECK(full == DARKSYS8_INVALID_HANDLE, "fourth add on a full pool reports DARKSYS8_INVALID_HANDLE");

    darksys8_handle_t bad = DARKSYS8_ADD(&sys, (void *)5, (void *)5); // wrong arg count
    CHECK(bad == DARKSYS8_INVALID_HANDLE, "wrong argument count also reports DARKSYS8_INVALID_HANDLE (unified with 'full')");

    MEM_free(sys.pool);
    MEM_free(sys.lookup);
    MEM_free(sys.handles);
}

/* ============================================================================
 * Equivalence: identical random op stream against both, small scale
 * (capacity chosen to comfortably fit the Genesis's 64KB work RAM)
 * ============================================================================ */

#define EQUIV_CAPACITY 20
#define EQUIV_OPS 1000

static void test_equivalence(void)
{
    kprintf("");
    kprintf("=== Equivalence: original vs darksys8, capacity=%d, %d random ops ===", EQUIV_CAPACITY, EQUIV_OPS);

    darksys orig = DARKSYS_POOL_ALLOC(MEM_alloc, EQUIV_CAPACITY, 1);
    darksys8 d8 = DARKSYS8_POOL_ALLOC(MEM_alloc, EQUIV_CAPACITY, 1);
    darksys8_init(&d8);
    u8 alive[EQUIV_CAPACITY];
    void * value_of[EQUIV_CAPACITY];
    u16 alive_list[EQUIV_CAPACITY];
    u16 alive_count = 0;
    u16 i;

    for (i = 0; i < EQUIV_CAPACITY; i++)
        alive[i] = 0;

    setRandomSeed(42);
    u16 mismatches = 0;
    void * next_value = 1;
    s16 op;

    for (op = 0; op < EQUIV_OPS; op++)
    {
        u8 do_add = (alive_count == 0) || (alive_count < EQUIV_CAPACITY && (random() % 100) < 60);

        if (do_add)
        {
            void * v = next_value++;
            void *vp = (void *)v;

            int16_t h_orig = DARKSYS_ADD(&orig, vp);
            darksys8_handle_t h_d8 = DARKSYS8_ADD(&d8, vp);

            u8 orig_full = (h_orig < 0);
            u8 d8_full = (h_d8 == DARKSYS8_INVALID_HANDLE);

            if (orig_full != d8_full)
            {
                mismatches++;
                kprintf("  MISMATCH op %d: full-state differs (orig=%d, d8=%d)", op, orig_full, d8_full);
            }
            else if (!orig_full)
            {
                if ((u16)h_orig != h_d8)
                {
                    mismatches++;
                    kprintf("  MISMATCH op %d: handle differs (orig=%d, d8=%u)", op, h_orig, (u16)h_d8);
                }
                else
                {
                    u16 h = h_d8;
                    alive[h] = 1;
                    value_of[h] = v;
                    alive_list[alive_count++] = h;
                }
            }
        }
        else
        {
            u16 idx = random() % alive_count;
            u16 h = alive_list[idx];

            int16_t r_orig = darksys_remove(&orig, h);
            int16_t r_d8 = darksys8_remove(&d8, h);

            u8 orig_ok = (r_orig >= 0); // safe at this scale: count never exceeds EQUIV_CAPACITY (200)
            u8 d8_ok = (r_d8 == 0);

            if (!orig_ok || !d8_ok)
            {
                mismatches++;
                kprintf("  MISMATCH op %d: remove(%u) reported failure (orig=%d, d8=%d)", op, h, r_orig, r_d8);
            }

            alive[h] = 0;
            alive_list[idx] = alive_list[--alive_count];
        }
    }

    u16 sweep_mismatches = 0;
    for (i = 0; i < EQUIV_CAPACITY; i++)
    {
        void **do_ = darksys_data(&orig, i);
        void **d8_ = darksys8_data(&d8, i);
        void *ov = do_ ? do_[0] : NULL;
        void *dv = d8_ ? d8_[0] : NULL;
        void *expected = alive[i] ? (void *)value_of[i] : NULL;

        if (ov != expected || dv != expected)
            sweep_mismatches++;
    }

    u8 pool_matches = 1;
    for (i = 0; i < orig.count; i++)
        if (orig.pool[i] != d8.pool[i] || orig.handles[i] != d8.handles[i])
            pool_matches = 0;

    CHECK(orig.count == d8.count, "final live-group counts match");
    CHECK(pool_matches, "dense pool/handles arrays match element-by-element");
    CHECK(mismatches == 0, "no divergence observed during the operation stream");
    CHECK(sweep_mismatches == 0, "final darksys_data()/darksys8_data() sweep matches the shadow model");

    MEM_free(orig.pool);
    MEM_free(orig.lookup);
    MEM_free(orig.handles);
    MEM_free(d8.pool);
    MEM_free(d8.lookup);
    MEM_free(d8.handles);
}

/* ============================================================================
 * Arithmetic illustration of the fixed bug (no large allocation needed/possible)
 * ============================================================================ */

static void demo_int16_truncation_concept(void)
{
    kprintf("");
    kprintf("=== Why the fix matters: int16_t truncation, shown directly ===");
    kprintf("(A real darksys can't grow anywhere near this scale on 64KB RAM --");
    kprintf(" this is a plain arithmetic illustration of the original's return path.)");

    u16 raw_handle = 32768;
    int16_t reported = (int16_t)raw_handle;
    kprintf("  uint16_t handle=%u  ->  as int16_t = %d", raw_handle, reported);
    CHECK(reported < 0, "handle 32768 would be misreported as negative by the original's DARKSYS_ADD");

    u16 raw_count = 40000;
    int16_t reported_count = (int16_t)raw_count;
    kprintf("  uint16_t count=%u  ->  as int16_t = %d", raw_count, reported_count);
    CHECK(reported_count < 0, "a count above 32767 would be misreported as negative by darksys_remove");

    kprintf("  darksys8 never narrows a handle/count to int16_t: DARKSYS8_INVALID_HANDLE");
    kprintf("  (0xFFFF) is the only sentinel, and darksys8_remove() always returns 0 on success.");
}

/* ============================================================================
 * main
 * ============================================================================ */

int test_darksys_sgdk_main(bool hardReset)
{
    kprintf("darksys vs darksys8 -- SGDK correctness test suite");
    kprintf("===================================================");

    test_original_basic();
    test_darksys8_basic();
    test_original_capacity_and_paramcount();
    test_darksys8_capacity_and_paramcount();
    test_equivalence();
    demo_int16_truncation_concept();

    kprintf("");
    kprintf("===================================================");
    kprintf("Checks: %u/%u passed", g_passed, g_checks);

    return 0;
}