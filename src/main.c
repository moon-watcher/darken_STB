#include <genesis.h>
#define DARKEN_IMPLEMENTATION
#include "darken.h"

/* ============================================================
 * DARKEN_SYSTEMS.H (inline para ser autocontenido)
 * ============================================================ */

#ifndef DARKEN_SYSTEMS_H
#define DARKEN_SYSTEMS_H

typedef void (*de_system_fn)(de_manager *);

#define DE_SYSTEM(NAME, PAYLOAD_TYPE, CODE) \
    static void NAME(de_manager *_sys_mgr) \
    { \
        de_manager_iterate(_sys_mgr, { \
            PAYLOAD_TYPE *data = (PAYLOAD_TYPE *)ENTITY->data; \
            (void)data; \
            CODE; \
        }); \
    }

#define DE_SYSTEM_ALL(NAME, PAYLOAD_TYPE, CODE) \
    static void NAME(de_manager *_sys_mgr) \
    { \
        de_manager_iterateAll(_sys_mgr, { \
            PAYLOAD_TYPE *data = (PAYLOAD_TYPE *)ENTITY->data; \
            (void)data; \
            CODE; \
        }); \
    }

#define DE_SYSTEM_TAG(NAME, PAYLOAD_TYPE, TAG_VALUE, CODE) \
    static void NAME(de_manager *_sys_mgr) \
    { \
        de_manager_iterate(_sys_mgr, { \
            if (ENTITY->tag == (TAG_VALUE)) { \
                PAYLOAD_TYPE *data = (PAYLOAD_TYPE *)ENTITY->data; \
                (void)data; \
                CODE; \
            } \
        }); \
    }

typedef struct de_pipeline
{
    de_system_fn *fns;
    uint8_t       count;
    uint8_t       capacity;
} de_pipeline;

#define de_pipeline_init(P, FNS_ARRAY, CAP) \
    do { \
        (P)->fns = (FNS_ARRAY); \
        (P)->count = 0; \
        (P)->capacity = (CAP); \
    } while (0)

#define de_pipeline_add(P, FN) \
    do { \
        if ((P)->count < (P)->capacity) \
            (P)->fns[(P)->count++] = (FN); \
    } while (0)

#define de_pipeline_run(P, MGR) \
    do { \
        for (uint8_t _pi = 0; _pi < (P)->count; ++_pi) \
            (P)->fns[_pi](MGR); \
    } while (0)

#endif /* DARKEN_SYSTEMS_H */

/* ============================================================
 * PAYLOAD DE EJEMPLO
 * ============================================================ */

struct MyComponent
{
    int x, y;
    uint8_t health;
};

/* ============================================================
 * MINI FRAMEWORK DE TESTS
 * ============================================================ */

static u16 g_testsRun = 0;
static u16 g_testsPassed = 0;

#define CHECK(desc, cond)                     \
    do                                         \
    {                                          \
        g_testsRun++;                         \
        if (cond)                             \
        {                                      \
            g_testsPassed++;                  \
            kprintf("  [PASS] %s", desc);     \
        }                                      \
        else                                   \
        {                                      \
            kprintf("  [FAIL] %s", desc);     \
        }                                      \
    } while (0)

static bool all_slots_aligned(de_manager *mgr)
{
    for (uint16_t i = 0; i < mgr->capacity; ++i)
        if (((u32)mgr->items[i]) & 3)
            return FALSE;
    return TRUE;
}

/* ============================================================
 * TESTS 1-17: FUNCIONALIDAD BASE
 * ============================================================ */

static void test_alignment(void)
{
    kprintf("-- test_alignment --");
    de_manager m1, m2, m3;
    de_manager_create(&m1, 8, sizeof(struct MyComponent));
    de_manager_create(&m2, 8, sizeof(struct MyComponent) + 73);
    de_manager_create(&m3, 8, 1);
    CHECK("payload par: todos los slots alineados a 4", all_slots_aligned(&m1));
    CHECK("payload impar (+73): todos los slots alineados a 4", all_slots_aligned(&m2));
    CHECK("payload=1: todos los slots alineados a 4", all_slots_aligned(&m3));
}

static void test_creation(void)
{
    kprintf("-- test_creation --");
    de_manager m;
    de_manager_create(&m, 3, sizeof(struct MyComponent));
    CHECK("manager empieza con size 0", m.size == 0);
    de_entity *e0 = de_manager_new(&m);
    de_entity *e1 = de_manager_new(&m);
    de_entity *e2 = de_manager_new(&m);
    de_entity *e3 = de_manager_new(&m);
    CHECK("new valida (1)", e0 != 0);
    CHECK("new valida (2)", e1 != 0);
    CHECK("new valida (3)", e2 != 0);
    CHECK("new devuelve 0 al llenarse", e3 == 0);
    CHECK("size == capacity", m.size == 3);
    CHECK("arranca en state delete", e0->state == de_state_delete);
    CHECK("arranca con tag 0", e0->tag == 0);
}

static int g_walkCalls = 0;
static void *state_walk(void *data)
{
    struct MyComponent *c = (struct MyComponent *)data;
    c->x += 1;
    ++g_walkCalls;
    return (void *)de_state_loop;
}
static void *state_once_then_idle(void *data)
{
    (void)data;
    return (void *)state_walk;
}

