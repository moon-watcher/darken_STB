#include <genesis.h>

#define DARKEN_IMPLEMENTATION
#include "darken.h"

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

#define CHECK(desc, cond)                 \
    do                                    \
    {                                     \
        g_testsRun++;                     \
        if (cond)                         \
        {                                 \
            g_testsPassed++;              \
            kprintf("  [PASS] %s", desc); \
        }                                 \
        else                              \
        {                                 \
            kprintf("  [FAIL] %s", desc); \
        }                                 \
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

    de_manager_storage(m1_storage, 8, sizeof(struct MyComponent));
    de_manager_init(&m1, de_manager_args(m1_storage));

    de_manager_storage(m2_storage, 8, sizeof(struct MyComponent) + 73);
    de_manager_init(&m2, de_manager_args(m2_storage));

    de_manager_storage(m3_storage, 8, 1);
    de_manager_init(&m3, de_manager_args(m3_storage));

    CHECK("payload par: todos los slots alineados a 4", all_slots_aligned(&m1));
    CHECK("payload impar (+73): todos los slots alineados a 4", all_slots_aligned(&m2));
    CHECK("payload=1: todos los slots alineados a 4", all_slots_aligned(&m3));
}

static void test_creation(void)
{
    kprintf("-- test_creation --");
    de_manager m;

    de_manager_storage(m_storage, 3, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    CHECK("manager empieza con size 0", m.size == 0);
    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);
    de_entity e2 = de_manager_new(&m);
    de_entity e3 = de_manager_new(&m);
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

    de_manager_storage(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity e = de_manager_new(&m);
    struct MyComponent *c = (struct MyComponent *)e->data;
    c->x = 0;
    e->state = (de_state)state_walk;
    de_manager_update(&m);
    de_manager_update(&m);
    de_manager_update(&m);
    CHECK("loop no cambia puntero", e->state == (de_state)state_walk);
    CHECK("loop ejecuta x3", g_walkCalls == 3 && c->x == 3);
    de_entity e2 = de_manager_new(&m);
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

    de_manager_storage(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
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

    de_manager_storage(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    de_entity c = de_manager_new(&m);
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

    de_manager_storage(m_storage, 5, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    for (int i = 0; i < 5; ++i)
    {
        de_entity e = de_manager_new(&m);
        e->tag = i;
    }
    de_manager_applyAll(&m, (ENTITY->tag % 2) == 0, de_entity_delete);
    CHECK("quedan 2 entidades", m.size == 2);
    bool onlyOdd = TRUE;
    de_manager_iterateAll(&m, {
        if ((ENTITY->tag % 2) == 0)
            onlyOdd = FALSE;
    });
    CHECK("quedan tags impares", onlyOdd);
}

static void test_reset(void)
{
    kprintf("-- test_reset --");
    g_destructorCalls = 0;
    de_manager m;

    de_manager_storage(m_storage, 6, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    for (int i = 0; i < 6; ++i)
    {
        de_entity e = de_manager_new(&m);
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

    de_manager_storage(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity e = de_manager_new(&m);
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

    de_manager_storage(m_storage, 3, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
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

    de_manager_storage(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);
    de_entity e2 = de_manager_new(&m);
    de_entity e3 = de_manager_new(&m);
    e0->state = e1->state = e2->state = e3->state = (de_state)state_noop;
    de_entity_pause(e1);
    de_entity_pause(e3);
    bool stable = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
        if (m.items[i]->slot != i)
            stable = FALSE;
    CHECK("slots tras pausar", stable);
    CHECK("pause_index 2", m.pause_index == 2);
    de_entity_delete(e0);
    stable = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
        if (m.items[i]->slot != i)
            stable = FALSE;
    CHECK("slots tras delete", stable);
    CHECK("size 3", m.size == 3);
}

static void test_delete_paused(void)
{
    kprintf("-- test_delete_paused --");
    de_manager m;

    de_manager_storage(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    de_entity c = de_manager_new(&m);
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

    de_manager_storage(m_storage, 6, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    for (int i = 0; i < 6; ++i)
    {
        de_entity e = de_manager_new(&m);
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
        if (shouldBePaused != isPaused)
            correct = FALSE;
    });
    CHECK("apply pause: pares pausadas", correct);
}

static void test_reuse(void)
{
    kprintf("-- test_reuse --");
    de_manager m;

    de_manager_storage(m_storage, 3, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity e0 = de_manager_new(&m);
    e0->state = (de_state)state_noop;
    struct MyComponent *d0 = (struct MyComponent *)e0->data;
    d0->x = 42;
    de_entity_delete(e0);
    de_entity e1 = de_manager_new(&m);
    CHECK("reuse: not null", e1 != 0);
    CHECK("reuse: slot 0", e1->slot == 0);
    CHECK("reuse: size 1", m.size == 1);
    struct MyComponent *d1 = (struct MyComponent *)e1->data;
    CHECK("reuse: datos antiguos persisten", d1->x == 42);
}

static int g_stressCallsA = 0, g_stressCallsB = 0;
static void *state_stress_a(void *data)
{
    (void)data;
    ++g_stressCallsA;
    return (void *)de_state_loop;
}
static void *state_stress_b(void *data)
{
    (void)data;
    ++g_stressCallsB;
    return (void *)de_state_loop;
}

static void test_mixed_stress(void)
{
    kprintf("-- test_mixed_stress --");
    g_stressCallsA = 0;
    g_stressCallsB = 0;
    de_manager m;

    de_manager_storage(m_storage, 5, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity e[5];
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
    g_stressCallsA = 0;
    g_stressCallsB = 0;
    de_manager_update(&m);
    CHECK("stress f2: 3 activas", g_stressCallsA + g_stressCallsB == 3);
    CHECK("stress f2: size 4", m.size == 4);
    de_entity_resume(e[1]);
    g_stressCallsA = 0;
    g_stressCallsB = 0;
    de_manager_update(&m);
    CHECK("stress f3: 4 activas", g_stressCallsA + g_stressCallsB == 4);
    bool ok = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
        if (m.items[i]->slot != i)
            ok = FALSE;
    CHECK("stress: slots ok", ok);
}

static void test_data_integrity(void)
{
    kprintf("-- test_data_integrity --");
    de_manager m;

    de_manager_storage(m_storage, 3, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    de_entity c = de_manager_new(&m);
    struct MyComponent *da = (struct MyComponent *)a->data;
    struct MyComponent *db = (struct MyComponent *)b->data;
    struct MyComponent *dc = (struct MyComponent *)c->data;
    da->x = 111;
    db->x = 333;
    dc->x = 555;
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

    de_manager_storage(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

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

    de_manager_storage(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity e = de_manager_new(&m);
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

    de_manager_storage(m_storage, 50, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity ents[50];
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
        de_entity e = de_manager_new(&m);
        CHECK("rellenar huecos", e != 0);
    }
    CHECK("size final 35", m.size == 35);

    bool ok = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
    {
        if (m.items[i]->slot != i)
            ok = FALSE;
        if (m.items[i]->manager != &m)
            ok = FALSE;
    }
    CHECK("slots coherentes tras fragmentacion", ok);
}

static void test_stress_many_entities(void)
{
    kprintf("-- test_stress_many_entities --");
    de_manager m;

    de_manager_storage(m_storage, 150, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity ents[150];
    for (int i = 0; i < 150; ++i)
    {
        ents[i] = de_manager_new(&m);
        ents[i]->state = (de_state)state_noop;
    }
    CHECK("many: size 150", m.size == 150);

    for (int i = 0; i < 150; i += 2)
        de_entity_pause(ents[i]);

    CHECK("many: pause_index 75", m.pause_index == 75);

    de_manager_update(&m);

    bool ok = TRUE;
    for (int i = 0; i < 150; i += 2)
        if (ents[i]->slot >= m.pause_index)
            ok = FALSE;
    CHECK("many: pares siguen pausados", ok);

    bool ok2 = TRUE;
    for (int i = 1; i < 150; i += 2)
        if (ents[i]->slot < m.pause_index)
            ok2 = FALSE;
    CHECK("many: impares siguen activas", ok2);
}

static void test_stress_fragmentation(void)
{
    kprintf("-- test_stress_fragmentation --");
    de_manager m;

    de_manager_storage(m_storage, 20, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity e[20];
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
        de_entity ne = de_manager_new(&m);
        ne->tag = 100 + i;
        ne->state = (de_state)state_noop;
    }
    CHECK("frag: size 15 tras recrear", m.size == 15);

    bool unique = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
    {
        for (uint16_t j = i + 1; j < m.size; ++j)
        {
            if (m.items[i]->tag == m.items[j]->tag)
                unique = FALSE;
        }
    }
    CHECK("frag: tags unicos tras recreacion", unique);
}

/* ============================================================
 * TESTS 26+: SISTEMAS COMO ENTIDADES DE DARKEN
 * ============================================================ */

typedef struct TestSystemEntity
{
    int16_t x, y;
    int16_t vx, vy;
    uint16_t frame;
} TestSystemEntity;

typedef struct TestBatchSystem
{
    void **items;
    uint16_t size;
} TestBatchSystem;

static void *test_system_physics(void *data)
{
    TestBatchSystem *system = (TestBatchSystem *)data;

    uint16_t i = 0;
    while (i < system->size)
    {
        int16_t *vy = (int16_t *)system->items[i++];
        *vy += 1;
    }

    return (void *)de_state_loop;
}

static void *test_system_movement(void *data)
{
    TestBatchSystem *system = (TestBatchSystem *)data;

    uint16_t i = 0;
    while (i < system->size)
    {
        int16_t *x = (int16_t *)system->items[i++];
        int16_t *y = (int16_t *)system->items[i++];
        int16_t *vx = (int16_t *)system->items[i++];
        int16_t *vy = (int16_t *)system->items[i++];

        *x += *vx;
        *y += *vy;
    }

    return (void *)de_state_loop;
}

static void *test_system_frames(void *data)
{
    TestBatchSystem *system = (TestBatchSystem *)data;

    uint16_t i = 0;
    while (i < system->size)
    {
        uint16_t *frame = (uint16_t *)system->items[i++];
        *frame += 1;
    }

    return (void *)de_state_loop;
}

static void test_entity_system_basic(void)
{
    kprintf("-- test_entity_system_basic --");

    de_manager entities;
    de_manager systems;

    de_manager_storage(entities_storage, 3, sizeof(TestSystemEntity));
    de_manager_init(&entities, de_manager_args(entities_storage));
    de_manager_storage(systems_storage, 3, sizeof(TestBatchSystem));
    de_manager_init(&systems, de_manager_args(systems_storage));

    void *physics_items[3];
    void *movement_items[12];
    void *frame_items[3];

    de_entity frames_entity = de_manager_new(&systems);
    de_entity movement_entity = de_manager_new(&systems);
    de_entity physics_entity = de_manager_new(&systems);

    TestBatchSystem *physics_pool = (TestBatchSystem *)physics_entity->data;
    TestBatchSystem *movement_pool = (TestBatchSystem *)movement_entity->data;
    TestBatchSystem *frame_pool = (TestBatchSystem *)frames_entity->data;

    physics_pool->items = physics_items;
    physics_pool->size = 0;
    movement_pool->items = movement_items;
    movement_pool->size = 0;
    frame_pool->items = frame_items;
    frame_pool->size = 0;

    physics_entity->state = (de_state)test_system_physics;
    movement_entity->state = (de_state)test_system_movement;
    frames_entity->state = (de_state)test_system_frames;

    de_entity p1_entity = de_manager_new(&entities);
    de_entity p2_entity = de_manager_new(&entities);

    TestSystemEntity *p1 = (TestSystemEntity *)p1_entity->data;
    TestSystemEntity *p2 = (TestSystemEntity *)p2_entity->data;

    p1->x = 10;
    p1->y = 20;
    p1->vx = 2;
    p1->vy = 3;
    p1->frame = 0;

    p2->x = 100;
    p2->y = 200;
    p2->vx = -4;
    p2->vy = 5;
    p2->frame = 10;

    p1_entity->state = (de_state)state_noop;
    p2_entity->state = (de_state)state_noop;

    physics_items[physics_pool->size++] = &p1->vy;
    physics_items[physics_pool->size++] = &p2->vy;

    movement_items[movement_pool->size++] = &p1->x;
    movement_items[movement_pool->size++] = &p1->y;
    movement_items[movement_pool->size++] = &p1->vx;
    movement_items[movement_pool->size++] = &p1->vy;
    movement_items[movement_pool->size++] = &p2->x;
    movement_items[movement_pool->size++] = &p2->y;
    movement_items[movement_pool->size++] = &p2->vx;
    movement_items[movement_pool->size++] = &p2->vy;

    frame_items[frame_pool->size++] = &p1->frame;
    frame_items[frame_pool->size++] = &p2->frame;

    de_manager_update(&systems);

    CHECK("entity systems: physics modifica vy",
          p1->vy == 4 && p2->vy == 6);

    CHECK("entity systems: movement procesa ambas entidades",
          p1->x == 12 && p1->y == 24 &&
              p2->x == 96 && p2->y == 206);

    CHECK("entity systems: frames procesa ambas entidades",
          p1->frame == 1 && p2->frame == 11);

    CHECK("entity systems: manager de sistemas tiene 3 entidades",
          systems.size == 3);

    CHECK("entity systems: entidades normales siguen intactas",
          entities.size == 2);
}

static void test_entity_system_shared_data(void)
{
    kprintf("-- test_entity_system_shared_data --");

    de_manager entities;
    de_manager systems;

    de_manager_storage(entities_storage, 2, sizeof(TestSystemEntity));
    de_manager_init(&entities, de_manager_args(entities_storage));
    de_manager_storage(systems_storage, 2, sizeof(TestBatchSystem));
    de_manager_init(&systems, de_manager_args(systems_storage));

    void *movement_items[8];
    void *frame_items[2];

    de_entity movement = de_manager_new(&systems);
    de_entity frames = de_manager_new(&systems);

    TestBatchSystem *movement_system = (TestBatchSystem *)movement->data;
    TestBatchSystem *frame_system = (TestBatchSystem *)frames->data;

    movement_system->items = movement_items;
    movement_system->size = 0;
    frame_system->items = frame_items;
    frame_system->size = 0;

    movement->state = (de_state)test_system_movement;
    frames->state = (de_state)test_system_frames;

    de_entity e = de_manager_new(&entities);
    TestSystemEntity *data = (TestSystemEntity *)e->data;

    data->x = 50;
    data->y = 60;
    data->vx = 7;
    data->vy = -2;
    data->frame = 3;
    e->state = (de_state)state_noop;

    movement_items[movement_system->size++] = &data->x;
    movement_items[movement_system->size++] = &data->y;
    movement_items[movement_system->size++] = &data->vx;
    movement_items[movement_system->size++] = &data->vy;
    frame_items[frame_system->size++] = &data->frame;

    de_manager_update(&systems);

    CHECK("shared data: movimiento usa x/y/vx/vy",
          data->x == 57 && data->y == 58);
    CHECK("shared data: frames usa el mismo payload",
          data->frame == 4);
}

static void test_entity_system_paused_entity(void)
{
    kprintf("-- test_entity_system_paused_entity --");

    de_manager entities;
    de_manager systems;

    de_manager_storage(entities_storage, 2, sizeof(TestSystemEntity));
    de_manager_init(&entities, de_manager_args(entities_storage));
    de_manager_storage(systems_storage, 1, sizeof(TestBatchSystem));
    de_manager_init(&systems, de_manager_args(systems_storage));

    void *movement_items[8];
    de_entity system_entity = de_manager_new(&systems);
    TestBatchSystem *system = (TestBatchSystem *)system_entity->data;
    system->items = movement_items;
    system->size = 0;
    system_entity->state = (de_state)test_system_movement;

    de_entity active_entity = de_manager_new(&entities);
    de_entity paused_entity = de_manager_new(&entities);

    TestSystemEntity *active = (TestSystemEntity *)active_entity->data;
    TestSystemEntity *paused = (TestSystemEntity *)paused_entity->data;

    active->x = 10;
    active->y = 20;
    active->vx = 1;
    active->vy = 2;
    paused->x = 100;
    paused->y = 200;
    paused->vx = 3;
    paused->vy = 4;

    active_entity->state = (de_state)state_noop;
    paused_entity->state = (de_state)state_noop;

    movement_items[system->size++] = &active->x;
    movement_items[system->size++] = &active->y;
    movement_items[system->size++] = &active->vx;
    movement_items[system->size++] = &active->vy;
    movement_items[system->size++] = &paused->x;
    movement_items[system->size++] = &paused->y;
    movement_items[system->size++] = &paused->vx;
    movement_items[system->size++] = &paused->vy;

    de_entity_pause(paused_entity);

    de_manager_update(&systems);

    CHECK("paused entity: sigue en el pool del sistema",
          paused->x == 103 && paused->y == 204);
    CHECK("paused entity: active tambien se procesa",
          active->x == 11 && active->y == 22);
    CHECK("paused entity: sigue pausada en su manager",
          paused_entity->slot < entities.pause_index);
}

/* ============================================================
 * TESTS ADICIONALES: de_system como pool de campos
 * ============================================================ */

typedef struct TestDeSystemEntity
{
    int16_t x, y;
    int16_t vx, vy;
    uint16_t frame;
} TestDeSystemEntity;

static de_system_iterator(test_de_system_movement, int16_t *x, int16_t *y, int16_t *vx, int16_t *vy, {
    *x += *vx;
    *y += *vy;
});

static de_system_iterator(test_de_system_physics, int16_t *vy, {
    *vy += 1;
});

static de_system_iterator(test_de_system_frames, uint16_t *frame, {
    *frame += 1;
});

static void test_de_system_init_add(void)
{
    kprintf("-- test_de_system_init_add --");
    de_system sys;
    void *pool[12];
    de_system_init(&sys, pool, 3, 4);
    CHECK("de_system init: size 0", sys.size == 0);
    CHECK("de_system init: params 4", sys.params == 4);
    CHECK("de_system init: capacity 12", sys.capacity == 12);
    int a, b, c, d;
    CHECK("de_system add: primer grupo", de_system_add(&sys, &a, &b, &c, &d) == 1);
    CHECK("de_system add: size 4", sys.size == 4);
    CHECK("de_system add: punteros conservados", sys.pool[0] == &a && sys.pool[1] == &b && sys.pool[2] == &c && sys.pool[3] == &d);
}

static void test_de_system_multiple_groups(void)
{
    kprintf("-- test_de_system_multiple_groups --");
    de_system sys;
    void *pool[12];
    de_system_init(&sys, pool, 3, 4);
    int a1, b1, c1, d1, a2, b2, c2, d2;
    de_system_add(&sys, &a1, &b1, &c1, &d1);
    de_system_add(&sys, &a2, &b2, &c2, &d2);
    CHECK("de_system: dos grupos", sys.size == 8);
    CHECK("de_system: grupo 1 intacto", sys.pool[0] == &a1 && sys.pool[1] == &b1 && sys.pool[2] == &c1 && sys.pool[3] == &d1);
    CHECK("de_system: grupo 2 intacto", sys.pool[4] == &a2 && sys.pool[5] == &b2 && sys.pool[6] == &c2 && sys.pool[7] == &d2);
}

static void test_de_system_remove(void)
{
    kprintf("-- test_de_system_remove --");
    de_system sys;
    void *pool[12];
    de_system_init(&sys, pool, 3, 4);
    int a1, b1, c1, d1, a2, b2, c2, d2, a3, b3, c3, d3;
    de_system_add(&sys, &a1, &b1, &c1, &d1);
    de_system_add(&sys, &a2, &b2, &c2, &d2);
    de_system_add(&sys, &a3, &b3, &c3, &d3);
    CHECK("de_system remove: encuentra grupo", de_system_remove(&sys, &a2) == 1);
    CHECK("de_system remove: size 8", sys.size == 8);
    CHECK("de_system remove: grupo final compactado", sys.pool[0] == &a1 && sys.pool[1] == &b1 && sys.pool[2] == &c1 && sys.pool[3] == &d1 && sys.pool[4] == &a3 && sys.pool[5] == &b3 && sys.pool[6] == &c3 && sys.pool[7] == &d3);
    CHECK("de_system remove: no encuentra grupo ausente", de_system_remove(&sys, &a2) == 0);
}

static void test_de_system_capacity(void)
{
    kprintf("-- test_de_system_capacity --");
    de_system sys;
    void *pool[8];
    de_system_init(&sys, pool, 2, 4);
    int a[2], b[2], c[2], d[2];
    CHECK("de_system capacity: primer grupo", de_system_add(&sys, &a[0], &a[1], &b[0], &b[1]) == 1);
    CHECK("de_system capacity: segundo grupo", de_system_add(&sys, &b[0], &b[1], &c[0], &c[1]) == 1);
    CHECK("de_system capacity: rechaza grupo lleno", de_system_add(&sys, &c[0], &c[1], &d[0], &d[1]) == 0);
    CHECK("de_system capacity: size no cambia", sys.size == 8);
}

static void test_de_system_as_entities(void)
{
    kprintf("-- test_de_system_as_entities --");

    de_manager entities, systems;

    de_manager_storage(entities_storage, 2, sizeof(TestDeSystemEntity));
    de_manager_init(&entities, de_manager_args(entities_storage));
    de_manager_storage(systems_storage, 3, sizeof(de_system));
    de_manager_init(&systems, de_manager_args(systems_storage));

    de_entity frames_entity = de_manager_new(&systems);
    de_entity movement_entity = de_manager_new(&systems);
    de_entity physics_entity = de_manager_new(&systems);

    de_system *frames = (de_system *)frames_entity->data;
    de_system *movement = (de_system *)movement_entity->data;
    de_system *physics = (de_system *)physics_entity->data;

    de_system_create(frames, 2, 1);
    de_system_create(movement, 2, 4);
    de_system_create(physics, 2, 1);

    frames_entity->state = (de_state)test_de_system_frames;
    movement_entity->state = (de_state)test_de_system_movement;
    physics_entity->state = (de_state)test_de_system_physics;

    de_entity e1 = de_manager_new(&entities);
    de_entity e2 = de_manager_new(&entities);
    TestDeSystemEntity *p1 = (TestDeSystemEntity *)e1->data;
    TestDeSystemEntity *p2 = (TestDeSystemEntity *)e2->data;

    p1->x = 10;
    p1->y = 20;
    p1->vx = 2;
    p1->vy = 3;
    p1->frame = 0;
    p2->x = 100;
    p2->y = 200;
    p2->vx = -4;
    p2->vy = 5;
    p2->frame = 10;

    e1->state = (de_state)state_noop;
    e2->state = (de_state)state_noop;

    de_system_add(movement, &p1->x, &p1->y, &p1->vx, &p1->vy);
    de_system_add(movement, &p2->x, &p2->y, &p2->vx, &p2->vy);
    de_system_add(physics, &p1->vy);
    de_system_add(physics, &p2->vy);
    de_system_add(frames, &p1->frame);
    de_system_add(frames, &p2->frame);

    de_manager_update(&systems);

    CHECK("de_system entities: physics modifica vy", p1->vy == 4 && p2->vy == 6);
    CHECK("de_system entities: movement procesa ambas", p1->x == 12 && p1->y == 24 && p2->x == 96 && p2->y == 206);
    CHECK("de_system entities: frames procesa ambas", p1->frame == 1 && p2->frame == 11);
    CHECK("de_system entities: manager contiene 3 sistemas", systems.size == 3);
    CHECK("de_system entities: manager de entidades intacto", entities.size == 2);
}

static void test_de_system_shared_payload(void)
{
    kprintf("-- test_de_system_shared_payload --");
    de_manager entities, systems;

    de_manager_storage(entities_storage, 1, sizeof(TestDeSystemEntity));
    de_manager_init(&entities, de_manager_args(entities_storage));
    de_manager_storage(systems_storage, 2, sizeof(de_system));
    de_manager_init(&systems, de_manager_args(systems_storage));

    void *movement_pool[4], *frames_pool[1];
    de_entity movement_entity = de_manager_new(&systems), frames_entity = de_manager_new(&systems);
    de_system *movement = (de_system *)movement_entity->data, *frames = (de_system *)frames_entity->data;
    de_system_init(movement, movement_pool, 1, 4);
    de_system_init(frames, frames_pool, 1, 1);
    movement_entity->state = (de_state)test_de_system_movement;
    frames_entity->state = (de_state)test_de_system_frames;
    de_entity entity = de_manager_new(&entities);
    TestDeSystemEntity *data = (TestDeSystemEntity *)entity->data;
    data->x = 50;
    data->y = 60;
    data->vx = 7;
    data->vy = -2;
    data->frame = 3;
    de_system_add(movement, &data->x, &data->y, &data->vx, &data->vy);
    de_system_add(frames, &data->frame);
    de_manager_update(&systems);
    CHECK("de_system shared: movimiento modifica payload", data->x == 57 && data->y == 58);
    CHECK("de_system shared: frames usa el mismo payload", data->frame == 4);
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
    test_de_system_init_add();
    test_de_system_multiple_groups();
    test_de_system_remove();
    test_de_system_capacity();
    test_de_system_as_entities();
    test_de_system_shared_payload();
    test_entity_system_basic();
    test_entity_system_shared_data();
    test_entity_system_paused_entity();
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

    de_manager_storage(m_storage, 32, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

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

    de_manager_storage(m_storage, 32, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    for (u16 i = 0; i < 32; ++i)
    {
        de_entity e = de_manager_new(&m);
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

    de_manager_storage(m_storage, 32, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS; ++r)
    {
        for (u16 i = 0; i < 32; ++i)
        {
            de_entity e = de_manager_new(&m);
            e->tag = i;
        }
        de_manager_applyAll(&m, (ENTITY->tag % 2) == 0, de_entity_delete);
        de_manager_reset(&m);
    }
    u32 frames = bench_frames_elapsed(t0);
    kprintf("create32+applyAll(borrar pares)+reset x%d reps: %ld frames", BENCH_REPS, frames);
}

/* Benchmarks estáticos para 128 y 256 entidades */
static void bench_create_destroy_128(void)
{
    de_manager m;

    de_manager_storage(m_storage, 128, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    u32 t0 = bench_start();
    for (u32 r = 0; r < 500; ++r)
    {
        for (u16 i = 0; i < 128; ++i)
            de_manager_new(&m);
        de_manager_reset(&m);
    }
    u32 frames = bench_frames_elapsed(t0);
    kprintf("create+reset 128 entidades x500 reps: %ld frames", frames);
}

static void bench_create_destroy_256(void)
{
    de_manager m;

    de_manager_storage(m_storage, 256, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    u32 t0 = bench_start();
    for (u32 r = 0; r < 250; ++r)
    {
        for (u16 i = 0; i < 256; ++i)
            de_manager_new(&m);
        de_manager_reset(&m);
    }
    u32 frames = bench_frames_elapsed(t0);
    kprintf("create+reset 256 entidades x250 reps: %ld frames", frames);
}

static void bench_update_128(void)
{
    de_manager m;

    de_manager_storage(m_storage, 128, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    for (u16 i = 0; i < 128; ++i)
    {
        de_entity e = de_manager_new(&m);
        e->state = (de_state)bench_state_fn;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < 500; ++r)
        de_manager_update(&m);
    u32 frames = bench_frames_elapsed(t0);
    kprintf("de_manager_update, 128 entidades x500 reps: %ld frames", frames);
}

static void bench_update_256(void)
{
    de_manager m;

    de_manager_storage(m_storage, 256, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    for (u16 i = 0; i < 256; ++i)
    {
        de_entity e = de_manager_new(&m);
        e->state = (de_state)bench_state_fn;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < 250; ++r)
        de_manager_update(&m);
    u32 frames = bench_frames_elapsed(t0);
    kprintf("de_manager_update, 256 entidades x250 reps: %ld frames", frames);
}

#define BENCH_SWAP_REPS 50000

static void bench_swap(void)
{
    de_manager m;

    de_manager_storage(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, de_manager_args(m_storage));

    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    u32 t0 = bench_start();
    // for (u32 r = 0; r < BENCH_SWAP_REPS; ++r)
    //     de_entity_swap(a, b);
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

static void bench_systems_vs_individual(void)
{
    kprintf("========== BENCHMARK SISTEMAS ==========");

    de_manager m_ind;
    de_manager_storage(m_ind_storage, 32, sizeof(struct MyComponent));
    de_manager_init(&m_ind, de_manager_args(m_ind_storage));

    for (u16 i = 0; i < 32; ++i)
    {
        de_entity e = de_manager_new(&m_ind);
        e->state = (de_state)bench_system_state;
    }
    u32 t0 = bench_start();
    for (u32 r = 0; r < BENCH_REPS; ++r)
        de_manager_update(&m_ind);
    u32 frames_ind = bench_frames_elapsed(t0);
    kprintf("update INDIVIDUAL (32 ent x%d reps): %ld frames", BENCH_REPS, frames_ind);

    de_manager m_sys;
    de_manager_storage(m_sys_storage, 32, sizeof(struct MyComponent));
    de_manager_init(&m_sys, de_manager_args(m_sys_storage));

    for (u16 i = 0; i < 32; ++i)
    {
        de_entity e = de_manager_new(&m_sys);
        e->state = (de_state)state_noop;
    }
    kprintf("=======================================");
}

/* ============================================================
 * BENCHMARK DE MEMORIA
 * ============================================================ */

static void bench_memory_overhead(void)
{
    kprintf("========== MEMORIA ==========");

    u16 stride16 = de_entity_stride(16);
    u16 stride32 = de_entity_stride(32);
    u16 stride1 = de_entity_stride(1);
    u16 stride9 = de_entity_stride(9);

    kprintf("sizeof(de_entity) base: %d bytes", sizeof(de_entity));
    kprintf("stride payload=1:  %d bytes/entidad", stride1);
    kprintf("stride payload=9:  %d bytes/entidad (impar)", stride9);
    kprintf("stride payload=16: %d bytes/entidad", stride16);
    kprintf("stride payload=32: %d bytes/entidad", stride32);

    de_manager m32;
    de_manager_storage(m32_storage, 32, sizeof(struct MyComponent));
    de_manager_init(&m32, de_manager_args(m32_storage));
    u32 bytes32 = 32 * de_entity_stride(sizeof(struct MyComponent));
    kprintf("Manager 32 entidades (payload %d): %ld bytes en storage",
            sizeof(struct MyComponent), bytes32);

    de_manager m128;
    de_manager_storage(m128_storage, 128, sizeof(struct MyComponent));
    de_manager_init(&m128, de_manager_args(m128_storage));
    u32 bytes128 = 128 * de_entity_stride(sizeof(struct MyComponent));
    kprintf("Manager 128 entidades (payload %d): %ld bytes en storage",
            sizeof(struct MyComponent), bytes128);

    u16 overhead = de_entity_stride(sizeof(struct MyComponent)) - sizeof(struct MyComponent);
    kprintf("Overhead por entidad: %d bytes (header + padding)", overhead);

    u32 ramAvailable = 64 * 1024;
    u32 entidadesEn64k = ramAvailable / de_entity_stride(sizeof(struct MyComponent));
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

    de_manager_storage(g_manager_storage, 10, sizeof(struct MyComponent));
    de_manager_init(&g_manager, de_manager_args(g_manager_storage));

    de_manager_storage(g_manager2_storage, 20, sizeof(struct MyComponent) + 73);
    de_manager_init(&g_manager2, de_manager_args(g_manager2_storage));

    de_entity e1 = de_manager_new(&g_manager);
    struct MyComponent *data1 = (struct MyComponent *)e1->data;
    data1->x = 0;
    data1->y = 0;
    data1->health = 100;
    e1->state = (de_state)update_walk;
    e1->destructor = (de_state)destructor;
    e1->tag = 1;

    de_entity e2 = de_manager_new(&g_manager);
    struct MyComponent *data2 = (struct MyComponent *)e2->data;
    data2->x = 10;
    data2->y = 20;
    data2->health = 80;
    e2->state = (de_state)update_idle;
    e2->destructor = (de_state)destructor;
    e2->tag = 2;

    de_entity e3 = de_manager_new(&g_manager);
    struct MyComponent *data3 = (struct MyComponent *)e3->data;
    data3->x = 5;
    data3->y = 5;
    data3->health = 50;
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
 * MAIN TESTS
 * ============================================================ */

void test(void)
{
    BLASTEM_PROFIL_START
    SYS_setVIntCallback(bench_vblank_cb);

    run_usage_example();
    run_all_tests();
    bench_memory_overhead();

    kprintf("========== BENCHMARKS ==========");
    bench_create_destroy();
    bench_update();
    bench_apply();
    bench_create_destroy_128();
    bench_create_destroy_256();
    bench_update_128();
    bench_update_256();
    bench_swap();
    bench_systems_vs_individual();
    kprintf("=================================");
    BLASTEM_PROFIL_END
}

int main(void)
{
    test();

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
 *          de_manager_storage(mgr_storage, CAPACIDAD, sizeof(MiPayload));
 *          de_manager_init(&mgr, de_manager_args(mgr_storage));
 *
 *   3. Crear entidades:
 *          de_entity e = de_manager_new(&mgr);
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
 *          de_entity ENTITY = entidad_actual;
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