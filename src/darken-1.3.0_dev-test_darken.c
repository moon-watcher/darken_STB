/* test_darken_1_3.c — tests de darken-1.3.0_dev para SGDK
 *
 * Cubre la API 1.1 sin pause/resume. Todos los tests que quedan son los mismos que en 1.1.
 * El test de "pause/resume" se omite deliberadamente porque la API ya no existe.
 */

#include <genesis.h>
#include <stdint.h>
#include "darken-1.3.0_dev.h"

static u16 g_run = 0, g_pass = 0, g_fail = 0;

#define CHECK(cond)                                          \
    do {                                                     \
        g_run++;                                             \
        if (cond) { g_pass++; }                              \
        else {                                               \
            g_fail++;                                        \
            kprintf("  FAIL L%d: %s", (int)__LINE__, #cond); \
        }                                                    \
    } while (0)

#define RUN(fn)                    \
    do {                           \
        kprintf("== %s ==", #fn);  \
        fn();                      \
    } while (0)

struct counter { s16 value; s16 destroys; };

static void *cb_increment(void *data)
{
    ((struct counter *)data)->value++;
    return DARKEN_CONTINUE;
}

static void *cb_increment_10(void *data)
{
    ((struct counter *)data)->value += 10;
    return DARKEN_CONTINUE;
}

static void *cb_delete_now(void *data)
{
    ((struct counter *)data)->value = -1;
    return DARKEN_DELETE;
}

static void *cb_destroy(void *data)
{
    ((struct counter *)data)->destroys++;
    return DARKEN_CONTINUE;
}

static void *cb_switch_at_3(void *data)
{
    struct counter *c = data;
    c->value++;
    if (c->value >= 3)
        return cb_increment_10;
    return DARKEN_CONTINUE;
}

static void *cb_recover(void *data)
{
    darken_entity_t e = DARKEN_ENTITY(data);
    DARKEN_DATA(struct counter, c, e);
    c->value = 999;
    return DARKEN_CONTINUE;
}

static void test_init_basic(void)
{
    static DARKEN_DECLARE(storage, 8, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    CHECK(m.capacity == 8);
    CHECK(m.size == 0);
    CHECK(m.paused == 8);           /* vestigial, pero coherente con 1.1 */
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 0);
    CHECK(DARKEN_COUNT_FREE(&m) == 8);
    CHECK(DARKEN_COUNT_PAUSED(&m) == 0);
    CHECK(m.stride >= sizeof(struct darken_entity_t) + sizeof(struct counter));
}

static void test_spawn_and_full(void)
{
    static DARKEN_DECLARE(storage, 4, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t e0 = DARKEN_SPAWN(&m);
    CHECK(e0 != NULL);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 1);
    CHECK(DARKEN_COUNT_FREE(&m) == 3);
    CHECK(DARKEN_ENTITY_IN_ACTIVE(e0));
    CHECK(!DARKEN_ENTITY_IN_FREE(e0));

    DARKEN_DATA(struct counter, c, e0);
    c->value = 42;
    CHECK(c->value == 42);

    CHECK(DARKEN_SPAWN(&m) != NULL);
    CHECK(DARKEN_SPAWN(&m) != NULL);
    CHECK(DARKEN_SPAWN(&m) != NULL);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 4);
    CHECK(DARKEN_COUNT_FREE(&m) == 0);
    CHECK(DARKEN_SPAWN(&m) == NULL);
}

static void test_update_continue(void)
{
    static DARKEN_DECLARE(storage, 4, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t e = DARKEN_SPAWN(&m);
    DARKEN_DATA(struct counter, c, e);
    c->value = 0;
    e->update = cb_increment;

    darken_update(&m); CHECK(c->value == 1);
    darken_update(&m); CHECK(c->value == 2);
    darken_update(&m); CHECK(c->value == 3);
}

static void test_self_delete(void)
{
    static DARKEN_DECLARE(storage, 4, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t e = DARKEN_SPAWN(&m);
    DARKEN_DATA(struct counter, c, e);
    c->value = 0; c->destroys = 0;
    e->update = cb_delete_now;
    e->destroy = cb_destroy;

    CHECK(DARKEN_COUNT_ACTIVE(&m) == 1);
    darken_update(&m);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 0);
    CHECK(c->value == -1);
    CHECK(c->destroys == 1);
    CHECK(DARKEN_ENTITY_IN_FREE(e));
}

static void test_new_callback_install(void)
{
    static DARKEN_DECLARE(storage, 4, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t e = DARKEN_SPAWN(&m);
    DARKEN_DATA(struct counter, c, e);
    c->value = 0;
    e->update = cb_switch_at_3;

    darken_update(&m); CHECK(c->value == 1); CHECK(e->update == cb_switch_at_3);
    darken_update(&m); CHECK(c->value == 2);
    darken_update(&m); CHECK(c->value == 3); CHECK(e->update == cb_increment_10);
    darken_update(&m); CHECK(c->value == 13);
    darken_update(&m); CHECK(c->value == 23);
}

static void test_delete_calls_destroy(void)
{
    static DARKEN_DECLARE(storage, 4, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t e = DARKEN_SPAWN(&m);
    DARKEN_DATA(struct counter, c, e);
    c->destroys = 0;
    e->update = cb_increment;
    e->destroy = cb_destroy;

    darken_entity_delete(e);
    CHECK(c->destroys == 1);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 0);
    CHECK(DARKEN_ENTITY_IN_FREE(e));

    darken_entity_delete(e);
    CHECK(c->destroys == 1);   /* segunda llamada: no-op */
}

static void test_reset_destroys_all(void)
{
    static DARKEN_DECLARE(storage, 4, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t e[3];
    for (s16 i = 0; i < 3; i++) {
        e[i] = DARKEN_SPAWN(&m);
        DARKEN_DATA(struct counter, c, e[i]);
        c->value = 0; c->destroys = 0;
        e[i]->update = cb_increment;
        e[i]->destroy = cb_destroy;
    }

    darken_reset(&m);

    for (s16 i = 0; i < 3; i++) {
        DARKEN_DATA(struct counter, c, e[i]);
        CHECK(c->destroys == 1);
    }
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 0);
    CHECK(DARKEN_COUNT_FREE(&m) == 4);
}

static void test_entity_recovery(void)
{
    static DARKEN_DECLARE(storage, 4, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t e = DARKEN_SPAWN(&m);
    DARKEN_DATA(struct counter, c, e);
    c->value = 0;
    e->update = cb_recover;

    darken_update(&m);
    CHECK(c->value == 999);
}

static void test_slot_reuse(void)
{
    static DARKEN_DECLARE(storage, 4, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t a = DARKEN_SPAWN(&m);
    darken_entity_delete(a);

    darken_entity_t b = DARKEN_SPAWN(&m);
    CHECK(b == a);
}

static void test_stress(void)
{
    enum { N = 16 };
    static DARKEN_DECLARE(storage, N, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t e[N];
    for (s16 i = 0; i < N; i++) {
        e[i] = DARKEN_SPAWN(&m);
        CHECK(e[i] != NULL);
        DARKEN_DATA(struct counter, c, e[i]);
        c->value = i; c->destroys = 0;
        e[i]->update = cb_increment;
        e[i]->destroy = cb_destroy;
    }
    CHECK(DARKEN_COUNT_ACTIVE(&m) == N);
    CHECK(DARKEN_SPAWN(&m) == NULL);

    for (s16 i = 0; i < N; i += 2) darken_entity_delete(e[i]);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == N / 2);
    CHECK(DARKEN_COUNT_FREE(&m) == N / 2);

    darken_reset(&m);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 0);
    CHECK(DARKEN_COUNT_FREE(&m) == N);
}

int darken_130_dev_test_darken_main(bool hardReset)
{
    (void)hardReset;

    kprintf("darken-1.3.0_dev test suite");
    kprintf("---------------------------");

    RUN(test_init_basic);
    RUN(test_spawn_and_full);
    RUN(test_update_continue);
    RUN(test_self_delete);
    RUN(test_new_callback_install);
    RUN(test_delete_calls_destroy);
    RUN(test_reset_destroys_all);
    RUN(test_entity_recovery);
    RUN(test_slot_reuse);
    RUN(test_stress);

    kprintf("---------------------------");
    kprintf("run=%d pass=%d fail=%d", (int)g_run, (int)g_pass, (int)g_fail);

    
    return 0;
}