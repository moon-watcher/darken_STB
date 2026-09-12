/* ============================================================================
 * bench.c -- tests + benchmarks para dsmap0.h (v1) y dsmap3.h (v2)
 *
 * Requiere dsmap0.h y dsmap3.h en el mismo directorio.
 *
 * Compilar dos veces para cubrir los dos modos de dsmap3:
 *     (normal)             -> dsmap3 densa
 *     (-DDSMAP3_STABLE)    -> dsmap3 estable
 *
 * Salida con kprintf (KDebug activo).
 * ==========================================================================*/

#include <stdint.h>
#include <genesis.h>

#include "../dsmap0.h"
#define DSMAP3_STABLE
#include "../dsmap3.h"


/* ---------------------------------------------------------------------------
 * Tipos de elemento y almacenamiento estático
 * -------------------------------------------------------------------------*/

typedef struct { uint32_t v[1];  } elem4_t;
typedef struct { uint32_t v[2];  } elem8_t;
typedef struct { uint32_t v[16]; } elem64_t;

#define CAP 128


/* ---------------------- v1: dsmap ---------------------- */

static elem64_t g_v1_pool   [CAP];
static uint16_t g_v1_lookup [CAP];
static uint16_t g_v1_handles[CAP];
static dsmap0_t  g_v1_map;

static void v1_bind(uint16_t size)
{
    g_v1_map.pool      = (char *)g_v1_pool;
    g_v1_map.lookup    = g_v1_lookup;
    g_v1_map.handles   = g_v1_handles;
    g_v1_map.capacity  = CAP;
    g_v1_map.size      = size;
    dsmap0_init(&g_v1_map);
}


/* ---------------------- v3: dsmap3 ---------------------- */

static elem64_t g_v3_pool   [CAP];
static uint16_t g_v3_lookup [CAP];
static uint16_t g_v3_handles[CAP];
static dsmap3_t g_v3_map;

static void v3_bind(uint16_t size)
{
    g_v3_map.pool      = (char *)g_v3_pool;
    g_v3_map.lookup    = g_v3_lookup;
    g_v3_map.handles   = g_v3_handles;
    g_v3_map.capacity  = CAP;
    g_v3_map.size      = size;
    dsmap3_init(&g_v3_map);
}


/* ---------------------------------------------------------------------------
 * Utilidades
 * -------------------------------------------------------------------------*/

static uint16_t g_pass, g_fail;

static void check(uint16_t cond, const char *msg)
{
    if (cond) g_pass++;
    else { g_fail++; kprintf("FAIL: %s", msg); }
}

static void print_res(const char *tag, uint32_t ns)
{
    kprintf("%s: %d ns/op", tag, (int)ns);
}


/* --- sumatorios usados por el bench de iteración --- */

static uint32_t v1_itersum(dsmap0_t *m)
{
    uint32_t s = 0;
    char *p = m->pool;
    for (uint16_t i = 0; i < m->count; i++)
        s += *(uint32_t *)(p + (uint32_t)i * m->size);
    return s;
}

static uint32_t v3_itersum(dsmap3_t *m)
{
    uint32_t s = 0;
    char *p = m->pool;
#ifdef DSMAP3_STABLE
    for (uint16_t i = 0; i < m->count; i++) {
        uint16_t h = m->handles[i];
        s += *(uint32_t *)(p + (uint32_t)h * m->size);
    }
#else
    for (uint16_t i = 0; i < m->count; i++)
        s += *(uint32_t *)(p + (uint32_t)i * m->size);
#endif
    return s;
}


/* ---------------------------------------------------------------------------
 * Tests paramétricos
 * -------------------------------------------------------------------------*/

