#include <stdint.h>
#include "darken-1.1.0_dev.h"

#define DARKEN_TEST_CAPACITY 32

struct darken_test_data
{
    int value;
    int ticks;
    uint16_t destroyed;
};

DARKEN_DECLARE(darken_test_storage, DARKEN_TEST_CAPACITY, sizeof(struct darken_test_data));
static darken_t darken_test;

static uint16_t darken_test_errors;
static volatile uint32_t darken_bench_sink;

#define DARKEN_TEST_ASSERT(X)            \
    do                                   \
    {                                    \
        if (!(X))                        \
        {                                \
            darken_test_errors++;        \
            kprintf("FAIL: %s\n", #X);  \
        }                                \
    } while (0)

static void darken_test_reset(void)
{
    darken_test = DARKEN_BIND(darken_test_storage);
    darken_init(&darken_test);
    darken_test_errors = 0;
}

#ifndef DARKEN_DIRECT

/*
 * En modo state-machine la firma del callback es:
 *
 *     void *callback(void *data)
 *
 * El valor devuelto es un "estado": o bien uno de los centinelas DARKEN_*,
 * o bien un nuevo puntero a callback. darken_state_t es el tipo del callback
 * almacenado (void *(*)()) — devolver void * mantiene al callback compatible
 * con ese campo.
 */

static void *darken_test_continue(void *data)
{
    struct darken_test_data *d = data;
    d->ticks++;

    return DARKEN_CONTINUE;
}

static void *darken_test_pause(void *data)
{
    struct darken_test_data *d = data;
    d->ticks++;

    return DARKEN_PAUSE;
}

static void *darken_test_delete(void *data)
{
    struct darken_test_data *d = data;
    d->ticks++;

    return DARKEN_DELETE;
}

static void *darken_test_switch(void *data)
{
    struct darken_test_data *d = data;
    d->ticks++;

    return darken_test_continue;
}

/* Renombrado: antes colisionaba con la función de test darken_test_destroy(void). */
static void darken_test_on_destroy(void *data)
{
    struct darken_test_data *d = data;
    d->destroyed++;
}

static darken_entity_t darken_test_spawn(int value, darken_state_t update)
{
    darken_entity_t entity = DARKEN_SPAWN(&darken_test);

    DARKEN_TEST_ASSERT(entity != 0);

    if (!entity)
        return 0;

    DARKEN_DATA(struct darken_test_data, data, entity);

    data->value = value;
    data->ticks = 0;
    data->destroyed = 0;

    entity->update = update;
    entity->destroy = darken_test_on_destroy;
    entity->usr = value;
    entity->tag = value;

    return entity;
}

static void darken_test_spawn_delete_reuse(void)
{
    darken_test_reset();

    darken_entity_t a = darken_test_spawn(1, darken_test_continue);
    darken_entity_t b = darken_test_spawn(2, darken_test_continue);

    darken_entity_delete(b);

    DARKEN_TEST_ASSERT(darken_test.size == 1);
    DARKEN_TEST_ASSERT(DARKEN_COUNT_FREE(&darken_test) == 31);
    DARKEN_TEST_ASSERT(DARKEN_ENTITY_IN_FREE(b));

    darken_entity_t c = DARKEN_SPAWN(&darken_test);

    DARKEN_TEST_ASSERT(c == b);

    (void)a;
}

static void darken_test_update_continue(void)
{
    darken_test_reset();

    darken_entity_t a = darken_test_spawn(1, darken_test_continue);
    darken_entity_t b = darken_test_spawn(2, darken_test_continue);
    darken_entity_t c = darken_test_spawn(3, darken_test_continue);

    darken_update(&darken_test);

    DARKEN_DATA(struct darken_test_data, da, a);
    DARKEN_DATA(struct darken_test_data, db, b);
    DARKEN_DATA(struct darken_test_data, dc, c);

    DARKEN_TEST_ASSERT(da->ticks == 1);
    DARKEN_TEST_ASSERT(db->ticks == 1);
    DARKEN_TEST_ASSERT(dc->ticks == 1);
}

static void darken_test_state_switch(void)
{
    darken_test_reset();

    darken_entity_t entity = darken_test_spawn(1, darken_test_switch);

    darken_update(&darken_test);

    /* entity->update es darken_state_t; darken_test_continue es void *(*)(void *).
       Hay que castear para comparar punteros a función del mismo tipo. */
    DARKEN_TEST_ASSERT(entity->update == (darken_state_t)darken_test_continue);

    DARKEN_DATA(struct darken_test_data, data, entity);

    DARKEN_TEST_ASSERT(data->ticks == 1);

    darken_update(&darken_test);

    DARKEN_TEST_ASSERT(data->ticks == 2);
}

static void darken_test_delete_during_update(void)
{
    darken_test_reset();

    darken_entity_t a = darken_test_spawn(1, darken_test_continue);
    darken_entity_t b = darken_test_spawn(2, darken_test_delete);
    darken_entity_t c = darken_test_spawn(3, darken_test_continue);

    darken_update(&darken_test);

    DARKEN_TEST_ASSERT(darken_test.size == 2);
    DARKEN_TEST_ASSERT(DARKEN_ENTITY_IN_FREE(b));

    /* destroyed vive en el payload, no en la entidad */
    DARKEN_DATA(struct darken_test_data, db, b);
    DARKEN_TEST_ASSERT(db->destroyed == 1);

    DARKEN_DATA(struct darken_test_data, da, a);
    DARKEN_DATA(struct darken_test_data, dc, c);

    DARKEN_TEST_ASSERT(da->ticks == 1);
    DARKEN_TEST_ASSERT(dc->ticks == 1);
}

static void darken_test_pause_resume(void)
{
    darken_test_reset();

    darken_entity_t a = darken_test_spawn(1, darken_test_pause);
    darken_entity_t b = darken_test_spawn(2, darken_test_continue);

    darken_update(&darken_test);

    DARKEN_TEST_ASSERT(DARKEN_ENTITY_IN_PAUSED(a));
    DARKEN_TEST_ASSERT(DARKEN_ENTITY_IN_ACTIVE(b));
    DARKEN_TEST_ASSERT(darken_test.size == 1);
    DARKEN_TEST_ASSERT(darken_test.paused == 31);

    darken_entity_resume(a);

    DARKEN_TEST_ASSERT(DARKEN_ENTITY_IN_ACTIVE(a));
    DARKEN_TEST_ASSERT(DARKEN_ENTITY_IN_ACTIVE(b));
    DARKEN_TEST_ASSERT(darken_test.size == 2);
    DARKEN_TEST_ASSERT(darken_test.paused == 32);
}

static void darken_test_data_stability(void)
{
    darken_test_reset();

    darken_entity_t a = darken_test_spawn(1, darken_test_continue);
    darken_entity_t b = darken_test_spawn(2, darken_test_continue);
    darken_entity_t c = darken_test_spawn(3, darken_test_continue);

    uint8_t *data_a = a->data;
    uint8_t *data_b = b->data;
    uint8_t *data_c = c->data;

    darken_entity_pause(b);

    DARKEN_TEST_ASSERT(a->data == data_a);
    DARKEN_TEST_ASSERT(b->data == data_b);
    DARKEN_TEST_ASSERT(c->data == data_c);

    darken_entity_resume(b);

    DARKEN_TEST_ASSERT(a->data == data_a);
    DARKEN_TEST_ASSERT(b->data == data_b);
    DARKEN_TEST_ASSERT(c->data == data_c);
}

static void darken_test_destroy_semantics(void)   /* antes: darken_test_destroy */
{
    darken_test_reset();

    darken_entity_t active = darken_test_spawn(1, darken_test_continue);
    darken_entity_t paused = darken_test_spawn(2, darken_test_continue);

    darken_entity_pause(paused);

    darken_entity_delete(active);

    /* destroyed vive en el payload, no en la entidad */
    DARKEN_DATA(struct darken_test_data, d_active, active);
    DARKEN_DATA(struct darken_test_data, d_paused, paused);

    DARKEN_TEST_ASSERT(d_active->destroyed == 1);
    DARKEN_TEST_ASSERT(d_paused->destroyed == 0);

    darken_entity_delete(paused);

    DARKEN_TEST_ASSERT(d_paused->destroyed == 0);
}

static void darken_test_reset_semantics(void)
{
    darken_test_reset();

    darken_entity_t a = darken_test_spawn(1, darken_test_continue);
    darken_entity_t b = darken_test_spawn(2, darken_test_continue);
    darken_entity_t c = darken_test_spawn(3, darken_test_continue);

    darken_entity_pause(c);

    darken_reset(&darken_test);

    DARKEN_DATA(struct darken_test_data, da, a);
    DARKEN_DATA(struct darken_test_data, db, b);
    DARKEN_DATA(struct darken_test_data, dc, c);

    DARKEN_TEST_ASSERT(da->destroyed == 1);
    DARKEN_TEST_ASSERT(db->destroyed == 1);
    DARKEN_TEST_ASSERT(dc->destroyed == 0);

    DARKEN_TEST_ASSERT(darken_test.size == 0);
    DARKEN_TEST_ASSERT(darken_test.paused == DARKEN_TEST_CAPACITY);
}

static void darken_test_entity_recovery(void)
{
    darken_test_reset();

    darken_entity_t entity = darken_test_spawn(123, darken_test_continue);

    DARKEN_DATA(struct darken_test_data, data, entity);

    DARKEN_TEST_ASSERT(DARKEN_ENTITY(data) == entity);
}

static void darken_test_foreach(void)
{
    darken_test_reset();

    darken_entity_t a = darken_test_spawn(1, darken_test_continue);
    darken_entity_t b = darken_test_spawn(2, darken_test_continue);
    darken_entity_t c = darken_test_spawn(3, darken_test_continue);

    uint16_t seen = 0;

    DARKEN_FOREACH(&darken_test, {
        seen++;
        _entity->usr += 10;
    });

    DARKEN_TEST_ASSERT(seen == 3);
    DARKEN_TEST_ASSERT(a->usr == 11);
    DARKEN_TEST_ASSERT(b->usr == 12);
    DARKEN_TEST_ASSERT(c->usr == 13);
}

#endif

void darken_run_tests(void)
{
#ifndef DARKEN_DIRECT
    darken_test_spawn_delete_reuse();
    darken_test_update_continue();
    darken_test_state_switch();
    darken_test_delete_during_update();
    darken_test_pause_resume();
    darken_test_data_stability();
    darken_test_destroy_semantics();
    darken_test_reset_semantics();
    darken_test_entity_recovery();
    darken_test_foreach();
#endif

    if (darken_test_errors)
        kprintf("DARKEN TESTS FAILED: %u\n", darken_test_errors);
    else
        kprintf("DARKEN TESTS OK\n");
}

/* ============================================================================
 * BENCHMARKS
 * ============================================================================ */

#ifndef DARKEN_DIRECT

static void *darken_bench_update_state(void *data)   /* antes devolvía darken_state_t */
{
    struct darken_test_data *d = data;

    d->value++;
    darken_bench_sink += d->value;

    return DARKEN_CONTINUE;
}

static void darken_bench_setup(void)
{
    darken_test = DARKEN_BIND(darken_test_storage);
    darken_init(&darken_test);

    uint16_t i = DARKEN_TEST_CAPACITY;

    while (i--)
    {
        darken_entity_t entity = DARKEN_SPAWN(&darken_test);

        DARKEN_DATA(struct darken_test_data, data, entity);

        data->value = i;
        data->ticks = 0;
        data->destroyed = 0;

        entity->update = darken_bench_update_state;
        entity->destroy = 0;
    }
}

uint32_t darken_bench_init(uint16_t iterations)
{
    uint32_t sink = 0;

    while (iterations--)
    {
        darken_test = DARKEN_BIND(darken_test_storage);
        darken_init(&darken_test);

        sink += darken_test.size;
        sink += darken_test.paused;
    }

    return sink;
}

uint32_t darken_bench_spawn(uint16_t iterations)
{
    uint32_t sink = 0;

    while (iterations--)
    {
        darken_test = DARKEN_BIND(darken_test_storage);
        darken_init(&darken_test);

        uint16_t i = DARKEN_TEST_CAPACITY;

        while (i--)
        {
            darken_entity_t entity = DARKEN_SPAWN(&darken_test);
            sink += entity->slot;
        }

        sink += darken_test.size;
    }

    return sink;
}

uint32_t darken_bench_update(uint16_t iterations)
{
    darken_bench_sink = 0;

    darken_bench_setup();

    while (iterations--)
        darken_update(&darken_test);

    return darken_bench_sink;
}

uint32_t darken_bench_foreach(uint16_t iterations)
{
    darken_bench_sink = 0;

    darken_bench_setup();

    while (iterations--)
    {
        DARKEN_FOREACH(&darken_test, {
            DARKEN_DATA(struct darken_test_data, data, _entity);

            data->value++;
            darken_bench_sink += data->value;
        });
    }

    return darken_bench_sink;
}

uint32_t darken_bench_pause_resume(uint16_t iterations)
{
    uint32_t sink = 0;

    while (iterations--)
    {
        darken_test = DARKEN_BIND(darken_test_storage);
        darken_init(&darken_test);

        darken_entity_t entity = DARKEN_SPAWN(&darken_test);

        sink += entity->slot;

        darken_entity_pause(entity);
        sink += entity->slot;

        darken_entity_resume(entity);
        sink += entity->slot;
    }

    return sink;
}

uint32_t darken_bench_delete(uint16_t iterations)
{
    uint32_t sink = 0;

    while (iterations--)
    {
        darken_test = DARKEN_BIND(darken_test_storage);
        darken_init(&darken_test);

        uint16_t i = DARKEN_TEST_CAPACITY;

        while (i--)
            darken_test_spawn(i, darken_bench_update_state);

        while (darken_test.size)
        {
            darken_entity_t entity = darken_test.pool[darken_test.size - 1];

            darken_entity_delete(entity);

            sink += darken_test.size;
        }
    }

    return sink;
}

#endif

/*
 * Benchmark:
 *
 *     uint32_t r;
 *
 *     r = darken_bench_init(1000);
 *     r = darken_bench_spawn(1000);
 *     r = darken_bench_update(1000);
 *     r = darken_bench_foreach(1000);
 *     r = darken_bench_pause_resume(1000);
 *     r = darken_bench_delete(1000);
 *
 * Measure each call with KDEBUG TIMER.
 */