#include <genesis.h>

#include "darksys8.h"
#include "darksys9.h"

/* ================================================================
 * CONFIG
 * ================================================================ */

#define BENCH_CAPACITY 256
#define BENCH_PARAMS 3
#define BENCH_ITERATIONS 100

/* ================================================================
 * TEST DATA
 * ================================================================ */

static uint16_t values[BENCH_CAPACITY * BENCH_PARAMS];

/* ================================================================
 * STORAGE
 * ================================================================ */

DARKSYS8_POOL_DECLARE(storage_darksys8, BENCH_CAPACITY, BENCH_PARAMS);
DARKSYS9_POOL_DECLARE(storage_darksys9, BENCH_CAPACITY, BENCH_PARAMS);

/* ================================================================
 * GLOBAL DATA
 * ================================================================ */

static uint16_t handles_darksys8[BENCH_CAPACITY];
static uint16_t handles_darksys9[BENCH_CAPACITY];

static volatile uint32_t bench_sink;

/* ================================================================
 * PREPARE
 * ================================================================ */

static void bench_prepare_darksys8(darksys8 *s)
{
    *s = (darksys8)DARKSYS8_POOL_BIND(storage_darksys8);
    darksys8_clear(s);
}

static void bench_prepare_darksys9(darksys9 *s)
{
    *s = (darksys9)DARKSYS9_POOL_BIND(storage_darksys9);
    darksys9_clear(s);
}

/* ================================================================
 * FILL
 * ================================================================ */

static void bench_fill_darksys8(darksys8 *s)
{
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        uint16_t base = i * BENCH_PARAMS;

        int16_t handle = DARKSYS8_ADD(
            s,
            &values[base],
            &values[base + 1],
            &values[base + 2]
        );

        handles_darksys8[i] = (uint16_t)handle;
    }
}

static void bench_fill_darksys9(darksys9 *s)
{
    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        uint16_t base = i * BENCH_PARAMS;

        int16_t handle = DARKSYS9_ADD(
            s,
            &values[base],
            &values[base + 1],
            &values[base + 2]
        );

        handles_darksys9[i] = (uint16_t)handle;
    }
}

/* ================================================================
 * CORRECTNESS
 * ================================================================ */

static void test_darksys8(void)
{
    darksys8 s;

    bench_prepare_darksys8(&s);

    int16_t a = DARKSYS8_ADD(&s, &values[0], &values[1], &values[2]);
    int16_t b = DARKSYS8_ADD(&s, &values[3], &values[4], &values[5]);
    int16_t c = DARKSYS8_ADD(&s, &values[6], &values[7], &values[8]);

    void **data_a = darksys8_data(&s, (darksys8_handle_t)a);
    void **data_b = darksys8_data(&s, (darksys8_handle_t)b);
    void **data_c = darksys8_data(&s, (darksys8_handle_t)c);

    if (!data_a || !data_b || !data_c)
    {
        kprintf("DARKSYS8 TEST ERROR DATA");
        return;
    }

    if ((uint16_t *)data_a[0] != &values[0])
    {
        kprintf("DARKSYS8 TEST ERROR A");
        return;
    }

    if ((uint16_t *)data_b[1] != &values[4])
    {
        kprintf("DARKSYS8 TEST ERROR B");
        return;
    }

    if ((uint16_t *)data_c[2] != &values[8])
    {
        kprintf("DARKSYS8 TEST ERROR C");
        return;
    }

    darksys8_remove(&s, (darksys8_handle_t)b);

    data_a = darksys8_data(&s, (darksys8_handle_t)a);
    data_b = darksys8_data(&s, (darksys8_handle_t)b);
    data_c = darksys8_data(&s, (darksys8_handle_t)c);

    if (!data_a || data_b || !data_c)
    {
        kprintf("DARKSYS8 TEST ERROR REMOVE");
        return;
    }

    if ((uint16_t *)data_c[2] != &values[8])
    {
        kprintf("DARKSYS8 TEST ERROR MOVE");
        return;
    }

    kprintf("DARKSYS8 TEST OK");
}