static void test_update(void)
{
    kprintf("-- test_update --");
    g_walkCalls = 0;
    de_manager m;
    de_manager_create(&m, 2, sizeof(struct MyComponent));
    de_entity *e = de_manager_new(&m);
    struct MyComponent *c = (struct MyComponent *)e->data;
    c->x = 0;
    e->state = (de_state)state_walk;
    de_manager_update(&m);
    de_manager_update(&m);
    de_manager_update(&m);
    CHECK("loop no cambia puntero", e->state == (de_state)state_walk);
    CHECK("loop ejecuta x3", g_walkCalls == 3 && c->x == 3);
    de_entity *e2 = de_manager_new(&m);
    e2->state = (de_state)state_once_then_idle;
    de_manager_update(&m);
    CHECK("transicion actualiza estado", e2->state == (de_state)state_walk);
}

static int g_idleCalls = 0;
static void *state_idle_counter(void *data)
{
    (void)data;
    ++g_idleCalls;
    return (void *)de_state_loop;
}

static void test_pause_resume(void)
{
    kprintf("-- test_pause_resume --");
    g_idleCalls = 0;
    de_manager m;
    de_manager_create(&m, 4, sizeof(struct MyComponent));
    de_entity *a = de_manager_new(&m);
    de_entity *b = de_manager_new(&m);
    a->state = (de_state)state_idle_counter;
    b->state = (de_state)state_idle_counter;
    de_manager_update(&m);
    CHECK("ambas activas: 2 llamadas", g_idleCalls == 2);
    de_entity_pause(b);
    CHECK("b cae antes de pause_index", b->slot < m.pause_index);
    g_idleCalls = 0;
    de_manager_update(&m);
    CHECK("con b pausada: 1 llamada", g_idleCalls == 1);
    de_entity_resume(b);
    CHECK("b vuelve a zona activa", b->slot >= m.pause_index);
    g_idleCalls = 0;
    de_manager_update(&m);
    CHECK("resumir: 2 llamadas", g_idleCalls == 2);
}

static int g_destructorCalls = 0;
static void *my_destructor(void *data)
{
    (void)data;
    ++g_destructorCalls;
    return (void *)de_state_delete;
}
static void *state_noop(void *data)
{
    (void)data;
    return (void *)de_state_loop;
}

static void test_delete(void)
{
    kprintf("-- test_delete --");
    g_destructorCalls = 0;
    de_manager m;
    de_manager_create(&m, 4, sizeof(struct MyComponent));
    de_entity *a = de_manager_new(&m);
    de_entity *b = de_manager_new(&m);
    de_entity *c = de_manager_new(&m);
    a->destructor = (de_state)my_destructor;
    b->destructor = (de_state)my_destructor;
    c->destructor = (de_state)my_destructor;
    b->state = (de_state)state_noop;
    c->state = (de_state)state_noop;
    a->state = de_state_delete;
    de_manager_update(&m);
    CHECK("delete via estado: size 2", m.size == 2);
    CHECK("delete via estado: destructor 1 vez", g_destructorCalls == 1);
    de_entity_delete(b);
    CHECK("delete directo: size 1", m.size == 1);
    CHECK("delete directo: destructor 2 veces", g_destructorCalls == 2);
}

static void test_apply(void)
{
    kprintf("-- test_apply --");
    de_manager m;
    de_manager_create(&m, 5, sizeof(struct MyComponent));
    for (int i = 0; i < 5; ++i)
    {
        de_entity *e = de_manager_new(&m);
        e->tag = i;
    }
    de_manager_applyAll(&m, (ENTITY->tag % 2) == 0, de_entity_delete);
    CHECK("quedan 2 entidades", m.size == 2);
    bool onlyOdd = TRUE;
    de_manager_iterateAll(&m, {
        if ((ENTITY->tag % 2) == 0) onlyOdd = FALSE;
    });
    CHECK("quedan tags impares", onlyOdd);
}

static void test_reset(void)
{
    kprintf("-- test_reset --");
    g_destructorCalls = 0;
    de_manager m;
    de_manager_create(&m, 6, sizeof(struct MyComponent));
    for (int i = 0; i < 6; ++i)
    {
        de_entity *e = de_manager_new(&m);
        e->destructor = (de_state)my_destructor;
    }
    de_manager_reset(&m);
    CHECK("reset: size 0", m.size == 0);
    CHECK("reset: destructor 6 veces", g_destructorCalls == 6);
}

static int g_abortDestructorCalls = 0;
static void *state_abort_destructor(void *data)
{
    (void)data;
    ++g_abortDestructorCalls;
    return (void *)de_state_loop;
}

static void test_destructor_abort(void)
{
    kprintf("-- test_destructor_abort --");
    g_abortDestructorCalls = 0;
    de_manager m;
    de_manager_create(&m, 2, sizeof(struct MyComponent));
    de_entity *e = de_manager_new(&m);
    e->destructor = (de_state)state_abort_destructor;
    e->state = de_state_delete;
    de_manager_update(&m);
    CHECK("abort: size 1", m.size == 1);
    CHECK("abort: destructor llamado", g_abortDestructorCalls == 1);
    CHECK("abort: entidad viva", e->slot < m.size);
    CHECK("abort: estado a loop", e->state == de_state_loop);
}

static int g_selfKillCalls = 0;
static void *state_self_kill(void *data)
{
    (void)data;
    ++g_selfKillCalls;
    return (void *)de_state_delete;
}

static void test_self_delete(void)
{
    kprintf("-- test_self_delete --");
    g_selfKillCalls = 0;
    de_manager m;
    de_manager_create(&m, 3, sizeof(struct MyComponent));
    de_entity *a = de_manager_new(&m);
    de_entity *b = de_manager_new(&m);
    a->state = (de_state)state_self_kill;
    b->state = (de_state)state_noop;
    de_manager_update(&m);
    CHECK("self-del f1: ejecutado 1 vez", g_selfKillCalls == 1);
    CHECK("self-del f1: marcada deleted", a->state == de_state_delete);
    CHECK("self-del f1: size aun 2", m.size == 2);
    de_manager_update(&m);
    CHECK("self-del f2: size 1", m.size == 1);
    CHECK("self-del f2: queda b", m.items[0] == b);
}