#define DO_TESTS(PFX, MAP, HND, INV, BIND, ALLOC, DATA, VALID, REMOVE)       \
do {                                                                         \
    kprintf("--- " #PFX " tests ---");                                       \
    MAP *m = &g_##PFX##_map;                                                 \
                                                                             \
    BIND(sizeof(elem8_t));                                                   \
    check(m->count == 0, "init count");                                      \
    check(m->capacity == CAP, "init cap");                                   \
    check(m->size == sizeof(elem8_t), "init size");                          \
                                                                             \
    for (uint16_t i = 0; i < CAP; i++) {                                     \
        HND h = ALLOC(m);                                                    \
        check(h == i, "alloc seq");                                          \
        elem8_t *e = (elem8_t *)DATA(m, h);                                  \
        check(e != 0, "data nonnull");                                       \
        if (e) { e->v[0] = 0xAA000000u | i; e->v[1] = 0xBEEFu; }             \
    }                                                                        \
    check(ALLOC(m) == INV, "full alloc INV");                                \
                                                                             \
    for (uint16_t i = 0; i < CAP; i++) {                                     \
        elem8_t *e = (elem8_t *)DATA(m, i);                                  \
        check(e && e->v[0] == (0xAA000000u | i), "data v0");                 \
        check(e && e->v[1] == 0xBEEFu, "data v1");                           \
    }                                                                        \
                                                                             \
    for (uint16_t i = 0; i < CAP; i += 2) REMOVE(m, i);                      \
    check(m->count == CAP / 2, "count half");                                \
    for (uint16_t i = 1; i < CAP; i += 2) {                                  \
        elem8_t *e = (elem8_t *)DATA(m, i);                                  \
        check(e && e->v[0] == (0xAA000000u | i), "odd intact");              \
    }                                                                        \
                                                                             \
    for (uint16_t i = 1; i < CAP; i += 2) REMOVE(m, i);                      \
    check(m->count == 0, "count 0");                                         \
                                                                             \
    check(!VALID(m, INV), "INV not valid");                                  \
    check(DATA(m, INV) == 0, "DATA INV null");                               \
    check(REMOVE(m, INV) == 0, "REMOVE INV null");                           \
                                                                             \
    BIND(sizeof(elem8_t));                                                   \
    HND h0 = ALLOC(m);                                                       \
    HND h1 = ALLOC(m);                                                       \
    HND h2 = ALLOC(m);                                                       \
    (void)h0; (void)h2;                                                      \
    REMOVE(m, h1);                                                           \
    HND hr = ALLOC(m);                                                       \
    check(hr == h1, "LIFO reuse");                                           \
} while (0)


/* ---------------------------------------------------------------------------
 * Benchmarks paramétricos
 * -------------------------------------------------------------------------*/

#define DO_BENCHES(PFX, MAP, HND, BIND, ALLOC, DATA, VALID, REMOVE, ITERSUM) \
do {                                                                         \
    MAP *m = &g_##PFX##_map;                                                 \
    uint32_t tprev, tnow, accum, ns;                                         \
                                                                             \
    /* ---- alloc 8B ---- */                                                 \
    BIND(sizeof(elem8_t));                                                   \
    tprev = getTick(); accum = 0;                                            \
    for (uint16_t it = 0; it < 200; it++) {                                  \
        for (uint16_t j = 0; j < CAP; j++) ALLOC(m);                         \
        tnow = getTick(); accum += tnow - tprev;                             \
        for (uint16_t j = 0; j < CAP; j++) REMOVE(m, j);                     \
        tprev = getTick();                                                   \
    }                                                                        \
    ns = accum * 1000000uL / (200uL * CAP);                                  \
    print_res(#PFX " alloc 8B", ns);                                         \
                                                                             \
    /* ---- remove 8B reverse ---- */                                        \
    BIND(sizeof(elem8_t));                                                   \
    for (uint16_t j = 0; j < CAP; j++) ALLOC(m);                             \
    tprev = getTick(); accum = 0;                                            \
    for (uint16_t it = 0; it < 200; it++) {                                  \
        for (uint16_t j = CAP; j-- > 0;) REMOVE(m, j);                       \
        tnow = getTick(); accum += tnow - tprev;                             \
        for (uint16_t j = 0; j < CAP; j++) ALLOC(m);                         \
        tprev = getTick();                                                   \
    }                                                                        \
    ns = accum * 1000000uL / (200uL * CAP);                                  \
    print_res(#PFX " rm 8B rev", ns);                                        \
                                                                             \
    /* ---- remove 8B forward ---- */                                        \
    BIND(sizeof(elem8_t));                                                   \
    for (uint16_t j = 0; j < CAP; j++) ALLOC(m);                             \
    tprev = getTick(); accum = 0;                                            \
    for (uint16_t it = 0; it < 200; it++) {                                  \
        for (uint16_t j = 0; j < CAP; j++) REMOVE(m, j);                     \
        tnow = getTick(); accum += tnow - tprev;                             \
        for (uint16_t j = 0; j < CAP; j++) ALLOC(m);                         \
        tprev = getTick();                                                   \
    }                                                                        \
    ns = accum * 1000000uL / (200uL * CAP);                                  \
    print_res(#PFX " rm 8B fwd", ns);                                        \
                                                                             \
    /* ---- remove 64B reverse ---- */                                       \
    BIND(sizeof(elem64_t));                                                  \
    for (uint16_t j = 0; j < CAP; j++) ALLOC(m);                             \
    tprev = getTick(); accum = 0;                                            \
    for (uint16_t it = 0; it < 100; it++) {                                  \
        for (uint16_t j = CAP; j-- > 0;) REMOVE(m, j);                       \
        tnow = getTick(); accum += tnow - tprev;                             \
        for (uint16_t j = 0; j < CAP; j++) ALLOC(m);                         \
        tprev = getTick();                                                   \
    }                                                                        \
    ns = accum * 1000000uL / (100uL * CAP);                                  \
    print_res(#PFX " rm 64B rev", ns);                                       \
                                                                             \
    /* ---- remove 64B forward ---- */                                       \
    BIND(sizeof(elem64_t));                                                  \
    for (uint16_t j = 0; j < CAP; j++) ALLOC(m);                             \
    tprev = getTick(); accum = 0;                                            \
    for (uint16_t it = 0; it < 100; it++) {                                  \
        for (uint16_t j = 0; j < CAP; j++) REMOVE(m, j);                     \
        tnow = getTick(); accum += tnow - tprev;                             \
        for (uint16_t j = 0; j < CAP; j++) ALLOC(m);                         \
        tprev = getTick();                                                   \
    }                                                                        \
    ns = accum * 1000000uL / (100uL * CAP);                                  \
    print_res(#PFX " rm 64B fwd", ns);                                       \
                                                                             \
    /* ---- iterate sum ---- */                                              \
    BIND(sizeof(elem4_t));                                                   \
    for (uint16_t j = 0; j < CAP; j++) {                                     \
        HND h = ALLOC(m);                                                    \
        elem4_t *e = (elem4_t *)DATA(m, h);                                  \
        e->v[0] = j;                                                         \
    }                                                                        \
    {                                                                        \
        volatile uint32_t sink = 0;                                          \
        uint32_t acc = 0;                                                    \
        tprev = getTick();                                                   \
        for (uint16_t it = 0; it < 500; it++) acc += ITERSUM(m);             \
        tnow = getTick();                                                    \
        sink = acc; (void)sink;                                              \
        ns = (tnow - tprev) * 1000000uL / (500uL * CAP);                     \
        print_res(#PFX " iter", ns);                                         \
    }                                                                        \
                                                                             \
    /* ---- data() access ---- */                                            \
    BIND(sizeof(elem8_t));                                                   \
    for (uint16_t j = 0; j < CAP; j++) {                                     \
        HND h = ALLOC(m);                                                    \
        elem4_t *e = (elem4_t *)DATA(m, h);                                  \
        e->v[0] = 1;                                                         \
    }                                                                        \
    {                                                                        \
        volatile uint32_t sink = 0;                                          \
        uint32_t acc = 0;                                                    \
        tprev = getTick();                                                   \
        for (uint16_t it = 0; it < 2000; it++)                               \
            for (uint16_t j = 0; j < CAP; j++)                               \
                acc += *(uint32_t *)DATA(m, j);                              \
        tnow = getTick();                                                    \
        sink = acc; (void)sink;                                              \
        ns = (tnow - tprev) * 1000000uL / (2000uL * CAP);                    \
        print_res(#PFX " data", ns);                                         \
    }                                                                        \
                                                                             \
    /* ---- valid() check ---- */                                            \
    BIND(sizeof(elem8_t));                                                   \
    for (uint16_t j = 0; j < CAP; j++) ALLOC(m);                             \
    {                                                                        \
        volatile uint32_t sink = 0;                                          \
        uint32_t acc = 0;                                                    \
        tprev = getTick();                                                   \
        for (uint16_t it = 0; it < 2000; it++)                               \
            for (uint16_t j = 0; j < CAP; j++)                               \
                acc += VALID(m, j);                                          \
        tnow = getTick();                                                    \
        sink = acc; (void)sink;                                              \
        ns = (tnow - tprev) * 1000000uL / (2000uL * CAP);                    \
        print_res(#PFX " valid", ns);                                        \
    }                                                                        \
} while (0)


/* ---------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/

int dsmap0_bench_deepseek_main(bool hardReset)
{
    (void)hardReset;

    kprintf("dsmap / dsmap3 tests & benchmarks");
    kprintf("CAP = %d", CAP);
#ifdef DSMAP3_STABLE
    kprintf("dsmap3 mode = STABLE");
#else
    kprintf("dsmap3 mode = dense");
#endif

    /* ---------------- tests ---------------- */
    kprintf("=== TESTS ===");
    g_pass = 0; g_fail = 0;

    DO_TESTS(v1, dsmap0_t, dsmap0_handle_t, DSMAP0_INVALID_HANDLE,
             v1_bind, dsmap0_alloc, dsmap0_data, dsmap0_valid, dsmap0_remove);

    DO_TESTS(v3, dsmap3_t, dsmap3_handle_t, DSMAP3_INVALID_HANDLE,
             v3_bind, dsmap3_alloc, dsmap3_data, dsmap3_valid, dsmap3_remove);

    kprintf("total: %d pass %d fail", g_pass, g_fail);

    /* ---------------- benchmarks ---------------- */
    kprintf("=== BENCHMARKS ===");

    DO_BENCHES(v1, dsmap0_t, dsmap0_handle_t,
               v1_bind, dsmap0_alloc, dsmap0_data, dsmap0_valid,
               dsmap0_remove, v1_itersum);

    DO_BENCHES(v3, dsmap3_t, dsmap3_handle_t,
               v3_bind, dsmap3_alloc, dsmap3_data, dsmap3_valid,
               dsmap3_remove, v3_itersum);

    kprintf("done");

    while (1) SYS_doVBlankProcess();
    return 0;
}
