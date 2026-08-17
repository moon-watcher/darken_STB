#include <genesis.h>
#include "../darken.h"
#include "tests.h"

struct MyComponent
{
    int x, y;
    uint8_t health;
};

static u16 g_testsRun = 0, g_testsPassed = 0;

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

static void test_alignment(void)
{
    kprintf("-- test_alignment --");
    de_manager m1, m2, m3;
    DE_MANAGER_STORAGE(m1_storage, 8, sizeof(struct MyComponent));
    de_manager_init(&m1, DE_MANAGER_ARGS(m1_storage));
    DE_MANAGER_STORAGE(m2_storage, 8, sizeof(struct MyComponent) + 73);
    de_manager_init(&m2, DE_MANAGER_ARGS(m2_storage));
    DE_MANAGER_STORAGE(m3_storage, 8, 1);
    de_manager_init(&m3, DE_MANAGER_ARGS(m3_storage));
    CHECK("payload par: todos los slots alineados a 4", all_slots_aligned(&m1));
    CHECK("payload impar (+73): todos los slots alineados a 4", all_slots_aligned(&m2));
    CHECK("payload=1: todos los slots alineados a 4", all_slots_aligned(&m3));
}

static void test_creation(void)
{
    kprintf("-- test_creation --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 3, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    CHECK("manager empieza con size 0", m.active_count == 0);
    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);
    de_entity e2 = de_manager_new(&m);
    de_entity e3 = de_manager_new(&m);
    CHECK("new valida (1)", e0 != 0);
    CHECK("new valida (2)", e1 != 0);
    CHECK("new valida (3)", e2 != 0);
    CHECK("new devuelve 0 al llenarse", e3 == 0);
    CHECK("size == capacity", m.active_count == 3);
    CHECK("arranca en state delete", e0->state == DE_STATE_DELETE);
    CHECK("arranca con tag 0", e0->tag == 0);
}

static int g_walkCalls = 0;
static void *state_walk(void *data)
{
    struct MyComponent *c = (struct MyComponent *)data;
    c->x += 1;
    ++g_walkCalls;
    kprintf("------------walk------------");
    return DE_STATE_LOOP;
}
static void *state_once_then_idle(void *data)
{
    (void)data;
    return state_walk;
}

static void test_update(void)
{
    kprintf("-- test_update --");
    g_walkCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
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
    return DE_STATE_LOOP;
}

static void test_pause_resume(void)
{
    kprintf("-- test_pause_resume --");
    g_idleCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    a->state = (de_state)state_idle_counter;
    b->state = (de_state)state_idle_counter;
    de_manager_update(&m);
    CHECK("ambas activas: 2 llamadas", g_idleCalls == 2);
    de_entity_pause(b);
    CHECK("b queda en zona pausada", b->slot >= m.paused_start);
    g_idleCalls = 0;
    de_manager_update(&m);
    CHECK("con b pausada: 1 llamada", g_idleCalls == 1);
    de_entity_resume(b);
    CHECK("b vuelve a zona activa", b->slot < m.paused_start);
    g_idleCalls = 0;
    de_manager_update(&m);
    CHECK("resumir: 2 llamadas", g_idleCalls == 2);
}

static int g_destructorCalls = 0;
static void *my_destructor(void *data)
{
    (void)data;
    ++g_destructorCalls;
    return DE_STATE_DELETE;
}
static void *state_noop(void *data)
{
    (void)data;
    return DE_STATE_LOOP;
}

static void test_delete(void)
{
    kprintf("-- test_delete --");
    g_destructorCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    de_entity c = de_manager_new(&m);
    a->destructor = (de_state)my_destructor;
    b->destructor = (de_state)my_destructor;
    c->destructor = (de_state)my_destructor;
    b->state = (de_state)state_noop;
    c->state = (de_state)state_noop;
    a->state = DE_STATE_DELETE;
    de_manager_update(&m);
    CHECK("delete via estado: size 2", m.active_count == 2);
    CHECK("delete via estado: destructor 1 vez", g_destructorCalls == 1);
    de_entity_delete(b);
    CHECK("delete directo: size 1", m.active_count == 1);
    CHECK("delete directo: destructor 2 veces", g_destructorCalls == 2);
}