static void test_slot_stability(void)
{
    kprintf("-- test_slot_stability --");
    de_manager m;
    de_manager_create(&m, 4, sizeof(struct MyComponent));
    de_entity *e0 = de_manager_new(&m);
    de_entity *e1 = de_manager_new(&m);
    de_entity *e2 = de_manager_new(&m);
    de_entity *e3 = de_manager_new(&m);
    e0->state = e1->state = e2->state = e3->state = (de_state)state_noop;
    de_entity_pause(e1);
    de_entity_pause(e3);
    bool stable = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
        if (m.items[i]->slot != i) stable = FALSE;
    CHECK("slots tras pausar", stable);
    CHECK("pause_index 2", m.pause_index == 2);
    de_entity_delete(e0);
    stable = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
        if (m.items[i]->slot != i) stable = FALSE;
    CHECK("slots tras delete", stable);
    CHECK("size 3", m.size == 3);
}

static void test_delete_paused(void)
{
    kprintf("-- test_delete_paused --");
    de_manager m;
    de_manager_create(&m, 4, sizeof(struct MyComponent));
    de_entity *a = de_manager_new(&m);
    de_entity *b = de_manager_new(&m);
    de_entity *c = de_manager_new(&m);
    a->state = b->state = c->state = (de_state)state_noop;
    de_entity_pause(b);
    de_entity_delete(b);
    CHECK("del pausado: size 2", m.size == 2);
    CHECK("del pausado: pause_index 0", m.pause_index == 0);
    CHECK("del pausado: a activa", a->slot >= m.pause_index);
    CHECK("del pausado: c activa", c->slot >= m.pause_index);
}

static void test_apply_pause(void)
{
    kprintf("-- test_apply_pause --");
    de_manager m;
    de_manager_create(&m, 6, sizeof(struct MyComponent));
    for (int i = 0; i < 6; ++i)
    {
        de_entity *e = de_manager_new(&m);
        e->tag = i;
        e->state = (de_state)state_noop;
    }
    de_manager_applyAll(&m, (ENTITY->tag % 2) == 0, de_entity_pause);
    CHECK("apply pause: pause_index 3", m.pause_index == 3);
    CHECK("apply pause: size 6", m.size == 6);
    bool correct = TRUE;
    de_manager_iterateAll(&m, {
        bool shouldBePaused = (ENTITY->tag % 2) == 0;
        bool isPaused = ENTITY->slot < m.pause_index;
        if (shouldBePaused != isPaused) correct = FALSE;
    });
    CHECK("apply pause: pares pausadas", correct);
}

static void test_reuse(void)
{
    kprintf("-- test_reuse --");
    de_manager m;
    de_manager_create(&m, 3, sizeof(struct MyComponent));
    de_entity *e0 = de_manager_new(&m);
    e0->state = (de_state)state_noop;
    struct MyComponent *d0 = (struct MyComponent *)e0->data;
    d0->x = 42;
    de_entity_delete(e0);
    de_entity *e1 = de_manager_new(&m);
    CHECK("reuse: not null", e1 != 0);
    CHECK("reuse: slot 0", e1->slot == 0);
    CHECK("reuse: size 1", m.size == 1);
    struct MyComponent *d1 = (struct MyComponent *)e1->data;
    CHECK("reuse: datos antiguos persisten", d1->x == 42);
}

static int g_stressCallsA = 0, g_stressCallsB = 0;
static void *state_stress_a(void *data) { (void)data; ++g_stressCallsA; return (void *)de_state_loop; }
static void *state_stress_b(void *data) { (void)data; ++g_stressCallsB; return (void *)de_state_loop; }

static void test_mixed_stress(void)
{
    kprintf("-- test_mixed_stress --");
    g_stressCallsA = 0; g_stressCallsB = 0;
    de_manager m;
    de_manager_create(&m, 5, sizeof(struct MyComponent));
    de_entity *e[5];
    for (int i = 0; i < 5; ++i)
    {
        e[i] = de_manager_new(&m);
        e[i]->tag = i;
        e[i]->state = (i % 2 == 0) ? (de_state)state_stress_a : (de_state)state_stress_b;
    }
    de_manager_update(&m);
    CHECK("stress f1: 5 llamadas", g_stressCallsA + g_stressCallsB == 5);
    de_entity_pause(e[1]);
    e[3]->state = de_state_delete;
    g_stressCallsA = 0; g_stressCallsB = 0;
    de_manager_update(&m);
    CHECK("stress f2: 3 activas", g_stressCallsA + g_stressCallsB == 3);
    CHECK("stress f2: size 4", m.size == 4);
    de_entity_resume(e[1]);
    g_stressCallsA = 0; g_stressCallsB = 0;
    de_manager_update(&m);
    CHECK("stress f3: 4 activas", g_stressCallsA + g_stressCallsB == 4);
    bool ok = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
        if (m.items[i]->slot != i) ok = FALSE;
    CHECK("stress: slots ok", ok);
}

