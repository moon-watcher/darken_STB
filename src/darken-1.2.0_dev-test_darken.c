/* test_darken.c — tests de darken.h 1.2.0_dev para SGDK
 *
 * Requiere: SGDK (genesis.h), m68k-elf-gcc (GNU C).
 * Salida por kprintf() (debug serial).
 *
 * Convenciones SGDK:
 *   - int main(bool hardReset)
 *   - kprintf() para debug
 *   - tipos s16/u16/s32/u32/bool de genesis.h
 *   - almacenamiento estático; sin malloc/free
 */

#include <genesis.h>
#include <stdint.h>

#include "darken-1.2.0_dev.h"

/* ------------------------------------------------------------------ */
/* Mini framework                                                     */
/* ------------------------------------------------------------------ */

static u16 g_run  = 0;
static u16 g_pass = 0;
static u16 g_fail = 0;

#define CHECK(cond)                                            \
    do {                                                       \
        g_run++;                                               \
        if (cond) { g_pass++; }                                \
        else {                                                 \
            g_fail++;                                          \
            kprintf("  FAIL L%d: %s", (int)__LINE__, #cond);   \
        }                                                      \
    } while (0)

#define RUN(fn)                        \
    do {                               \
        kprintf("== %s ==", #fn);      \
        fn();                          \
    } while (0)

/* ------------------------------------------------------------------ */
/* Payload + callbacks (state-machine)                                */
/* ------------------------------------------------------------------ */

struct counter {
    s16 value;
    s16 destroys;
};

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

/* ------------------------------------------------------------------ */
/* 1. init basico                                                     */
/* ------------------------------------------------------------------ */

static void test_init_basic(void)
{
    static DARKEN_DECLARE(storage, 8, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    CHECK(m.capacity == 8);
    CHECK(m.zones == 1);
    CHECK(m.bounds[0] == 0);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 0);
    CHECK(DARKEN_COUNT_FREE(&m) == 8);
    CHECK(m.stride >= sizeof(struct darken_entity_t) + sizeof(struct counter));
}

/* ------------------------------------------------------------------ */
/* 2. spawn + capacidad                                               */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* 3. update + CONTINUE                                               */
/* ------------------------------------------------------------------ */

static void test_update_continue(void)
{
    static DARKEN_DECLARE(storage, 4, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t e = DARKEN_SPAWN(&m);
    DARKEN_DATA(struct counter, c, e);
    c->value = 0;
    e->update = cb_increment;

    darken_update(&m);
    CHECK(c->value == 1);
    darken_update(&m);
    CHECK(c->value == 2);
    darken_update(&m);
    CHECK(c->value == 3);
}

/* ------------------------------------------------------------------ */
/* 4. DELETE desde callback + destroy                                 */
/* ------------------------------------------------------------------ */

static void test_self_delete(void)
{
    static DARKEN_DECLARE(storage, 4, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t e = DARKEN_SPAWN(&m);
    DARKEN_DATA(struct counter, c, e);
    c->value = 0;
    c->destroys = 0;
    e->update = cb_delete_now;
    e->destroy = cb_destroy;

    CHECK(DARKEN_COUNT_ACTIVE(&m) == 1);
    darken_update(&m);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 0);
    CHECK(c->value == -1);
    CHECK(c->destroys == 1);
    CHECK(DARKEN_ENTITY_IN_FREE(e));
}

/* ------------------------------------------------------------------ */
/* 5. instalacion de nuevo callback                                   */
/* ------------------------------------------------------------------ */

static void test_new_callback_install(void)
{
    static DARKEN_DECLARE(storage, 4, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t e = DARKEN_SPAWN(&m);
    DARKEN_DATA(struct counter, c, e);
    c->value = 0;
    e->update = cb_switch_at_3;

    darken_update(&m);
    CHECK(c->value == 1);
    CHECK(e->update == cb_switch_at_3);

    darken_update(&m);
    CHECK(c->value == 2);

    darken_update(&m);
    CHECK(c->value == 3);
    CHECK(e->update == cb_increment_10);

    darken_update(&m);
    CHECK(c->value == 13);
    darken_update(&m);
    CHECK(c->value == 23);
}

/* ------------------------------------------------------------------ */
/* 6. destroy en darken_entity_delete                                 */
/* ------------------------------------------------------------------ */

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
    CHECK(c->destroys == 1);
}

/* ------------------------------------------------------------------ */
/* 7. destroy en darken_reset (single zone)                           */
/* ------------------------------------------------------------------ */

static void test_reset_destroys_all(void)
{
    static DARKEN_DECLARE(storage, 4, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t e[3];
    s16 i;
    for (i = 0; i < 3; i++) {
        e[i] = DARKEN_SPAWN(&m);
        DARKEN_DATA(struct counter, c, e[i]);
        c->value = 0;
        c->destroys = 0;
        e[i]->update = cb_increment;
        e[i]->destroy = cb_destroy;
    }

    darken_reset(&m);

    for (i = 0; i < 3; i++) {
        DARKEN_DATA(struct counter, c, e[i]);
        CHECK(c->destroys == 1);
    }
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 0);
    CHECK(DARKEN_COUNT_FREE(&m) == 4);
}

/* ------------------------------------------------------------------ */
/* 8. recuperacion de entity via DARKEN_ENTITY                        */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* 9. reutilizacion de slot tras delete                               */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* 10. spawn por zona + count_zone + entity_zone                      */
/* ------------------------------------------------------------------ */

static void test_zones_spawn(void)
{
    static DARKEN_DECLARE_ZONES(storage, 12, 3, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t a = DARKEN_SPAWN_ZONE(&m, 0);
    darken_entity_t b = DARKEN_SPAWN_ZONE(&m, 0);
    darken_entity_t c = DARKEN_SPAWN_ZONE(&m, 1);
    darken_entity_t d = DARKEN_SPAWN_ZONE(&m, 2);
    darken_entity_t e = DARKEN_SPAWN_ZONE(&m, 2);

    CHECK(a && b && c && d && e);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 5);
    CHECK(DARKEN_COUNT_FREE(&m) == 7);
    CHECK(DARKEN_COUNT_ZONE(&m, 0) == 2);
    CHECK(DARKEN_COUNT_ZONE(&m, 1) == 1);
    CHECK(DARKEN_COUNT_ZONE(&m, 2) == 2);

    CHECK(darken_entity_zone(a) == 0);
    CHECK(darken_entity_zone(b) == 0);
    CHECK(darken_entity_zone(c) == 1);
    CHECK(darken_entity_zone(d) == 2);
    CHECK(darken_entity_zone(e) == 2);
}

/* ------------------------------------------------------------------ */
/* 11. DARKEN_FOREACH: solo zona 0                                    */
/* ------------------------------------------------------------------ */

static void test_foreach_zone0_only(void)
{
    static DARKEN_DECLARE_ZONES(storage, 12, 3, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    s16 i;
    for (i = 0; i < 3; i++) DARKEN_SPAWN_ZONE(&m, 0);
    for (i = 0; i < 4; i++) DARKEN_SPAWN_ZONE(&m, 1);
    for (i = 0; i < 2; i++) DARKEN_SPAWN_ZONE(&m, 2);

    s16 count = 0;
    DARKEN_FOREACH(&m, { (void)_entity; count++; });
    CHECK(count == 3);

    count = 0;
    DARKEN_FOREACH_ZONE(&m, 1, { (void)_entity; count++; });
    CHECK(count == 4);

    count = 0;
    DARKEN_FOREACH_ZONE(&m, 2, { (void)_entity; count++; });
    CHECK(count == 2);
}

/* ------------------------------------------------------------------ */
/* 12. set_zone conserva data y mueve la entidad                      */
/* ------------------------------------------------------------------ */

static void test_set_zone_moves(void)
{
    static DARKEN_DECLARE_ZONES(storage, 8, 3, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t e = DARKEN_SPAWN_ZONE(&m, 0);
    DARKEN_DATA(struct counter, c, e);
    c->value = 99;

    CHECK(darken_entity_zone(e) == 0);
    CHECK(DARKEN_COUNT_ZONE(&m, 0) == 1);
    CHECK(DARKEN_COUNT_ZONE(&m, 2) == 0);

    darken_entity_set_zone(e, 2);

    CHECK(darken_entity_zone(e) == 2);
    CHECK(DARKEN_COUNT_ZONE(&m, 0) == 0);
    CHECK(DARKEN_COUNT_ZONE(&m, 2) == 1);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 1);

    DARKEN_DATA(struct counter, c2, e);
    CHECK(c2->value == 99);

    darken_entity_set_zone(e, 2);
    CHECK(DARKEN_COUNT_ZONE(&m, 2) == 1);

    darken_entity_set_zone(e, 0);
    CHECK(darken_entity_zone(e) == 0);
    CHECK(DARKEN_COUNT_ZONE(&m, 0) == 1);
    CHECK(DARKEN_COUNT_ZONE(&m, 2) == 0);
}

/* ------------------------------------------------------------------ */
/* 13. update_zone solo actualiza esa zona                            */
/* ------------------------------------------------------------------ */

static void test_update_zone_only(void)
{
    static DARKEN_DECLARE_ZONES(storage, 8, 3, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t a = DARKEN_SPAWN_ZONE(&m, 0);
    darken_entity_t b = DARKEN_SPAWN_ZONE(&m, 1);
    darken_entity_t c = DARKEN_SPAWN_ZONE(&m, 2);

    DARKEN_DATA(struct counter, ca, a); ca->value = 0;
    DARKEN_DATA(struct counter, cb, b); cb->value = 0;
    DARKEN_DATA(struct counter, cc, c); cc->value = 0;

    a->update = cb_increment;
    b->update = cb_increment;
    c->update = cb_increment;

    darken_update_zone(&m, 1);
    CHECK(ca->value == 0);
    CHECK(cb->value == 1);
    CHECK(cc->value == 0);

    darken_update_zone(&m, 2);
    CHECK(cc->value == 1);
    CHECK(ca->value == 0);
}

/* ------------------------------------------------------------------ */
/* 14. darken_update: solo zona 0 (comportamiento actual)             */
/* ------------------------------------------------------------------ */

static void test_update_only_zone0(void)
{
    /* Fija el comportamiento actual: con DARKEN_FOREACH restringido a
     * zona 0, darken_update() solo actualiza la zona 0. Si algun dia
     * se cambia para recorrer todas las zonas, este test fallara y
     * habra que actualizarlo a proposito. */
    static DARKEN_DECLARE_ZONES(storage, 8, 3, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t a = DARKEN_SPAWN_ZONE(&m, 0);
    darken_entity_t b = DARKEN_SPAWN_ZONE(&m, 1);

    DARKEN_DATA(struct counter, ca, a); ca->value = 0;
    DARKEN_DATA(struct counter, cb, b); cb->value = 0;
    a->update = cb_increment;
    b->update = cb_increment;

    darken_update(&m);

    CHECK(ca->value == 1);
    CHECK(cb->value == 0);
}

/* ------------------------------------------------------------------ */
/* 15. reset_zone solo destruye su zona                               */
/* ------------------------------------------------------------------ */

static void test_reset_zone_only(void)
{
    static DARKEN_DECLARE_ZONES(storage, 12, 3, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t a = DARKEN_SPAWN_ZONE(&m, 0);
    darken_entity_t b = DARKEN_SPAWN_ZONE(&m, 1);
    darken_entity_t c = DARKEN_SPAWN_ZONE(&m, 1);
    darken_entity_t d = DARKEN_SPAWN_ZONE(&m, 2);

    DARKEN_DATA(struct counter, ca, a); ca->destroys = 0;
    DARKEN_DATA(struct counter, cb, b); cb->destroys = 0;
    DARKEN_DATA(struct counter, cc, c); cc->destroys = 0;
    DARKEN_DATA(struct counter, cd, d); cd->destroys = 0;

    a->destroy = cb_destroy;
    b->destroy = cb_destroy;
    c->destroy = cb_destroy;
    d->destroy = cb_destroy;

    darken_reset_zone(&m, 1);

    CHECK(ca->destroys == 0);
    CHECK(cb->destroys == 1);
    CHECK(cc->destroys == 1);
    CHECK(cd->destroys == 0);

    CHECK(DARKEN_COUNT_ZONE(&m, 0) == 1);
    CHECK(DARKEN_COUNT_ZONE(&m, 1) == 0);
    CHECK(DARKEN_COUNT_ZONE(&m, 2) == 1);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 2);
}

/* ------------------------------------------------------------------ */
/* 16. DARKEN_ENTITY_IN_ZONE                                          */
/* ------------------------------------------------------------------ */

static void test_entity_in_zone(void)
{
    static DARKEN_DECLARE_ZONES(storage, 8, 3, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t a = DARKEN_SPAWN_ZONE(&m, 0);
    darken_entity_t b = DARKEN_SPAWN_ZONE(&m, 2);

    CHECK(DARKEN_ENTITY_IN_ZONE(a, 0));
    CHECK(!DARKEN_ENTITY_IN_ZONE(a, 1));
    CHECK(!DARKEN_ENTITY_IN_ZONE(a, 2));

    CHECK(!DARKEN_ENTITY_IN_ZONE(b, 0));
    CHECK(DARKEN_ENTITY_IN_ZONE(b, 2));

    darken_entity_delete(a);
    CHECK(!DARKEN_ENTITY_IN_ZONE(a, 0));
    CHECK(DARKEN_ENTITY_IN_FREE(a));
}

/* ------------------------------------------------------------------ */
/* 17. DARKEN_ALLOC_ZONES sobre un buffer estatico                    */
/* ------------------------------------------------------------------ */

static u8  g_heap[2048];
static u16 g_heap_used = 0;

static void *test_alloc(u32 size)
{
    void *p = &g_heap[g_heap_used];
    g_heap_used += (u16)size;
    return p;
}

static void test_free(void *p) { (void)p; }

static void test_alloc_zones(void)
{
    g_heap_used = 0;
    darken_t m = DARKEN_ALLOC_ZONES(test_alloc, 6, 3, sizeof(struct counter));
    darken_init(&m);

    darken_entity_t a = DARKEN_SPAWN_ZONE(&m, 0);
    darken_entity_t b = DARKEN_SPAWN_ZONE(&m, 1);
    darken_entity_t c = DARKEN_SPAWN_ZONE(&m, 2);

    CHECK(a && b && c);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 3);
    CHECK(darken_entity_zone(a) == 0);
    CHECK(darken_entity_zone(b) == 1);
    CHECK(darken_entity_zone(c) == 2);

    DARKEN_FREE(test_free, &m);
}

/* ------------------------------------------------------------------ */
/* 18. stress: llenar, borrar la mitad, reset                         */
/* ------------------------------------------------------------------ */

static void test_stress(void)
{
    enum { N = 16 };
    static DARKEN_DECLARE_ZONES(storage, N, 4, sizeof(struct counter));
    darken_t m = DARKEN_BIND(storage);
    darken_init(&m);

    darken_entity_t e[N];
    s16 spawned = 0;
    s16 i;
    for (i = 0; i < N; i++) {
        s16 z = i & 3;
        e[i] = DARKEN_SPAWN_ZONE(&m, z);
        CHECK(e[i] != NULL);
        if (e[i]) {
            DARKEN_DATA(struct counter, c, e[i]);
            c->value = i;
            c->destroys = 0;
            e[i]->update = cb_increment;
            e[i]->destroy = cb_destroy;
            spawned++;
        }
    }
    CHECK(spawned == N);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == N);
    CHECK(DARKEN_SPAWN(&m) == NULL);

    for (i = 0; i < N; i += 2)
        darken_entity_delete(e[i]);

    CHECK(DARKEN_COUNT_ACTIVE(&m) == N / 2);
    CHECK(DARKEN_COUNT_FREE(&m) == N / 2);

    darken_reset(&m);
    CHECK(DARKEN_COUNT_ACTIVE(&m) == 0);
    CHECK(DARKEN_COUNT_FREE(&m) == N);
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int darken_120_dev_test_darken_main(bool hardReset)
{
    (void)hardReset;

    kprintf("darken 1.2.0_dev test suite");
    kprintf("----------------------------");

    RUN(test_init_basic);
    RUN(test_spawn_and_full);
    RUN(test_update_continue);
    RUN(test_self_delete);
    RUN(test_new_callback_install);
    RUN(test_delete_calls_destroy);
    RUN(test_reset_destroys_all);
    RUN(test_entity_recovery);
    RUN(test_slot_reuse);
    RUN(test_zones_spawn);
    RUN(test_foreach_zone0_only);
    RUN(test_set_zone_moves);
    RUN(test_update_zone_only);
    RUN(test_update_only_zone0);
    RUN(test_reset_zone_only);
    RUN(test_entity_in_zone);
    RUN(test_alloc_zones);
    RUN(test_stress);

    kprintf("----------------------------");
    kprintf("run=%d pass=%d fail=%d", (int)g_run, (int)g_pass, (int)g_fail);

    /* El resto de la ROM sigue corriendo. Si quieres parar aqui,
     * descomenta: for(;;) SYS_doVBlankProcess(); */

    return 0;
}