static void test_darksys9(void)
{
    darksys9 s;

    bench_prepare_darksys9(&s);

    int16_t a = DARKSYS9_ADD(&s, &values[0], &values[1], &values[2]);
    int16_t b = DARKSYS9_ADD(&s, &values[3], &values[4], &values[5]);
    int16_t c = DARKSYS9_ADD(&s, &values[6], &values[7], &values[8]);

    void **data_a = darksys9_data(&s, (darksys9_handle_t)a);
    void **data_b = darksys9_data(&s, (darksys9_handle_t)b);
    void **data_c = darksys9_data(&s, (darksys9_handle_t)c);

    if (!data_a || !data_b || !data_c)
    {
        kprintf("DARKSYS9 TEST ERROR DATA");
        return;
    }

    if ((uint16_t *)data_a[0] != &values[0])
    {
        kprintf("DARKSYS9 TEST ERROR A");
        return;
    }

    if ((uint16_t *)data_b[1] != &values[4])
    {
        kprintf("DARKSYS9 TEST ERROR B");
        return;
    }

    if ((uint16_t *)data_c[2] != &values[8])
    {
        kprintf("DARKSYS9 TEST ERROR C");
        return;
    }

    darksys9_remove(&s, (darksys9_handle_t)b);

    data_a = darksys9_data(&s, (darksys9_handle_t)a);
    data_b = darksys9_data(&s, (darksys9_handle_t)b);
    data_c = darksys9_data(&s, (darksys9_handle_t)c);

    if (!data_a || data_b || !data_c)
    {
        kprintf("DARKSYS9 TEST ERROR REMOVE");
        return;
    }

    if ((uint16_t *)data_c[2] != &values[8])
    {
        kprintf("DARKSYS9 TEST ERROR MOVE");
        return;
    }

    kprintf("DARKSYS9 TEST OK");
}

/* ================================================================
 * ADD
 * ================================================================ */

static void bench_darksys8_add(void)
{
    darksys8 s;
    bench_prepare_darksys8(&s);

    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        int16_t handle = DARKSYS8_ADD(
            &s,
            &values[i * BENCH_PARAMS],
            &values[i * BENCH_PARAMS + 1],
            &values[i * BENCH_PARAMS + 2]
        );

        sum += (uint16_t)handle;
    }

    BLASTEM_PROFIL_END;

    bench_sink = sum;
}

static void bench_darksys9_add(void)
{
    darksys9 s;
    bench_prepare_darksys9(&s);

    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        int16_t handle = DARKSYS9_ADD(
            &s,
            &values[i * BENCH_PARAMS],
            &values[i * BENCH_PARAMS + 1],
            &values[i * BENCH_PARAMS + 2]
        );

        sum += (uint16_t)handle;
    }

    BLASTEM_PROFIL_END;

    bench_sink = sum;
}

/* ================================================================
 * DATA
 * ================================================================ */

static void bench_darksys8_data(void)
{
    darksys8 s;
    bench_prepare_darksys8(&s);
    bench_fill_darksys8(&s);

    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
        {
            void **data = darksys8_data(&s, handles_darksys8[i]);

            if (data)
                sum += *(uint16_t *)data[0];
        }

    BLASTEM_PROFIL_END;

    bench_sink = sum;
}

static void bench_darksys9_data(void)
{
    darksys9 s;
    bench_prepare_darksys9(&s);
    bench_fill_darksys9(&s);

    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
        {
            void **data = darksys9_data(&s, handles_darksys9[i]);

            if (data)
                sum += *(uint16_t *)data[0];
        }

    BLASTEM_PROFIL_END;

    bench_sink = sum;
}

/* ================================================================
 * ITERATION
 * ================================================================ */

static void bench_darksys8_iteration(void)
{
    darksys8 s;
    bench_prepare_darksys8(&s);
    bench_fill_darksys8(&s);

    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        DARKSYS8_FOREACH(
            &s,
            sum ++;
            kprintf("%d", sum);
        );

    BLASTEM_PROFIL_END;

    bench_sink = sum;
}

static void bench_darksys9_iteration(void)
{
    darksys9 s;
    bench_prepare_darksys9(&s);
    bench_fill_darksys9(&s);

    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t n = 0; n < BENCH_ITERATIONS; n++)
        DARKSYS9_FOREACH(
            &s,
            // sum += *(uint16_t *)_pool[0];
            sum ++;
               // kprintf("%d", sum);
        );

    BLASTEM_PROFIL_END;

    bench_sink = sum;

    kprintf("D9 ITER END");
}