static void test_data_integrity(void)
{
    kprintf("-- test_data_integrity --");
    de_manager m;
    de_manager_create(&m, 3, sizeof(struct MyComponent));
    de_entity *a = de_manager_new(&m);
    de_entity *b = de_manager_new(&m);
    de_entity *c = de_manager_new(&m);
    struct MyComponent *da = (struct MyComponent *)a->data;
    struct MyComponent *db = (struct MyComponent *)b->data;
    struct MyComponent *dc = (struct MyComponent *)c->data;
    da->x = 111; db->x = 333; dc->x = 555;
    a->state = b->state = c->state = (de_state)state_noop;
    de_entity_pause(b);
    de_entity_delete(a);
    CHECK("integrity a", ((struct MyComponent *)a->data)->x == 111);
    CHECK("integrity b", ((struct MyComponent *)b->data)->x == 333);
    CHECK("integrity c", ((struct MyComponent *)c->data)->x == 555);
}

static void test_empty_manager(void)
{
    kprintf("-- test_empty_manager --");
    de_manager m;
    de_manager_create(&m, 4, sizeof(struct MyComponent));
    CHECK("empty: size 0", m.size == 0);
    de_manager_update(&m);
    de_manager_pause(&m);
    de_manager_resume(&m);
    CHECK("empty: size sigue 0", m.size == 0);
    CHECK("empty: pause_index 0", m.pause_index == 0);
}

static void test_delete_last(void)
{
    kprintf("-- test_delete_last --");
    de_manager m;
    de_manager_create(&m, 4, sizeof(struct MyComponent));
    de_entity *e = de_manager_new(&m);
    e->state = (de_state)state_noop;
    de_entity_delete(e);
    CHECK("last: size 0", m.size == 0);
    CHECK("last: pause_index 0", m.pause_index == 0);
}

/* ============================================================
 * TESTS 18-20: ESTRES DE ALTA CAPACIDAD
 * ============================================================ */

static void test_stress_capacity(void)
{
    kprintf("-- test_stress_capacity --");
    de_manager m;
    de_manager_create(&m, 50, sizeof(struct MyComponent));

    de_entity *ents[50];
    for (int i = 0; i < 50; ++i)
    {
        ents[i] = de_manager_new(&m);
        CHECK("llenado completo", ents[i] != 0);
    }
    CHECK("capacity 50", m.size == 50);

    for (int i = 0; i < 50; i += 2)
        de_entity_delete(ents[i]);

    CHECK("tras borrar pares: size 25", m.size == 25);

    for (int i = 0; i < 10; ++i)
    {
        de_entity *e = de_manager_new(&m);
        CHECK("rellenar huecos", e != 0);
    }
    CHECK("size final 35", m.size == 35);

    bool ok = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
    {
        if (m.items[i]->slot != i) ok = FALSE;
        if (m.items[i]->manager != &m) ok = FALSE;
    }
    CHECK("slots coherentes tras fragmentacion", ok);
}

static void test_stress_many_entities(void)
{
    kprintf("-- test_stress_many_entities --");
    de_manager m;
    de_manager_create(&m, 150, sizeof(struct MyComponent));

    de_entity *ents[150];
    for (int i = 0; i < 150; ++i)
    {
        ents[i] = de_manager_new(&m);
        ents[i]->state = (de_state)state_noop;
    }
    CHECK("many: size 150", m.size == 150);

    /* Pausar las entidades con índice par usando sus punteros originales */
    for (int i = 0; i < 150; i += 2)
        de_entity_pause(ents[i]);

    CHECK("many: pause_index 75", m.pause_index == 75);

    de_manager_update(&m);

    /* Verificar que las entidades ORIGINALMENTE pares siguen en zona pausada */
    bool ok = TRUE;
    for (int i = 0; i < 150; i += 2)
        if (ents[i]->slot >= m.pause_index) ok = FALSE;
    CHECK("many: pares siguen pausados", ok);

    /* Verificar que las entidades ORIGINALMENTE impares siguen activas */
    bool ok2 = TRUE;
    for (int i = 1; i < 150; i += 2)
        if (ents[i]->slot < m.pause_index) ok2 = FALSE;
    CHECK("many: impares siguen activas", ok2);
}

static void test_stress_fragmentation(void)
{
    kprintf("-- test_stress_fragmentation --");
    de_manager m;
    de_manager_create(&m, 20, sizeof(struct MyComponent));

    de_entity *e[20];
    for (int i = 0; i < 20; ++i)
    {
        e[i] = de_manager_new(&m);
        e[i]->tag = i;
        e[i]->state = (de_state)state_noop;
    }

    for (int i = 1; i < 20; i += 2)
        de_entity_delete(e[i]);

    CHECK("frag: size 10 tras borrar impares", m.size == 10);

    for (int i = 0; i < 5; ++i)
    {
        de_entity *ne = de_manager_new(&m);
        ne->tag = 100 + i;
        ne->state = (de_state)state_noop;
    }
    CHECK("frag: size 15 tras recrear", m.size == 15);

    bool unique = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
    {
        for (uint16_t j = i + 1; j < m.size; ++j)
        {
            if (m.items[i]->tag == m.items[j]->tag) unique = FALSE;
        }
    }
    CHECK("frag: tags unicos tras recreacion", unique);
}

/* ============================================================
 * TESTS 21-25: SISTEMAS (DARKSYS / DARKEN_SYSTEMS)
 * ============================================================ */

/* Sistemas de prueba */
DE_SYSTEM(sys_inc_x, struct MyComponent, {
    data->x += 1;
});

DE_SYSTEM(sys_double_y, struct MyComponent, {
    data->y *= 2;
});

DE_SYSTEM_TAG(sys_tag1_only, struct MyComponent, 1, {
    data->health += 10;
});

DE_SYSTEM_ALL(sys_all_health_dec, struct MyComponent, {
    data->health -= 1;
});

