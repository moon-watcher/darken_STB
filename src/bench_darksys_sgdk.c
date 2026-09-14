// bench_darksys_sgdk.c
//
// SGDK / Sega Genesis (68000) ROM: microbenchmarks for darksys.h (original) vs
// darksys8.h.
//
// BUILD: this file is meant to be compiled together with darksys_impl.c (unchanged,
// also included in this pack) as TWO SEPARATE translation units in a normal SGDK
// project -- exactly like the host-side benchmark, this keeps darksys.h's functions as
// real, non-inlinable calls from here, which is the realistic case for a header used
// across multiple .c files (see darksys8.h's own header comment on why that matters on
// a 68k target). darksys8.h's functions are `static inline` and defined directly in
// this file, so they DO get inlined here, same as they would anywhere else you include
// the header.
//
// I have NOT been able to compile or run this against real SGDK/m68k-elf-gcc in the
// environment I wrote it in. Every SGDK call (kprintf, MEM_alloc/MEM_free,
// getSubTick/SUBTICKPERSECOND, main's signature) was checked against SGDK's own GitHub
// source rather than guessed, but treat this as "should build", not "verified to
// build" -- and the iteration counts (K/M/F/R below) are rough guesses at what fits in
// a few seconds on a 7.67MHz 68000; tune them once you see real numbers.
//
// OUTPUT: via kprintf(), not the screen -- same emulator debug-console requirement as
// test_darksys_sgdk.c.
//
// SCALE: CAPACITY/PARAMS are kept small on purpose. The Genesis has 64KB of total work
// RAM; MEM_alloc's own size parameter is a u16 (so a single allocation is capped at
// 65535 bytes regardless of how much RAM is actually free) -- both of which make the
// host benchmark's capacity=20000 impossible here even in principle. Systems are
// allocated and freed one at a time (never both live together) to keep peak usage low.

#include <genesis.h>
#include <stdint.h> // intptr_t only -- portable, not host-specific

#include "darksys.h"  // extern declarations only -- implementation lives in darksys_impl.c
#include "darksys8.h" // static inline, self-contained

#define CAPACITY 1000
#define PARAMS 4

#define BENCH(label, N, code)                                                   \
    do                                                                          \
    {                                                                           \
        u32 _t0 = getSubTick();                                                 \
        code;                                                                    \
        u32 _t1 = getSubTick();                                                  \
        u32 _dt = _t1 - _t0;                                                      \
        u32 _ms = (_dt * 1000UL) / SUBTICKPERSECOND;                              \
        kprintf("  %-38s %6u subticks (~%u ms) / %u ops", label, _dt, _ms, (u32)(N)); \
    } while (0)