static void test_apply(void)
{
    kprintf("-- test_apply --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 5, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    for (int i = 0; i < 5; ++i)
    {
        de_entity e = de_manager_new(&m);
        e->tag = i;
    }

    // Iteración segura para borrar con swap-remove
    uint16_t idx = 0;
    while (idx < m.active_count)
    {
        de_entity e = m.items[idx];
        if (e->tag % 2 == 0)
        {
            de_entity_delete(e);
            // No incrementamos idx: la entidad movida al slot actual debe comprobarse
        }
        else
        {
            ++idx;
        }
    }

    CHECK("quedan 2 entidades", m.active_count == 2);
    bool onlyOdd = TRUE;
    for (uint16_t i = 0; i < m.active_count; ++i)
    {
        if ((m.items[i]->tag % 2) == 0)
            onlyOdd = FALSE;
    }
    CHECK("quedan tags impares", onlyOdd);
}

static void test_reset(void)
{
    kprintf("-- test_reset --");
    g_destructorCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 6, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    for (int i = 0; i < 6; ++i)
    {
        de_entity e = de_manager_new(&m);
        e->destructor = (de_state)my_destructor;
    }
    de_manager_reset(&m);
    CHECK("reset: size 0", m.active_count == 0);
    CHECK("reset: destructor 6 veces", g_destructorCalls == 6);
}

static int g_abortDestructorCalls = 0;
static void *state_abort_destructor(void *data)
{
    (void)data;
    ++g_abortDestructorCalls;
    return DE_STATE_LOOP;
}

static void test_destructor_abort(void)
{
    kprintf("-- test_destructor_abort --");
    g_abortDestructorCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    e->destructor = (de_state)state_abort_destructor;
    e->state = DE_STATE_DELETE;
    de_manager_update(&m);
    CHECK("abort: size 1", m.active_count == 1);
    CHECK("abort: destructor llamado", g_abortDestructorCalls == 1);
    CHECK("abort: entidad viva", e->slot < m.active_count);
    CHECK("abort: estado a loop", e->state == DE_STATE_LOOP);
}

static int g_selfKillCalls = 0;
static void *state_self_kill(void *data)
{
    (void)data;
    ++g_selfKillCalls;
    return DE_STATE_DELETE;
}

static void test_self_delete(void)
{
    kprintf("-- test_self_delete --");
    g_selfKillCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 3, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    a->state = (de_state)state_self_kill;
    b->state = (de_state)state_noop;
    de_manager_update(&m);
    CHECK("self-del f1: ejecutado 1 vez", g_selfKillCalls == 1);
    CHECK("self-del f1: marcada deleted", a->state == DE_STATE_DELETE);
    CHECK("self-del f1: size aun 2", m.active_count == 2);
    de_manager_update(&m);
    CHECK("self-del f2: size 1", m.active_count == 1);
    CHECK("self-del f2: queda b", m.items[0] == b);
}

static void test_slot_stability(void)
{
    kprintf("-- test_slot_stability --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);
    de_entity e2 = de_manager_new(&m);
    de_entity e3 = de_manager_new(&m);
    e0->state = e1->state = e2->state = e3->state = (de_state)state_noop;
    de_entity_pause(e1);
    de_entity_pause(e3);
    bool stable = TRUE;
    for (uint16_t i = 0; i < m.active_count; ++i)
        if (m.items[i]->slot != i)
            stable = FALSE;
    CHECK("slots tras pausar", stable);
    CHECK("paused_start 2", m.paused_start == 2);
    de_entity_delete(e0);
    stable = TRUE;
    for (uint16_t i = 0; i < m.active_count; ++i)
        if (m.items[i]->slot != i)
            stable = FALSE;
    CHECK("slots tras delete", stable);
    CHECK("size 1", m.active_count == 1);
}

static void test_delete_paused(void)
{
    kprintf("-- test_delete_paused --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    de_entity c = de_manager_new(&m);
    a->state = b->state = c->state = (de_state)state_noop;
    de_entity_pause(b);
    de_entity_delete(b);
    CHECK("del pausado: size 2", m.active_count == 2);
    CHECK("del pausado: paused_start 4", m.paused_start == 4);
    CHECK("del pausado: a activa", a->slot < m.paused_start); 
    CHECK("del pausado: c activa", c->slot < m.paused_start); 
}

static void test_apply_pause(void)
{
    kprintf("-- test_apply_pause --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 6, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity entities[6];
    for (int i = 0; i < 6; ++i)
    {
        entities[i] = de_manager_new(&m);
        entities[i]->tag = i;
        entities[i]->state = (de_state)state_noop;
    }

    // Pausar las pares manualmente (seguro)
    for (int i = 0; i < 6; i += 2)
    {
        de_entity_pause(entities[i]);
    }

    CHECK("apply pause: paused_start 3", m.paused_start == 3);
    CHECK("apply pause: size 3", m.active_count == 3);
    bool correct = TRUE;
    // Verificar que pares están en pausa y impares en activa
    for (int i = 0; i < 6; ++i)
    {
        de_entity e = entities[i];
        bool shouldBePaused = (e->tag % 2) == 0;
        bool isPaused = (e->slot >= m.paused_start);
        if (shouldBePaused != isPaused)
        {
            correct = FALSE;
            break;
        }
    }
    CHECK("apply pause: pares pausadas", correct);
}

static void test_reuse(void)
{
    kprintf("-- test_reuse --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 3, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e0 = de_manager_new(&m);
    e0->state = (de_state)state_noop;
    struct MyComponent *d0 = (struct MyComponent *)e0->data;
    d0->x = 42;
    de_entity_delete(e0);
    de_entity e1 = de_manager_new(&m);
    CHECK("reuse: not null", e1 != 0);
    CHECK("reuse: slot 0", e1->slot == 0);
    CHECK("reuse: size 1", m.active_count == 1);
    struct MyComponent *d1 = (struct MyComponent *)e1->data;
    CHECK("reuse: datos antiguos persisten", d1->x == 42);
}

static int g_stressCallsA = 0, g_stressCallsB = 0;
static void *state_stress_a(void *data)
{
    (void)data;
    ++g_stressCallsA;
    return DE_STATE_LOOP;
}
static void *state_stress_b(void *data)
{
    (void)data;
    ++g_stressCallsB;
    return DE_STATE_LOOP;
}

static void test_mixed_stress(void)
{
    kprintf("-- test_mixed_stress --");
    g_stressCallsA = 0;
    g_stressCallsB = 0;
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 5, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
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
    e[3]->state = DE_STATE_DELETE;
    g_stressCallsA = 0;
    g_stressCallsB = 0;
    de_manager_update(&m);
    CHECK("stress f2: 3 activas", g_stressCallsA + g_stressCallsB == 3);
    CHECK("stress f2: size 3", m.active_count == 3);
    de_entity_resume(e[1]);
    g_stressCallsA = 0;
    g_stressCallsB = 0;
    de_manager_update(&m);
    CHECK("stress f3: 4 activas", g_stressCallsA + g_stressCallsB == 4);
    bool ok = TRUE;
    for (uint16_t i = 0; i < m.active_count; ++i)
        if (m.items[i]->slot != i)
            ok = FALSE;
    CHECK("stress: slots ok", ok);
}

static void test_data_integrity(void)
{
    kprintf("-- test_data_integrity --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 3, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
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
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    CHECK("empty: size 0", m.active_count == 0);
    de_manager_update(&m);
    CHECK("empty: size sigue 0", m.active_count == 0);
    CHECK("empty: paused_start 4", m.paused_start == 4);
}

static void test_delete_last(void)
{
    kprintf("-- test_delete_last --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    e->state = (de_state)state_noop;
    de_entity_delete(e);
    CHECK("last: size 0", m.active_count == 0);
    CHECK("last: paused_start 4", m.paused_start == 4);
}

static void test_stress_capacity(void)
{
    kprintf("-- test_stress_capacity --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 50, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity ents[50];
    for (int i = 0; i < 50; ++i)
    {
        ents[i] = de_manager_new(&m);
        CHECK("llenado completo", ents[i] != 0);
    }
    CHECK("capacity 50", m.active_count == 50);
    for (int i = 0; i < 50; i += 2)
        de_entity_delete(ents[i]);
    CHECK("tras borrar pares: size 25", m.active_count == 25);
    for (int i = 0; i < 10; ++i)
    {
        de_entity e = de_manager_new(&m);
        CHECK("rellenar huecos", e != 0);
    }
    CHECK("size final 35", m.active_count == 35);
    bool ok = TRUE;
    for (uint16_t i = 0; i < m.active_count; ++i)
    {
        if (m.items[i]->slot != i)
            ok = FALSE;
        if (m.items[i]->manager != &m)
            ok = FALSE;
    }
    CHECK("slots y manager correctos", ok);
}

static void test_stress_many_entities(void)
{
    kprintf("-- test_stress_many_entities --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 20, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e[20];
    for (int i = 0; i < 20; ++i)
    {
        e[i] = de_manager_new(&m);
        e[i]->tag = i;
        e[i]->state = (de_state)state_noop;
    }
    for (int i = 1; i < 20; i += 2)
        de_entity_delete(e[i]);
    CHECK("many: size 10 tras borrar impares", m.active_count == 10);
    for (int i = 0; i < 5; ++i)
    {
        de_entity ne = de_manager_new(&m);
        ne->tag = 100 + i;
        ne->state = (de_state)state_noop;
    }
    CHECK("many: size 15 tras recrear", m.active_count == 15);
    bool unique = TRUE;
    for (uint16_t i = 0; i < m.active_count; ++i)
        for (uint16_t j = i + 1; j < m.active_count; ++j)
            if (m.items[i]->tag == m.items[j]->tag)
                unique = FALSE;
    CHECK("many: tags unicos", unique);
}

static void test_stress_fragmentation(void)
{
    kprintf("-- test_stress_fragmentation --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 20, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e[20];
    for (int i = 0; i < 20; ++i)
    {
        e[i] = de_manager_new(&m);
        e[i]->tag = i;
        e[i]->state = (de_state)state_noop;
    }
    for (int i = 1; i < 20; i += 2)
        de_entity_delete(e[i]);
    CHECK("frag: size 10 tras borrar impares", m.active_count == 10);
    for (int i = 0; i < 5; ++i)
    {
        de_entity ne = de_manager_new(&m);
        ne->tag = 100 + i;
        ne->state = (de_state)state_noop;
    }
    CHECK("frag: size 15 tras recrear", m.active_count == 15);
    bool unique = TRUE;
    for (uint16_t i = 0; i < m.active_count; ++i)
        for (uint16_t j = i + 1; j < m.active_count; ++j)
            if (m.items[i]->tag == m.items[j]->tag)
                unique = FALSE;
    CHECK("frag: tags unicos tras recreacion", unique);
}

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
    return DE_STATE_LOOP;
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
    return DE_STATE_LOOP;
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
    return DE_STATE_LOOP;
}

static void test_entity_system_basic(void)
{
    kprintf("-- test_entity_system_basic --");
    de_manager entities, systems;
    DE_MANAGER_STORAGE(entities_storage, 3, sizeof(TestSystemEntity));
    de_manager_init(&entities, DE_MANAGER_ARGS(entities_storage));
    DE_MANAGER_STORAGE(systems_storage, 3, sizeof(TestBatchSystem));
    de_manager_init(&systems, DE_MANAGER_ARGS(systems_storage));
    void *physics_items[3], *movement_items[12], *frame_items[3];
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
    CHECK("entity systems: physics modifica vy", p1->vy == 4 && p2->vy == 6);
    CHECK("entity systems: movement procesa ambas entidades", p1->x == 12 && p1->y == 24 && p2->x == 96 && p2->y == 206);
    CHECK("entity systems: frames procesa ambas entidades", p1->frame == 1 && p2->frame == 11);
    CHECK("entity systems: manager de sistemas tiene 3 entidades", systems.active_count == 3);
    CHECK("entity systems: entidades normales siguen intactas", entities.active_count == 2);
}

static void test_entity_system_shared_data(void)
{
    kprintf("-- test_entity_system_shared_data --");
    de_manager entities, systems;
    DE_MANAGER_STORAGE(entities_storage, 2, sizeof(TestSystemEntity));
    de_manager_init(&entities, DE_MANAGER_ARGS(entities_storage));
    DE_MANAGER_STORAGE(systems_storage, 2, sizeof(TestBatchSystem));
    de_manager_init(&systems, DE_MANAGER_ARGS(systems_storage));
    void *movement_items[8], *frame_items[2];
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
    CHECK("shared data: movimiento usa x/y/vx/vy", data->x == 57 && data->y == 58);
    CHECK("shared data: frames usa el mismo payload", data->frame == 4);
}

static void test_entity_system_paused_entity(void)
{
    kprintf("-- test_entity_system_paused_entity --");
    de_manager entities, systems;
    DE_MANAGER_STORAGE(entities_storage, 2, sizeof(TestSystemEntity));
    de_manager_init(&entities, DE_MANAGER_ARGS(entities_storage));
    DE_MANAGER_STORAGE(systems_storage, 1, sizeof(TestBatchSystem));
    de_manager_init(&systems, DE_MANAGER_ARGS(systems_storage));
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
    CHECK("paused entity: sigue en el pool del sistema", paused->x == 103 && paused->y == 204);
    CHECK("paused entity: active tambien se procesa", active->x == 11 && active->y == 22);
    CHECK("paused entity: sigue pausada en su manager", paused_entity->slot >= entities.paused_start);
}

typedef struct TestDeSystemEntity
{
    int16_t x, y;
    int16_t vx, vy;
    uint16_t frame;
} TestDeSystemEntity;

DE_SYSTEM_ITERATOR(test_de_system_movement, int16_t *x, int16_t *y, int16_t *vx, int16_t *vy, {
    *x += *vx;
    *y += *vy;
});

DE_SYSTEM_ITERATOR(test_de_system_physics, int16_t *vy, {
    *vy += 1;
});

DE_SYSTEM_ITERATOR(test_de_system_frames, uint16_t *frame, {
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
    CHECK("de_system add: primer grupo", DE_SYSTEM_ADD(&sys, &a, &b, &c, &d) == 1);
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
    DE_SYSTEM_ADD(&sys, &a1, &b1, &c1, &d1);
    DE_SYSTEM_ADD(&sys, &a2, &b2, &c2, &d2);
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
    DE_SYSTEM_ADD(&sys, &a1, &b1, &c1, &d1);
    DE_SYSTEM_ADD(&sys, &a2, &b2, &c2, &d2);
    DE_SYSTEM_ADD(&sys, &a3, &b3, &c3, &d3);
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
    CHECK("de_system capacity: primer grupo", DE_SYSTEM_ADD(&sys, &a[0], &a[1], &b[0], &b[1]) == 1);
    CHECK("de_system capacity: segundo grupo", DE_SYSTEM_ADD(&sys, &b[0], &b[1], &c[0], &c[1]) == 1);
    CHECK("de_system capacity: rechaza grupo lleno", DE_SYSTEM_ADD(&sys, &c[0], &c[1], &d[0], &d[1]) == 0);
    CHECK("de_system capacity: size no cambia", sys.size == 8);
}

static void test_de_system_as_entities(void)
{
    kprintf("-- test_de_system_as_entities --");

    de_manager entities, systems;
    DE_MANAGER_STORAGE(entities_storage, 2, sizeof(TestDeSystemEntity));
    de_manager_init(&entities, DE_MANAGER_ARGS(entities_storage));
    DE_MANAGER_STORAGE(systems_storage, 3, sizeof(de_system));
    de_manager_init(&systems, DE_MANAGER_ARGS(systems_storage));

    de_entity frames_entity = de_manager_new(&systems);
    de_entity movement_entity = de_manager_new(&systems);
    de_entity physics_entity = de_manager_new(&systems);

    de_system *frames = (de_system *)frames_entity->data;
    de_system *movement = (de_system *)movement_entity->data;
    de_system *physics = (de_system *)physics_entity->data;

    DE_SYSTEM_STORAGE(frames_storage, 2, 1);
    de_system_init(frames, DE_SYSTEM_ARGS(frames_storage));

    DE_SYSTEM_STORAGE(movement_storage, 2, 4);
    de_system_init(movement, DE_SYSTEM_ARGS(movement_storage));

    DE_SYSTEM_STORAGE(physics_storage, 2, 1);
    de_system_init(physics, DE_SYSTEM_ARGS(physics_storage));

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

    DE_SYSTEM_ADD(movement, &p1->x, &p1->y, &p1->vx, &p1->vy);
    DE_SYSTEM_ADD(movement, &p2->x, &p2->y, &p2->vx, &p2->vy);
    DE_SYSTEM_ADD(physics, &p1->vy);
    DE_SYSTEM_ADD(physics, &p2->vy);
    DE_SYSTEM_ADD(frames, &p1->frame);
    DE_SYSTEM_ADD(frames, &p2->frame);

    de_manager_update(&systems);

    CHECK("de_system entities: physics modifica vy", p1->vy == 4 && p2->vy == 6);
    CHECK("de_system entities: movement procesa ambas", p1->x == 12 && p1->y == 24 && p2->x == 96 && p2->y == 206);
    CHECK("de_system entities: frames procesa ambas", p1->frame == 1 && p2->frame == 11);
    CHECK("de_system entities: manager contiene 3 sistemas", systems.active_count == 3);
    CHECK("de_system entities: manager de entidades intacto", entities.active_count == 2);
}

static void test_de_system_shared_payload(void)
{
    kprintf("-- test_de_system_shared_payload --");
    de_manager entities, systems;
    DE_MANAGER_STORAGE(entities_storage, 1, sizeof(TestDeSystemEntity));
    de_manager_init(&entities, DE_MANAGER_ARGS(entities_storage));
    DE_MANAGER_STORAGE(systems_storage, 2, sizeof(de_system));
    de_manager_init(&systems, DE_MANAGER_ARGS(systems_storage));
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
    DE_SYSTEM_ADD(movement, &data->x, &data->y, &data->vx, &data->vy);
    DE_SYSTEM_ADD(frames, &data->frame);
    de_manager_update(&systems);
    CHECK("de_system shared: movimiento modifica payload", data->x == 57 && data->y == 58);
    CHECK("de_system shared: frames usa el mismo payload", data->frame == 4);
}

static int g_execCalls = 0;
static void *state_exec_counter(void *data)
{
    (void)data;
    ++g_execCalls;
    return DE_STATE_LOOP;
}

static void test_entity_exec(void)
{
    kprintf("-- test_entity_exec --");
    g_execCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    e->state = (de_state)state_exec_counter;
    de_entity_exec(e);
    CHECK("exec: llamado 1 vez", g_execCalls == 1);
    CHECK("exec: estado no cambia", e->state == (de_state)state_exec_counter);
    de_entity_exec(e);
    de_entity_exec(e);
    CHECK("exec: llamado 3 veces", g_execCalls == 3);
}

static int g_frontBackCalls = 0;
static void *state_front_back(void *data)
{
    (void)data;
    ++g_frontBackCalls;
    return DE_STATE_LOOP;
}

static void test_entity_move_front_back(void)
{
    kprintf("-- test_entity_move_front_back --");
    g_frontBackCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);
    de_entity e2 = de_manager_new(&m);
    de_entity e3 = de_manager_new(&m);
    e0->state = e1->state = e2->state = e3->state = (de_state)state_front_back;
    de_entity_move_front(e1);
    CHECK("move_front: e1 al final", e1->slot == m.active_count - 1);
    de_entity_move_back(e3);
    CHECK("move_back: e3 al principio activo", e3->slot == 0);
    g_frontBackCalls = 0;
    de_manager_update(&m);
    CHECK("move_front/back: update respeta orden", g_frontBackCalls == 4);
}

static void *state_transition_target(void *data)
{
    struct MyComponent *c = (struct MyComponent *)data;
    c->x = 999;
    return DE_STATE_LOOP;
}

static void *state_transition_source(void *data)
{
    (void)data;
    return state_transition_target;
}

static void test_state_transition(void)
{
    kprintf("-- test_state_transition --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 1, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    struct MyComponent *c = (struct MyComponent *)e->data;
    c->x = 0;
    e->state = (de_state)state_transition_source;
    de_manager_update(&m);
    CHECK("transicion: estado cambiado", e->state == (de_state)state_transition_target);
    de_manager_update(&m);
    CHECK("transicion: nuevo estado ejecutado", c->x == 999);
}

static void test_delete_twice(void)
{
    kprintf("-- test_delete_twice --");
    g_destructorCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    e->destructor = (de_state)my_destructor;
    e->state = (de_state)state_noop;
    de_entity_delete(e);
    CHECK("delete twice: size 0 tras primer delete", m.active_count == 0);
    de_entity_delete(e);
    CHECK("delete twice: size sigue 0 (ya borrada)", m.active_count == 0);
    CHECK("delete twice: destructor 1 vez", g_destructorCalls == 1);
}

static void test_pause_already_paused(void)
{
    kprintf("-- test_pause_already_paused --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    a->state = b->state = (de_state)state_noop;
    de_entity_pause(a);
    uint16_t slot_before = a->slot;
    uint16_t pause_before = m.paused_start;
    de_entity_pause(a);
    CHECK("pause idempotente: slot no cambia", a->slot == slot_before);
    CHECK("pause idempotente: paused_start no cambia", m.paused_start == pause_before);
}

static void test_resume_already_active(void)
{
    kprintf("-- test_resume_already_active --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    a->state = b->state = (de_state)state_noop;
    uint16_t slot_before = a->slot;
    uint16_t pause_before = m.paused_start;
    de_entity_resume(a);
    CHECK("resume idempotente: slot no cambia", a->slot == slot_before);
    CHECK("resume idempotente: paused_start no cambia", m.paused_start == pause_before);
}

static void test_manager_iterate_active_only(void)
{
    kprintf("-- test_manager_iterate_active_only --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    for (int i = 0; i < 4; ++i)
    {
        de_entity e = de_manager_new(&m);
        e->tag = i;
        e->state = (de_state)state_noop;
    }
    de_entity_pause(m.items[1]);
    uint16_t count = 0;
    DE_MANAGER_ITERATE(&m, { count++; });
    CHECK("iterate active: 3 entidades", count == 3);
}

static void test_apply_active_only(void)
{
    kprintf("-- test_apply_active_only --");
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    for (int i = 0; i < 4; ++i)
    {
        de_entity e = de_manager_new(&m);
        e->tag = i;
        e->state = (de_state)state_noop;
    }
    de_entity_pause(m.items[0]);
    de_entity_pause(m.items[2]);
    // Eliminar tag 1 manualmente
    for (uint16_t i = 0; i < m.active_count; ++i)
    {
        if (m.items[i]->tag == 1)
        {
            de_entity_delete(m.items[i]);
            break;
        }
    }
    CHECK("apply active: size 1", m.active_count == 1);
    bool foundTag1 = FALSE;
    DE_MANAGER_ITERATE(&m, { if (ENTITY->tag == 1) foundTag1 = TRUE; });
    CHECK("apply active: tag 1 borrado", !foundTag1);
}

static void test_system_foreach_direct(void)
{
    kprintf("-- test_system_foreach_direct --");
    de_system sys;
    void *pool[12];
    de_system_init(&sys, pool, 3, 4);
    int a1 = 1, b1 = 2, c1 = 3, d1 = 4;
    int a2 = 10, b2 = 20, c2 = 30, d2 = 40;
    DE_SYSTEM_ADD(&sys, &a1, &b1, &c1, &d1);
    DE_SYSTEM_ADD(&sys, &a2, &b2, &c2, &d2);
    DE_SYSTEM_FOREACH(&sys, int *a, int *b, int *c, int *d, { *a += *b + *c + *d; });
    CHECK("foreach direct: grupo 1 modificado", a1 == 1 + 2 + 3 + 4);
    CHECK("foreach direct: grupo 2 modificado", a2 == 10 + 20 + 30 + 40);
}

static void test_system_add_various_arity(void)
{
    kprintf("-- test_system_add_various_arity --");
    de_system sys1, sys2, sys3, sys5;
    void *p1[3], *p2[6], *p3[9], *p5[15];
    de_system_init(&sys1, p1, 3, 1);
    de_system_init(&sys2, p2, 3, 2);
    de_system_init(&sys3, p3, 3, 3);
    de_system_init(&sys5, p5, 3, 5);
    int a, b, c, d, e;
    CHECK("add arity 1", DE_SYSTEM_ADD(&sys1, &a) == 1);
    CHECK("add arity 2", DE_SYSTEM_ADD(&sys2, &a, &b) == 1);
    CHECK("add arity 3", DE_SYSTEM_ADD(&sys3, &a, &b, &c) == 1);
    CHECK("add arity 5", DE_SYSTEM_ADD(&sys5, &a, &b, &c, &d, &e) == 1);
    CHECK("arity sizes", sys1.size == 1 && sys2.size == 2 && sys3.size == 3 && sys5.size == 5);
}

static void test_system_remove_first(void)
{
    kprintf("-- test_system_remove_first --");
    de_system sys;
    void *pool[12];
    de_system_init(&sys, pool, 3, 4);
    int a1, b1, c1, d1, a2, b2, c2, d2, a3, b3, c3, d3;
    DE_SYSTEM_ADD(&sys, &a1, &b1, &c1, &d1);
    DE_SYSTEM_ADD(&sys, &a2, &b2, &c2, &d2);
    DE_SYSTEM_ADD(&sys, &a3, &b3, &c3, &d3);
    de_system_remove(&sys, &a1);
    CHECK("remove first: size 8", sys.size == 8);
    CHECK("remove first: compacta con ultimo", sys.pool[0] == &a3);
}

static void test_system_remove_last(void)
{
    kprintf("-- test_system_remove_last --");
    de_system sys;
    void *pool[12];
    de_system_init(&sys, pool, 3, 4);
    int a1, b1, c1, d1, a2, b2, c2, d2, a3, b3, c3, d3;
    DE_SYSTEM_ADD(&sys, &a1, &b1, &c1, &d1);
    DE_SYSTEM_ADD(&sys, &a2, &b2, &c2, &d2);
    DE_SYSTEM_ADD(&sys, &a3, &b3, &c3, &d3);
    de_system_remove(&sys, &a3);
    CHECK("remove last: size 8", sys.size == 8);
    CHECK("remove last: no toca primeros", sys.pool[0] == &a1 && sys.pool[4] == &a2);
}

static void test_empty_system(void)
{
    kprintf("-- test_empty_system --");
    de_system sys;
    void *pool[4];
    de_system_init(&sys, pool, 1, 4);
    CHECK("empty system: size 0", sys.size == 0);
    int sum = 0;
    DE_SYSTEM_FOREACH(&sys, int *a, int *b, int *c, int *d, {
        b = c;
        c = d;
        d = b;
        sum += *a;
    });
    CHECK("empty system: foreach no itera", sum == 0);
    CHECK("empty system: remove devuelve 0", de_system_remove(&sys, (void *)1) == 0);
}

static int g_destructorChangeCalls = 0;
static void *state_change_via_destructor(void *data)
{
    (void)data;
    ++g_destructorChangeCalls;
    return state_noop;
}

static void test_destructor_state_change(void)
{
    kprintf("-- test_destructor_state_change --");
    g_destructorChangeCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 1, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    e->destructor = (de_state)state_change_via_destructor;
    e->state = DE_STATE_DELETE;
    de_manager_update(&m);
    CHECK("destructor change: size 1", m.active_count == 1);
    CHECK("destructor change: llamado 1 vez", g_destructorChangeCalls == 1);
    CHECK("destructor change: estado cambiado a noop", e->state == (de_state)state_noop);
}

static void test_entity_update_direct(void)
{
    kprintf("-- test_entity_update_direct --");
    g_walkCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(m_storage, 1, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    struct MyComponent *c = (struct MyComponent *)e->data;
    c->x = 0;
    e->state = (de_state)state_walk;
    de_entity_update(e);
    CHECK("update direct: ejecuta estado", g_walkCalls == 1 && c->x == 1);
    CHECK("update direct: estado sigue activo", e->state == (de_state)state_walk);
}

void run_all_tests(void)
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
    test_entity_exec();
    test_entity_move_front_back();
    test_state_transition();
    test_delete_twice();
    test_pause_already_paused();
    test_resume_already_active();
    test_manager_iterate_active_only();
    test_apply_active_only();
    test_system_foreach_direct();
    test_system_add_various_arity();
    test_system_remove_first();
    test_system_remove_last();
    test_empty_system();
    test_destructor_state_change();
    test_entity_update_direct();
    kprintf("============================");
    kprintf("Resultado: %d/%d tests OK", g_testsPassed, g_testsRun);
}