static void test_system_basic(void)
{
    kprintf("-- test_system_basic --");
    de_manager m;
    de_manager_create(&m, 4, sizeof(struct MyComponent));

    for (int i = 0; i < 4; ++i)
    {
        de_entity *e = de_manager_new(&m);
        struct MyComponent *d = (struct MyComponent *)e->data;
        d->x = i;
        d->y = 1;
        d->health = 100;
        e->state = (de_state)state_noop;
    }

    sys_inc_x(&m);

    bool ok = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
    {
        struct MyComponent *d = (struct MyComponent *)m.items[i]->data;
        if (d->x != (int)i + 1) ok = FALSE;
    }
    CHECK("system basic: todas las x incrementadas", ok);
    CHECK("system basic: size intacto", m.size == 4);
}

static void test_system_respects_pause(void)
{
    kprintf("-- test_system_respects_pause --");
    de_manager m;
    de_manager_create(&m, 4, sizeof(struct MyComponent));

    de_entity *e[4];
    for (int i = 0; i < 4; ++i)
    {
        e[i] = de_manager_new(&m);
        struct MyComponent *d = (struct MyComponent *)e[i]->data;
        d->x = 0;
        e[i]->state = (de_state)state_noop;
    }

    de_entity_pause(e[1]);
    de_entity_pause(e[3]);

    sys_inc_x(&m);

    CHECK("sys respect pause: activa[0] modificada",
          ((struct MyComponent *)e[0]->data)->x == 1);
    CHECK("sys respect pause: pausada[1] NO modificada",
          ((struct MyComponent *)e[1]->data)->x == 0);
    CHECK("sys respect pause: activa[2] modificada",
          ((struct MyComponent *)e[2]->data)->x == 1);
    CHECK("sys respect pause: pausada[3] NO modificada",
          ((struct MyComponent *)e[3]->data)->x == 0);
}

static void test_system_all_includes_paused(void)
{
    kprintf("-- test_system_all_includes_paused --");
    de_manager m;
    de_manager_create(&m, 3, sizeof(struct MyComponent));

    de_entity *e[3];
    for (int i = 0; i < 3; ++i)
    {
        e[i] = de_manager_new(&m);
        struct MyComponent *d = (struct MyComponent *)e[i]->data;
        d->health = 5;
        e[i]->state = (de_state)state_noop;
    }

    de_entity_pause(e[1]);

    sys_all_health_dec(&m);

    CHECK("sys all: activa[0] health 4",
          ((struct MyComponent *)e[0]->data)->health == 4);
    CHECK("sys all: pausada[1] health 4 (tambien tocada)",
          ((struct MyComponent *)e[1]->data)->health == 4);
    CHECK("sys all: activa[2] health 4",
          ((struct MyComponent *)e[2]->data)->health == 4);
}

static void test_system_tag_filter(void)
{
    kprintf("-- test_system_tag_filter --");
    de_manager m;
    de_manager_create(&m, 6, sizeof(struct MyComponent));

    for (int i = 0; i < 6; ++i)
    {
        de_entity *e = de_manager_new(&m);
        struct MyComponent *d = (struct MyComponent *)e->data;
        d->health = 0;
        e->tag = (i < 3) ? 1 : 2;
        e->state = (de_state)state_noop;
    }

    sys_tag1_only(&m);

    bool ok = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
    {
        struct MyComponent *d = (struct MyComponent *)m.items[i]->data;
        uint8_t expected = (m.items[i]->tag == 1) ? 10 : 0;
        if (d->health != expected) ok = FALSE;
    }
    CHECK("sys tag: solo tag 1 modificado", ok);
}

static void test_pipeline_order(void)
{
    kprintf("-- test_pipeline_order --");
    de_manager m;
    de_manager_create(&m, 2, sizeof(struct MyComponent));

    for (int i = 0; i < 2; ++i)
    {
        de_entity *e = de_manager_new(&m);
        struct MyComponent *d = (struct MyComponent *)e->data;
        d->x = 0;
        d->y = 3;
        e->state = (de_state)state_noop;
    }

    de_system_fn fns[4];
    de_pipeline pipe;
    de_pipeline_init(&pipe, fns, 4);
    de_pipeline_add(&pipe, sys_inc_x);      /* x += 1 */
    de_pipeline_add(&pipe, sys_double_y);   /* y *= 2 */
    de_pipeline_add(&pipe, sys_inc_x);      /* x += 1 (de nuevo) */

    de_pipeline_run(&pipe, &m);

    struct MyComponent *d0 = (struct MyComponent *)m.items[0]->data;
    CHECK("pipeline: x incrementado 2 veces", d0->x == 2);
    CHECK("pipeline: y duplicado 1 vez", d0->y == 6);
    CHECK("pipeline: count 3", pipe.count == 3);
}

/* ============================================================
 * ORQUESTADOR DE TESTS
 * ============================================================ */

static void run_all_tests(void)
{
    g_testsRun = 0;
    g_testsPassed = 0;

    kprintf("========== TESTS ==========");
    test_alignment();
    test_creation();
    test_update();
    test_pause_resume();
    test_delete();
    test_apply();
    test_reset();
    test_destructor_abort();
    test_self_delete();
    test_slot_stability();
    test_delete_paused();
    test_apply_pause();
    test_reuse();
    test_mixed_stress();
    test_data_integrity();
    test_empty_manager();
    test_delete_last();
    test_stress_capacity();
    test_stress_many_entities();
    test_stress_fragmentation();
    test_system_basic();
    test_system_respects_pause();
    test_system_all_includes_paused();
    test_system_tag_filter();
    test_pipeline_order();
    kprintf("============================");
    kprintf("Resultado: %d/%d tests OK", g_testsPassed, g_testsRun);
}