/* ================================================================
 * REMOVE
 * ================================================================ */

static void bench_darksys8_remove(void)
{
    darksys8 s;
    bench_prepare_darksys8(&s);
    bench_fill_darksys8(&s);

    BLASTEM_PROFIL_START;

    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        uint16_t index = (uint16_t)((i * 37u) & (BENCH_CAPACITY - 1));
        darksys8_remove(&s, handles_darksys8[index]);
    }

    BLASTEM_PROFIL_END;
}

static void bench_darksys9_remove(void)
{
    darksys9 s;
    bench_prepare_darksys9(&s);
    bench_fill_darksys9(&s);

    BLASTEM_PROFIL_START;

    for (uint16_t i = 0; i < BENCH_CAPACITY; i++)
    {
        uint16_t index = (uint16_t)((i * 37u) & (BENCH_CAPACITY - 1));
        darksys9_remove(&s, handles_darksys9[index]);
    }

    BLASTEM_PROFIL_END;
}

/* ================================================================
 * REUSE
 * ================================================================ */

static void bench_darksys8_reuse(void)
{
    darksys8 s;
    bench_prepare_darksys8(&s);
    bench_fill_darksys8(&s);

    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t i = 0; i < BENCH_CAPACITY / 2; i++)
    {
        uint16_t index = (uint16_t)((i * 37u) & (BENCH_CAPACITY - 1));
        darksys8_remove(&s, handles_darksys8[index]);
    }

    for (uint16_t i = 0; i < BENCH_CAPACITY / 2; i++)
    {
        int16_t handle = DARKSYS8_ADD(
            &s,
            &values[i * BENCH_PARAMS],
            &values[i * BENCH_PARAMS + 1],
            &values[i * BENCH_PARAMS + 2]
        );

        sum += (uint16_t)handle;
    }

    BLASTEM_PROFIL_END;

    bench_sink = sum;
}

static void bench_darksys9_reuse(void)
{
    darksys9 s;
    bench_prepare_darksys9(&s);
    bench_fill_darksys9(&s);

    uint32_t sum = 0;

    BLASTEM_PROFIL_START;

    for (uint16_t i = 0; i < BENCH_CAPACITY / 2; i++)
    {
        uint16_t index = (uint16_t)((i * 37u) & (BENCH_CAPACITY - 1));
        darksys9_remove(&s, handles_darksys9[index]);
    }

    for (uint16_t i = 0; i < BENCH_CAPACITY / 2; i++)
    {
        int16_t handle = DARKSYS9_ADD(
            &s,
            &values[i * BENCH_PARAMS],
            &values[i * BENCH_PARAMS + 1],
            &values[i * BENCH_PARAMS + 2]
        );

        sum += (uint16_t)handle;
    }

    BLASTEM_PROFIL_END;

    bench_sink = sum;
}

/* ================================================================
 * PUBLIC TEST
 * ================================================================ */

void test_darksys89(void)
{
    kprintf("=== DARKSYS8/9 TEST ===");
    test_darksys8();
    test_darksys9();
}

/* ================================================================
 * PUBLIC BENCHMARK
 * ================================================================ */

void bench_darksys89_compare(void)
{
    kprintf("=== DARKSYS8 VS DARKSYS9 ===");
    kprintf("capacity=%u params=%u iterations=%u",
        BENCH_CAPACITY,
        BENCH_PARAMS,
        BENCH_ITERATIONS);

    kprintf("> ADD ---");
    bench_darksys8_add();
    bench_darksys9_add();

    kprintf("> DATA ---");
    bench_darksys8_data();
    bench_darksys9_data();

    kprintf("> ITER ---");
    bench_darksys8_iteration();
    bench_darksys9_iteration();

    kprintf("> REMOVE ---");
    bench_darksys8_remove();
    bench_darksys9_remove();

    kprintf("> REUSE ---");
    bench_darksys8_reuse();
    bench_darksys9_reuse();

    kprintf("sink=%lu", bench_sink);
}