int bench_darksys_sgdk_main(bool hardReset)
{
    setRandomSeed(1234);

    kprintf("darksys vs darksys8 -- SGDK microbenchmarks (real 68000)");
    kprintf("original in a separate TU (darksys_impl.c), darksys8 inlined here");
    kprintf("capacity=%u, params=%u, 1 subtick = 1/%u s", (u32)CAPACITY, (u32)PARAMS, (u32)SUBTICKPERSECOND);

    /* ---------- Fill: add() from empty to full ---------- */
    kprintf("");
    kprintf("[Fill: add() from empty to full]");
    {
        darksys orig = DARKSYS_POOL_ALLOC(MEM_alloc, CAPACITY, PARAMS);
        u16 i;
        BENCH("original DARKSYS_ADD x N", CAPACITY, {
            for (i = 0; i < CAPACITY; i++)
                DARKSYS_ADD(&orig, (void *)(void *)i, (void *)(void *)i, (void *)(void *)i, (void *)(void *)i);
        });
        MEM_free(orig.pool);
        MEM_free(orig.lookup);
        MEM_free(orig.handles);
    }
    {
        darksys8 d8 = DARKSYS8_POOL_ALLOC(MEM_alloc, CAPACITY, PARAMS);
        darksys8_init(&d8);
        u16 i;
        BENCH("darksys8 DARKSYS8_ADD x N", CAPACITY, {
            for (i = 0; i < CAPACITY; i++)
                DARKSYS8_ADD(&d8, (void *)(void *)i, (void *)(void *)i, (void *)(void *)i, (void *)(void *)i);
        });
        MEM_free(d8.pool);
        MEM_free(d8.lookup);
        MEM_free(d8.handles);
    }

    /* ---------- Reset cost: clear() on a full system, repeated ---------- */
    kprintf("");
    kprintf("[Reset cost: clear() on a full system, R times]");
    {
        const u16 R = 50;
        darksys orig = DARKSYS_POOL_ALLOC(MEM_alloc, CAPACITY, PARAMS);
        u16 i;
        for (i = 0; i < CAPACITY; i++)
            DARKSYS_ADD(&orig, 0, 0, 0, 0);
        BENCH("original darksys_clear() x R (O(1) each)", R, {
            for (i = 0; i < R; i++)
                darksys_clear(&orig);
        });
        MEM_free(orig.pool);
        MEM_free(orig.lookup);
        MEM_free(orig.handles);
    }
    {
        const u16 R = 50;
        darksys8 d8 = DARKSYS8_POOL_ALLOC(MEM_alloc, CAPACITY, PARAMS);
        darksys8_init(&d8);
        u16 i;
        for (i = 0; i < CAPACITY; i++)
            DARKSYS8_ADD(&d8, 0, 0, 0, 0);
        BENCH("darksys8 darksys8_clear() x R (O(capacity) each)", R, {
            for (i = 0; i < R; i++)
                darksys8_clear(&d8);
        });
        MEM_free(d8.pool);
        MEM_free(d8.lookup);
        MEM_free(d8.handles);
    }

    /* ---------- Churn: remove(random live) + add(), steady state ---------- */
    kprintf("");
    kprintf("[Churn: remove(random live) + add(), K iterations]");
    const u16 K = 5000;
    {
        darksys orig = DARKSYS_POOL_ALLOC(MEM_alloc, CAPACITY, PARAMS);
        static int16_t live[CAPACITY];
        u16 i;
        for (i = 0; i < CAPACITY; i++)
            live[i] = DARKSYS_ADD(&orig, 0, 0, 0, 0);

        BENCH("original remove+add x K", K, {
            for (i = 0; i < K; i++)
            {
                u16 idx = random() % CAPACITY;
                darksys_remove(&orig, live[idx]);
                live[idx] = DARKSYS_ADD(&orig, (void *)(void *)i, 0, 0, 0);
            }
        });
        MEM_free(orig.pool);
        MEM_free(orig.lookup);
        MEM_free(orig.handles);
    }
    {
        darksys8 d8 = DARKSYS8_POOL_ALLOC(MEM_alloc, CAPACITY, PARAMS);
        darksys8_init(&d8);
        static darksys8_handle_t live[CAPACITY];
        u16 i;
        for (i = 0; i < CAPACITY; i++)
            live[i] = DARKSYS8_ADD(&d8, 0, 0, 0, 0);

        BENCH("darksys8 remove+add x K", K, {
            for (i = 0; i < K; i++)
            {
                u16 idx = random() % CAPACITY;
                darksys8_remove(&d8, live[idx]);
                live[idx] = DARKSYS8_ADD(&d8, (void *)(void *)i, 0, 0, 0);
            }
        });
        MEM_free(d8.pool);
        MEM_free(d8.lookup);
        MEM_free(d8.handles);
    }

    /* ---------- Lookup: data() on M random valid handles ---------- */
    kprintf("");
    kprintf("[Lookup: data() on M random valid handles]");
    const u16 M = 5000;
    {
        darksys orig = DARKSYS_POOL_ALLOC(MEM_alloc, CAPACITY, PARAMS);
        u16 i;
        for (i = 0; i < CAPACITY; i++)
            DARKSYS_ADD(&orig, 0, 0, 0, 0);
        volatile void *sink = 0;
        BENCH("original darksys_data() x M", M, {
            for (i = 0; i < M; i++)
                sink = darksys_data(&orig, random() % CAPACITY);
        });
        (void)sink;
        MEM_free(orig.pool);
        MEM_free(orig.lookup);
        MEM_free(orig.handles);
    }
    {
        darksys8 d8 = DARKSYS8_POOL_ALLOC(MEM_alloc, CAPACITY, PARAMS);
        darksys8_init(&d8);
        u16 i;
        for (i = 0; i < CAPACITY; i++)
            DARKSYS8_ADD(&d8, 0, 0, 0, 0);
        volatile void *sink = 0;
        BENCH("darksys8 darksys8_data() x M", M, {
            for (i = 0; i < M; i++)
                sink = darksys8_data(&d8, random() % CAPACITY);
        });
        (void)sink;
        MEM_free(d8.pool);
        MEM_free(d8.lookup);
        MEM_free(d8.handles);
    }

    /* ---------- Iterate: FOREACH over a full system, repeated ---------- */
    kprintf("");
    kprintf("[Iterate: FOREACH over a full system, F times]");
    const u16 F = 50;
    {
        darksys orig = DARKSYS_POOL_ALLOC(MEM_alloc, CAPACITY, PARAMS);
        u16 i;
        for (i = 0; i < CAPACITY; i++)
            DARKSYS_ADD(&orig, (void *)1, (void *)1, (void *)1, (void *)1);
        volatile s32 sum = 0;
        void *a;
        void *b;
        void *c;
        void *d;
        BENCH("original DARKSYS_FOREACH x F", (u32)F * CAPACITY, {
            for (i = 0; i < F; i++)
                DARKSYS_FOREACH(&orig, a, b, c, d, { sum += (s32)(void *)a; });
        });
        MEM_free(orig.pool);
        MEM_free(orig.lookup);
        MEM_free(orig.handles);
    }
    {
        darksys8 d8 = DARKSYS8_POOL_ALLOC(MEM_alloc, CAPACITY, PARAMS);
        darksys8_init(&d8);
        u16 i;
        for (i = 0; i < CAPACITY; i++)
            DARKSYS8_ADD(&d8, (void *)1, (void *)1, (void *)1, (void *)1);
        volatile s32 sum = 0;
        void *a;
        void *b;
        void *c;
        void *d;
        BENCH("darksys8 DARKSYS8_FOREACH x F", (u32)F * CAPACITY, {
            for (i = 0; i < F; i++)
                DARKSYS8_FOREACH(&d8, a, b, c, d, { sum += (s32)(void *)a; });
        });
        MEM_free(d8.pool);
        MEM_free(d8.lookup);
        MEM_free(d8.handles);
    }

    kprintf("");
    kprintf("Done.");

    return 0;
}