/* ============================================================
 * BENCHMARKS DE RENDIMIENTO
 * ============================================================ */

static volatile u32 g_frameCounter = 0;

static void bench_vblank_cb(void)
{
    g_frameCounter++;
}

static u32 bench_start(void)
{
    return g_frameCounter;
}

static u32 bench_frames_elapsed(u32 start)
{
    return g_frameCounter - start;
}

#define BENCH_REPS 2000

static void bench_create_destroy(void)
{
    de_manager m;
    de_manager_create(&m, 32, sizeof(struct MyComponent));
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS; ++r)
    {
        for (u16 i = 0; i < 32; ++i)
            de_manager_new(&m);
        de_manager_reset(&m);
    }
    u32 frames = bench_frames_elapsed(t0);
    kprintf("create+reset 32 entidades x%d reps: %ld frames", BENCH_REPS, frames);
}

static void *bench_state_fn(void *data)
{
    (void)data;
    return (void *)de_state_loop;
}

static void bench_update(void)
{
    de_manager m;
    de_manager_create(&m, 32, sizeof(struct MyComponent));
    for (u16 i = 0; i < 32; ++i)
    {
        de_entity *e = de_manager_new(&m);
        e->state = (de_state)bench_state_fn;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS; ++r)
        de_manager_update(&m);
    u32 frames = bench_frames_elapsed(t0);
    kprintf("de_manager_update, 32 entidades x%d reps: %ld frames", BENCH_REPS, frames);
}

static void bench_apply(void)
{
    de_manager m;
    de_manager_create(&m, 32, sizeof(struct MyComponent));
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS; ++r)
    {
        for (u16 i = 0; i < 32; ++i)
        {
            de_entity *e = de_manager_new(&m);
            e->tag = i;
        }
        de_manager_applyAll(&m, (ENTITY->tag % 2) == 0, de_entity_delete);
        de_manager_reset(&m);
    }
    u32 frames = bench_frames_elapsed(t0);
    kprintf("create32+applyAll(borrar pares)+reset x%d reps: %ld frames", BENCH_REPS, frames);
}

static void bench_create_destroy_n(u16 n, u32 reps)
{
    de_manager m;
    de_manager_create(&m, n, sizeof(struct MyComponent));
    u32 t0 = bench_start();
    for (u32 r = 0; r < reps; ++r)
    {
        for (u16 i = 0; i < n; ++i)
            de_manager_new(&m);
        de_manager_reset(&m);
    }
    u32 frames = bench_frames_elapsed(t0);
    kprintf("create+reset %d entidades x%ld reps: %ld frames", n, reps, frames);
}

static void bench_update_n(u16 n, u32 reps)
{
    de_manager m;
    de_manager_create(&m, n, sizeof(struct MyComponent));
    for (u16 i = 0; i < n; ++i)
    {
        de_entity *e = de_manager_new(&m);
        e->state = (de_state)bench_state_fn;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < reps; ++r)
        de_manager_update(&m);
    u32 frames = bench_frames_elapsed(t0);
    kprintf("de_manager_update, %d entidades x%ld reps: %ld frames", n, reps, frames);
}

#define BENCH_SWAP_REPS 50000

static void bench_swap(void)
{
    de_manager m;
    de_manager_create(&m, 2, sizeof(struct MyComponent));
    de_entity *a = de_manager_new(&m);
    de_entity *b = de_manager_new(&m);
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_SWAP_REPS; ++r)
        de_entity_swap(a, b);
    u32 frames = bench_frames_elapsed(t0);
    kprintf("de_entity_swap x%d: %ld frames", BENCH_SWAP_REPS, frames);
}

/* ============================================================
 * BENCHMARK DE SISTEMAS (pipeline vs individual)
 * ============================================================ */

static void *bench_system_state(void *data)
{
    struct MyComponent *c = (struct MyComponent *)data;
    c->x += 1;
    c->y += 2;
    return (void *)de_state_loop;
}

DE_SYSTEM(sys_bench_move, struct MyComponent, {
    data->x += 1;
    data->y += 2;
});

static void bench_systems_vs_individual(void)
{
    kprintf("========== BENCHMARK SISTEMAS ==========");

    /* Individual: cada entidad con su puntero a funcion */
    de_manager m_ind;
    de_manager_create(&m_ind, 32, sizeof(struct MyComponent));
    for (u16 i = 0; i < 32; ++i)
    {
        de_entity *e = de_manager_new(&m_ind);
        e->state = (de_state)bench_system_state;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS; ++r)
        de_manager_update(&m_ind);
    u32 frames_ind = bench_frames_elapsed(t0);
    kprintf("update INDIVIDUAL (32 ent x%d reps): %ld frames", BENCH_REPS, frames_ind);

    /* Pipeline: sistemas por lotes, entidades en state_noop */
    de_manager m_sys;
    de_manager_create(&m_sys, 32, sizeof(struct MyComponent));
    for (u16 i = 0; i < 32; ++i)
    {
        de_entity *e = de_manager_new(&m_sys);
        e->state = (de_state)state_noop;
    }
    de_system_fn fns[2];
    de_pipeline pipe;
    de_pipeline_init(&pipe, fns, 2);
    de_pipeline_add(&pipe, sys_bench_move);

    t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS; ++r)
        de_pipeline_run(&pipe, &m_sys);
    u32 frames_sys = bench_frames_elapsed(t0);
    kprintf("update PIPELINE sys_move (32 ent x%d reps): %ld frames", BENCH_REPS, frames_sys);

    kprintf("=======================================");
}

/* ============================================================
 * BENCHMARK DE MEMORIA
 * ============================================================ */

static void bench_memory_overhead(void)
{
    kprintf("========== MEMORIA ==========");

    u16 stride16 = _DE_ENTITY_STRIDE(16);
    u16 stride32 = _DE_ENTITY_STRIDE(32);
    u16 stride1  = _DE_ENTITY_STRIDE(1);
    u16 stride9  = _DE_ENTITY_STRIDE(9);

    kprintf("sizeof(de_entity) base: %d bytes", sizeof(de_entity));
    kprintf("stride payload=1:  %d bytes/entidad", stride1);
    kprintf("stride payload=9:  %d bytes/entidad (impar)", stride9);
    kprintf("stride payload=16: %d bytes/entidad", stride16);
    kprintf("stride payload=32: %d bytes/entidad", stride32);

    de_manager m32;
    de_manager_create(&m32, 32, sizeof(struct MyComponent));
    u32 bytes32 = 32 * _DE_ENTITY_STRIDE(sizeof(struct MyComponent));
    kprintf("Manager 32 entidades (payload %d): %ld bytes en storage",
            sizeof(struct MyComponent), bytes32);

    de_manager m128;
    de_manager_create(&m128, 128, sizeof(struct MyComponent));
    u32 bytes128 = 128 * _DE_ENTITY_STRIDE(sizeof(struct MyComponent));
    kprintf("Manager 128 entidades (payload %d): %ld bytes en storage",
            sizeof(struct MyComponent), bytes128);

    u16 overhead = _DE_ENTITY_STRIDE(sizeof(struct MyComponent)) - sizeof(struct MyComponent);
    kprintf("Overhead por entidad: %d bytes (header + padding)", overhead);

    u32 ramAvailable = 64 * 1024;
    u32 entidadesEn64k = ramAvailable / _DE_ENTITY_STRIDE(sizeof(struct MyComponent));
    kprintf("Entidades de MyComponent que caben en 64 KB: ~%ld", entidadesEn64k);
    kprintf("==============================");
}

/* ============================================================
 * EJEMPLO DE USO NORMAL
 * ============================================================ */

void *update_walk(struct MyComponent *data)
{
    data->x += 1;
    data->y += 1;
    kprintf("Walking: (%d, %d) health=%d", data->x, data->y, data->health);
    return (void *)de_state_loop;
}

void *update_idle(struct MyComponent *data)
{
    kprintf("Idle: (%d, %d) health=%d", data->x, data->y, data->health);
    return (void *)de_state_loop;
}

void *destructor(struct MyComponent *data)
{
    kprintf("Destructor llamado para entidad en (%d, %d)", data->x, data->y);
    return (void *)de_state_delete;
}

static void run_usage_example(void)
{
    kprintf("========== EJEMPLO DE USO ==========");

    de_manager g_manager;
    de_manager g_manager2;

    de_manager_create(&g_manager, 10, sizeof(struct MyComponent));
    de_manager_create(&g_manager2, 20, sizeof(struct MyComponent) + 73);

    de_entity *e1 = de_manager_new(&g_manager);
    struct MyComponent *data1 = (struct MyComponent *)e1->data;
    data1->x = 0; data1->y = 0; data1->health = 100;
    e1->state = (de_state)update_walk;
    e1->destructor = (de_state)destructor;
    e1->tag = 1;

    de_entity *e2 = de_manager_new(&g_manager);
    struct MyComponent *data2 = (struct MyComponent *)e2->data;
    data2->x = 10; data2->y = 20; data2->health = 80;
    e2->state = (de_state)update_idle;
    e2->destructor = (de_state)destructor;
    e2->tag = 2;

    de_entity *e3 = de_manager_new(&g_manager);
    struct MyComponent *data3 = (struct MyComponent *)e3->data;
    data3->x = 5; data3->y = 5; data3->health = 50;
    e3->state = (de_state)update_walk;
    e3->destructor = (de_state)destructor;
    e3->tag = 3;

    kprintf("--- Frame 1 ---");
    de_manager_update(&g_manager);

    de_manager_applyAll(&g_manager, ENTITY->tag == 2, de_entity_pause);
    kprintf("Pausada entidad tag 2");

    kprintf("--- Frame 2 ---");
    de_manager_update(&g_manager);

    de_manager_applyAll(&g_manager, ENTITY->tag == 1, de_entity_delete);

    kprintf("--- Frame 3 ---");
    de_manager_update(&g_manager);

    kprintf("--- Reset ---");
    de_manager_reset(&g_manager);

    kprintf("Tamano final g_manager: %d", g_manager.size);
    kprintf("Tamano final g_manager2: %d", g_manager2.size);
    kprintf("=====================================");
}

/* ============================================================
 * MAIN
 * ============================================================ */

int main(void)
{
    SYS_setVIntCallback(bench_vblank_cb);

    run_usage_example();
    run_all_tests();
    bench_memory_overhead();

    kprintf("========== BENCHMARKS ==========");
    bench_create_destroy();
    bench_update();
    bench_apply();
    bench_create_destroy_n(128, 500);
    bench_create_destroy_n(256, 250);
    bench_update_n(128, 500);
    bench_update_n(256, 250);
    bench_swap();
    bench_systems_vs_individual();
    kprintf("=================================");

    while (1)
        SYS_doVBlankProcess();

    return 0;
}

/* ============================================================
 * DOCUMENTACION DE LA API
 * ============================================================
 *
 * darken.h -- Entity/Manager library para Sega Genesis (m68k)
 *
 * USO BASICO
 * ----------
 *   1. Incluir en un solo archivo .c:
 *          #define DARKEN_IMPLEMENTATION
 *          #include "darken.h"
 *
 *   2. Declarar un manager (storage estatico):
 *          de_manager mgr;
 *          de_manager_create(&mgr, CAPACIDAD, sizeof(MiPayload));
 *
 *   3. Crear entidades:
 *          de_entity *e = de_manager_new(&mgr);
 *          MiPayload *p = (MiPayload *)e->data;
 *          e->state = (de_state)mi_funcion_update;
 *
 * CICLO DE VIDA DE UNA ENTIDAD
 * ----------------------------
 *   [new] -> state=delete, slot=asignado, data=sin inicializar
 *     |
 *     v
 *   [active]  state > de_state_pause  -> se ejecuta cada frame
 *   [paused]  state == de_state_pause -> se ignora en update
 *   [deleted] state == de_state_delete -> se elimina en el proximo update
 *
 *   Transiciones:
 *     - Un estado activo puede retornar otro estado para cambiar.
 *     - Retornar de_state_loop mantiene el estado actual (no toca puntero).
 *     - Retornar de_state_delete marca para borrar (borrado DIFERIDO).
 *
 * ESTADOS ESPECIALES
 * ------------------
 *   de_state_delete (0) : La entidad se elimina. Si tiene destructor,
 *                         se le da una oportunidad de abortar.
 *   de_state_loop   (1) : Valor magico. update() no modifica e->state.
 *   de_state_pause  (2) : La entidad se mueve a la zona pausada.
 *   >2                  : Puntero a funcion de estado activo.
 *
 * PAUSE / RESUME
 * --------------
 *   de_entity_pause(e)  : Mueve 'e' a la particion pausada [0, pause_index).
 *   de_entity_resume(e) : Mueve 'e' a la particion activa [pause_index, size).
 *   de_manager_pause(m) : Pausa TODAS las entidades (pause_index = size).
 *   de_manager_resume(m): Activa TODAS las entidades (pause_index = 0).
 *
 *   La particion es O(1) por swap de punteros. El orden relativo dentro
 *   de cada particion NO esta garantizado tras swaps.
 *
 * DELETE
 * ------
 *   Via estado: la entidad retorna de_state_delete en su update.
 *               El borrado fisico ocurre en el SIGUIENTE frame.
 *   Directo:    de_entity_delete(e) fuerza el estado y compacta inmediatamente.
 *
 *   Si e->destructor != NULL, se llama antes de borrar. El destructor
 *   puede ABORTAR el borrado retornando un estado distinto de delete.
 *
 * ITERACION
 * ---------
 *   de_manager_iterate(m, { ... });    // Solo entidades ACTIVAS
 *   de_manager_iterateAll(m, { ... }); // Todas las entidades
 *
 *   Dentro del bloque CODE se define automaticamente:
 *          de_entity *ENTITY = entidad_actual;
 *          uint16_t   INDEX  = indice en el array;
 *
 * APPLY (filtrado seguro)
 * -----------------------
 *   de_manager_applyAll(m, FILTRO, ACCION);
 *
 *   Primero acumula en un array auxiliar las entidades que cumplen
 *   FILTRO, luego ejecuta ACCION sobre cada una. Es seguro usar
 *   de_entity_delete como ACCION porque el filtrado termina antes
 *   de tocar el manager.
 *
 *   OJO: El array auxiliar es un VLA en stack. En hardware real con
 *   stack limitado, no uses applyAll con managers de >200 entidades.
 *
 * SISTEMAS (DARKEN_SYSTEMS)
 * -------------------------
 *   Capa opcional sobre darken.h para procesar entidades por lotes.
 *
 *   DE_SYSTEM(name, payload_type, code)
 *       Define una funcion void name(de_manager*) que itera SOLO
 *       entidades activas, inyectando 'data' casteado al payload.
 *
 *   DE_SYSTEM_ALL(name, payload_type, code)
 *       Igual pero itera TODAS las entidades (activas + pausadas).
 *
 *   DE_SYSTEM_TAG(name, payload_type, tag_value, code)
 *       Solo procesa entidades donde ENTITY->tag == tag_value.
 *
 *   de_pipeline_init(&pipe, array, capacity)
 *   de_pipeline_add(&pipe, sys_func)
 *   de_pipeline_run(&pipe, &mgr)
 *       Ejecuta sistemas en orden. Ideal para separar fisica,
 *       movimiento, animacion y colisiones en pasadas distintas.
 *
 * REGLAS DE ALINEACION (m68k)
 * ---------------------------
 *   El Motorola 68000 exige que los accesos LONG (32-bit, punteros)
 *   caigan en direcciones multiplo de 4. darken.h fuerza:
 *     - stride de entidad redondeado a 4 (_DE_ALIGN4).
 *     - buffer de storage alineado a 4 bytes.
 *   Si modificas la estructura de de_entity, manten sizeof() multiplo
 *   de 4 o ajusta _DE_ALIGN4.
 *
 * RENDIMIENTO TIPICO (NTSC 60Hz, 7.6MHz m68k)
 * --------------------------------------------
 *   ~27-45 µs por entidad activa y frame (depende del payload).
 *   ~600 entidades activas maximo por frame sin drop.
 *   de_entity_swap: ~50 µs (operacion base de pause/resume/delete).
 *
 * LIMITES
 * -------
 *   - Capacidad maxima: uint16_t (65535), limitado por RAM.
 *   - No hay zero-fill automatico al reutilizar slots.
 *   - No es thread-safe (obviamente, es Genesis).
 *   - applyAll usa VLA: cuidado con stack en hardware real.
 *
 * LICENCIA
 * --------
 *   Public domain. Usa, modifica y rompe a tu gusto.
 *
 * ============================================================ */