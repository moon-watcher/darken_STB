#include <genesis.h>
#include "../../darken.h"
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
            /* kprintf("  [PASS] %s", desc); */ \
        }                                 \
        else                              \
        {                                 \
            kprintf("  [FAIL] %s", desc); \
        }                                 \
    } while (0)

static bool all_slots_aligned(de_manager mgr)
{
    for (uint16_t i = 0; i < mgr->capacity; ++i)
        if (((u32)mgr->pool[i]) & 3)
            return FALSE;
    return TRUE;
}

static void de_test_alignment(void)
{
    kprintf("-- test_alignment --");
    struct de_manager m1, m2, m3;
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

static void de_test_creation(void)
{
    kprintf("-- test_creation --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 3, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
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
    CHECK("arranca en state delete", e0->state == DE_STATE_DELETE);
    CHECK("arranca con tag 0", e0->tag == 0);
}

static int g_walkCalls = 0;
static void *de_state_walk(void *data)
{
    struct MyComponent *c = (struct MyComponent *)data;
    c->x += 1;
    ++g_walkCalls;
    // kprintf("------------walk------------");
    return DE_STATE_LOOP;
}
static void *de_state_once_then_idle(void *data)
{
    (void)data;
    return de_state_walk;
}

static void de_test_update(void)
{
    kprintf("-- test_update --");
    g_walkCalls = 0;
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    struct MyComponent *c = (struct MyComponent *)e->data;
    c->x = 0;
    e->state = (de_state)de_state_walk;
    de_manager_update(&m);
    de_manager_update(&m);
    de_manager_update(&m);
    CHECK("loop no cambia puntero", e->state == (de_state)de_state_walk);
    CHECK("loop ejecuta x3", g_walkCalls == 3 && c->x == 3);
    de_entity e2 = de_manager_new(&m);
    e2->state = (de_state)de_state_once_then_idle;
    de_manager_update(&m);
    CHECK("transicion actualiza estado", e2->state == (de_state)de_state_walk);
}

static int g_idleCalls = 0;
static void *de_state_idle_counter(void *data)
{
    (void)data;
    ++g_idleCalls;
    return DE_STATE_LOOP;
}

static void de_test_pause_resume(void)
{
    kprintf("-- test_pause_resume --");
    g_idleCalls = 0;
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    a->state = (de_state)de_state_idle_counter;
    b->state = (de_state)de_state_idle_counter;
    de_manager_update(&m);
    CHECK("ambas activas: 2 llamadas", g_idleCalls == 2);
    de_entity_pause(b);
    CHECK("b queda en zona pausada", b->slot >= m.paused);
    g_idleCalls = 0;
    de_manager_update(&m);
    CHECK("con b pausada: 1 llamada", g_idleCalls == 1);
    de_entity_resume(b);
    CHECK("b vuelve a zona activa", b->slot < m.paused);
    g_idleCalls = 0;
    de_manager_update(&m);
    CHECK("resumir: 2 llamadas", g_idleCalls == 2);
}

static int g_destructorCalls = 0;
static void *de_my_destructor(void *data)
{
    (void)data;
    ++g_destructorCalls;
    return DE_STATE_DELETE;
}
static void *de_state_noop(void *data)
{
    (void)data;
    return DE_STATE_LOOP;
}

static void de_test_delete(void)
{
    kprintf("-- test_delete --");
    g_destructorCalls = 0;
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    de_entity c = de_manager_new(&m);
    a->destructor = (de_state)de_my_destructor;
    b->destructor = (de_state)de_my_destructor;
    c->destructor = (de_state)de_my_destructor;
    b->state = (de_state)de_state_noop;
    c->state = (de_state)de_state_noop;
    a->state = DE_STATE_DELETE;
    de_manager_update(&m);
    CHECK("delete via estado: size 2", m.size == 2);
    CHECK("delete via estado: destructor 1 vez", g_destructorCalls == 1);
    de_entity_delete(b);
    CHECK("delete directo: size 1", m.size == 1);
    CHECK("delete directo: destructor 2 veces", g_destructorCalls == 2);
}

static void de_test_apply(void)
{
    kprintf("-- test_apply --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 5, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    for (int i = 0; i < 5; ++i)
    {
        de_entity e = de_manager_new(&m);
        e->tag = i;
    }

    // Iteración segura para borrar con swap-remove
    uint16_t idx = 0;
    while (idx < m.size)
    {
        de_entity e = m.pool[idx];
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

    CHECK("quedan 2 entidades", m.size == 2);
    bool onlyOdd = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
    {
        if ((m.pool[i]->tag % 2) == 0)
            onlyOdd = FALSE;
    }
    CHECK("quedan tags impares", onlyOdd);
}

static void de_test_reset(void)
{
    kprintf("-- test_reset --");
    g_destructorCalls = 0;
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 6, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    for (int i = 0; i < 6; ++i)
    {
        de_entity e = de_manager_new(&m);
        e->destructor = (de_state)de_my_destructor;
    }
    de_manager_reset(&m);
    CHECK("reset: size 0", m.size == 0);
    CHECK("reset: destructor 6 veces", g_destructorCalls == 6);
}

static int g_abortDestructorCalls = 0;
static void *de_state_abort_destructor(void *data)
{
    (void)data;
    ++g_abortDestructorCalls;
    return DE_STATE_LOOP;
}

static void de_test_destructor_abort(void)
{
    kprintf("-- test_destructor_abort --");
    g_abortDestructorCalls = 0;
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    e->destructor = (de_state)de_state_abort_destructor;
    e->state = DE_STATE_DELETE;
    de_manager_update(&m);
    CHECK("abort: size 1", m.size == 1);
    CHECK("abort: destructor llamado", g_abortDestructorCalls == 1);
    CHECK("abort: entidad viva", e->slot < m.size);
    CHECK("abort: estado a loop", e->state == DE_STATE_LOOP);
}

static int g_selfKillCalls = 0;
static void *de_state_self_kill(void *data)
{
    (void)data;
    ++g_selfKillCalls;
    return DE_STATE_DELETE;
}

static void de_test_self_delete(void)
{
    kprintf("-- test_self_delete --");
    g_selfKillCalls = 0;
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 3, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    a->state = (de_state)de_state_self_kill;
    b->state = (de_state)de_state_noop;
    de_manager_update(&m);
    CHECK("self-del f1: ejecutado 1 vez", g_selfKillCalls == 1);
    CHECK("self-del f1: marcada deleted", a->state == DE_STATE_DELETE);
    CHECK("self-del f1: size aun 2", m.size == 2);
    de_manager_update(&m);
    CHECK("self-del f2: size 1", m.size == 1);
    CHECK("self-del f2: queda b", m.pool[0] == b);
}

static void de_test_slot_stability(void)
{
    kprintf("-- test_slot_stability --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);
    de_entity e2 = de_manager_new(&m);
    de_entity e3 = de_manager_new(&m);
    e0->state = e1->state = e2->state = e3->state = (de_state)de_state_noop;
    de_entity_pause(e1);
    de_entity_pause(e3);
    bool stable = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
        if (m.pool[i]->slot != i)
            stable = FALSE;
    CHECK("slots tras pausar", stable);
    CHECK("paused 2", m.paused == 2);
    de_entity_delete(e0);
    stable = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
        if (m.pool[i]->slot != i)
            stable = FALSE;
    CHECK("slots tras delete", stable);
    CHECK("size 1", m.size == 1);
}

static void de_test_delete_paused(void)
{
    kprintf("-- test_delete_paused --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    de_entity c = de_manager_new(&m);
    a->state = b->state = c->state = (de_state)de_state_noop;
    de_entity_pause(b);
    de_entity_delete(b);
    CHECK("del pausado: size 2", m.size == 2);
    CHECK("del pausado: paused 4", m.paused == 4);
    CHECK("del pausado: a activa", a->slot < m.paused); 
    CHECK("del pausado: c activa", c->slot < m.paused); 
}

static void de_test_apply_pause(void)
{
    kprintf("-- test_apply_pause --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 6, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity entities[6];
    for (int i = 0; i < 6; ++i)
    {
        entities[i] = de_manager_new(&m);
        entities[i]->tag = i;
        entities[i]->state = (de_state)de_state_noop;
    }

    // Pausar las pares manualmente (seguro)
    for (int i = 0; i < 6; i += 2)
    {
        de_entity_pause(entities[i]);
    }

    CHECK("apply pause: paused 3", m.paused == 3);
    CHECK("apply pause: size 3", m.size == 3);
    bool correct = TRUE;
    // Verificar que pares están en pausa y impares en activa
    for (int i = 0; i < 6; ++i)
    {
        de_entity e = entities[i];
        bool shouldBePaused = (e->tag % 2) == 0;
        bool isPaused = (e->slot >= m.paused);
        if (shouldBePaused != isPaused)
        {
            correct = FALSE;
            break;
        }
    }
    CHECK("apply pause: pares pausadas", correct);
}

static void de_test_reuse(void)
{
    kprintf("-- test_reuse --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 3, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e0 = de_manager_new(&m);
    e0->state = (de_state)de_state_noop;
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
static void *de_state_stress_a(void *data)
{
    (void)data;
    ++g_stressCallsA;
    return DE_STATE_LOOP;
}
static void *de_state_stress_b(void *data)
{
    (void)data;
    ++g_stressCallsB;
    return DE_STATE_LOOP;
}

static void de_test_mixed_stress(void)
{
    kprintf("-- test_mixed_stress --");
    g_stressCallsA = 0;
    g_stressCallsB = 0;
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 5, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e[5];
    for (int i = 0; i < 5; ++i)
    {
        e[i] = de_manager_new(&m);
        e[i]->tag = i;
        e[i]->state = (i % 2 == 0) ? (de_state)de_state_stress_a : (de_state)de_state_stress_b;
    }
    de_manager_update(&m);
    CHECK("stress f1: 5 llamadas", g_stressCallsA + g_stressCallsB == 5);
    de_entity_pause(e[1]);
    e[3]->state = DE_STATE_DELETE;
    g_stressCallsA = 0;
    g_stressCallsB = 0;
    de_manager_update(&m);
    CHECK("stress f2: 3 activas", g_stressCallsA + g_stressCallsB == 3);
    CHECK("stress f2: size 3", m.size == 3);
    de_entity_resume(e[1]);
    g_stressCallsA = 0;
    g_stressCallsB = 0;
    de_manager_update(&m);
    CHECK("stress f3: 4 activas", g_stressCallsA + g_stressCallsB == 4);
    bool ok = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
        if (m.pool[i]->slot != i)
            ok = FALSE;
    CHECK("stress: slots ok", ok);
}

static void de_test_data_integrity(void)
{
    kprintf("-- test_data_integrity --");
    struct de_manager m;
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
    a->state = b->state = c->state = (de_state)de_state_noop;
    de_entity_pause(b);
    de_entity_delete(a);
    CHECK("integrity a", ((struct MyComponent *)a->data)->x == 111);
    CHECK("integrity b", ((struct MyComponent *)b->data)->x == 333);
    CHECK("integrity c", ((struct MyComponent *)c->data)->x == 555);
}

static void de_test_empty_manager(void)
{
    kprintf("-- test_empty_manager --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    CHECK("empty: size 0", m.size == 0);
    de_manager_update(&m);
    CHECK("empty: size sigue 0", m.size == 0);
    CHECK("empty: paused 4", m.paused == 4);
}

static void de_test_delete_last(void)
{
    kprintf("-- test_delete_last --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    e->state = (de_state)de_state_noop;
    de_entity_delete(e);
    CHECK("last: size 0", m.size == 0);
    CHECK("last: paused 4", m.paused == 4);
}

static void de_test_stress_capacity(void)
{
    kprintf("-- test_stress_capacity --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 50, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
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
        if (m.pool[i]->slot != i)
            ok = FALSE;
        if (m.pool[i]->owner != &m)
            ok = FALSE;
    }
    CHECK("slots y manager correctos", ok);
}

static void de_test_stress_many_entities(void)
{
    kprintf("-- test_stress_many_entities --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 20, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e[20];
    for (int i = 0; i < 20; ++i)
    {
        e[i] = de_manager_new(&m);
        e[i]->tag = i;
        e[i]->state = (de_state)de_state_noop;
    }
    for (int i = 1; i < 20; i += 2)
        de_entity_delete(e[i]);
    CHECK("many: size 10 tras borrar impares", m.size == 10);
    for (int i = 0; i < 5; ++i)
    {
        de_entity ne = de_manager_new(&m);
        ne->tag = 100 + i;
        ne->state = (de_state)de_state_noop;
    }
    CHECK("many: size 15 tras recrear", m.size == 15);
    bool unique = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
        for (uint16_t j = i + 1; j < m.size; ++j)
            if (m.pool[i]->tag == m.pool[j]->tag)
                unique = FALSE;
    CHECK("many: tags unicos", unique);
}

static void de_test_stress_fragmentation(void)
{
    kprintf("-- test_stress_fragmentation --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 20, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e[20];
    for (int i = 0; i < 20; ++i)
    {
        e[i] = de_manager_new(&m);
        e[i]->tag = i;
        e[i]->state = (de_state)de_state_noop;
    }
    for (int i = 1; i < 20; i += 2)
        de_entity_delete(e[i]);
    CHECK("frag: size 10 tras borrar impares", m.size == 10);
    for (int i = 0; i < 5; ++i)
    {
        de_entity ne = de_manager_new(&m);
        ne->tag = 100 + i;
        ne->state = (de_state)de_state_noop;
    }
    CHECK("frag: size 15 tras recrear", m.size == 15);
    bool unique = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
        for (uint16_t j = i + 1; j < m.size; ++j)
            if (m.pool[i]->tag == m.pool[j]->tag)
                unique = FALSE;
    CHECK("frag: tags unicos tras recreacion", unique);
}



// static void *de_test_

static int g_execCalls = 0;
static void *de_state_exec_counter(void *data)
{
    (void)data;
    ++g_execCalls;
    return DE_STATE_LOOP;
}

static void de_test_entity_exec(void)
{
    kprintf("-- test_entity_exec --");
    g_execCalls = 0;
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    e->state = (de_state)de_state_exec_counter;
    de_entity_exec(e);
    CHECK("exec: llamado 1 vez", g_execCalls == 1);
    CHECK("exec: estado no cambia", e->state == (de_state)de_state_exec_counter);
    de_entity_exec(e);
    de_entity_exec(e);
    CHECK("exec: llamado 3 veces", g_execCalls == 3);
}


static void *de_state_transition_target(void *data)
{
    struct MyComponent *c = (struct MyComponent *)data;
    c->x = 999;
    return DE_STATE_LOOP;
}

static void *de_state_transition_source(void *data)
{
    (void)data;
    return de_state_transition_target;
}

static void de_test_state_transition(void)
{
    kprintf("-- test_state_transition --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 1, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    struct MyComponent *c = (struct MyComponent *)e->data;
    c->x = 0;
    e->state = (de_state)de_state_transition_source;
    de_manager_update(&m);
    CHECK("transicion: estado cambiado", e->state == (de_state)de_state_transition_target);
    de_manager_update(&m);
    CHECK("transicion: nuevo estado ejecutado", c->x == 999);
}

static void de_test_delete_twice(void)
{
    kprintf("-- test_delete_twice --");
    g_destructorCalls = 0;
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    e->destructor = (de_state)de_my_destructor;
    e->state = (de_state)de_state_noop;
    de_entity_delete(e);
    CHECK("delete twice: size 0 tras primer delete", m.size == 0);
    de_entity_delete(e);
    CHECK("delete twice: size sigue 0 (ya borrada)", m.size == 0);
    CHECK("delete twice: destructor 1 vez", g_destructorCalls == 1);
}

static void de_test_pause_already_paused(void)
{
    kprintf("-- test_pause_already_paused --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    a->state = b->state = (de_state)de_state_noop;
    de_entity_pause(a);
    uint16_t slot_before = a->slot;
    uint16_t pause_before = m.paused;
    de_entity_pause(a);
    CHECK("pause idempotente: slot no cambia", a->slot == slot_before);
    CHECK("pause idempotente: paused no cambia", m.paused == pause_before);
}

static void de_test_resume_already_active(void)
{
    kprintf("-- test_resume_already_active --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity a = de_manager_new(&m);
    de_entity b = de_manager_new(&m);
    a->state = b->state = (de_state)de_state_noop;
    uint16_t slot_before = a->slot;
    uint16_t pause_before = m.paused;
    de_entity_resume(a);
    CHECK("resume idempotente: slot no cambia", a->slot == slot_before);
    CHECK("resume idempotente: paused no cambia", m.paused == pause_before);
}

static void de_test_manager_iterate_active_only(void)
{
    kprintf("-- test_manager_iterate_active_only --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    for (int i = 0; i < 4; ++i)
    {
        de_entity e = de_manager_new(&m);
        e->tag = i;
        e->state = (de_state)de_state_noop;
    }
    de_entity_pause(m.pool[1]);
    uint16_t count = 0;
    DE_MANAGER_FOREACH(&m, { count++; });
    CHECK("iterate active: 3 entidades", count == 3);
}

static void de_test_apply_active_only(void)
{
    kprintf("-- test_apply_active_only --");
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    for (int i = 0; i < 4; ++i)
    {
        de_entity e = de_manager_new(&m);
        e->tag = i;
        e->state = (de_state)de_state_noop;
    }
    de_entity_pause(m.pool[0]);
    de_entity_pause(m.pool[2]);
    // Eliminar tag 1 manualmente
    for (uint16_t i = 0; i < m.size; ++i)
    {
        if (m.pool[i]->tag == 1)
        {
            de_entity_delete(m.pool[i]);
            break;
        }
    }
    CHECK("apply active: size 1", m.size == 1);
    bool foundTag1 = FALSE;
    DE_MANAGER_FOREACH(&m, { if (ENTITY->tag == 1) foundTag1 = TRUE; });
    CHECK("apply active: tag 1 borrado", !foundTag1);
}



static int g_destructorChangeCalls = 0;
static void *de_state_change_via_destructor(void *data)
{
    (void)data;
    ++g_destructorChangeCalls;
    return de_state_noop;
}

static void de_test_destructor_state_change(void)
{
    kprintf("-- test_destructor_state_change --");
    g_destructorChangeCalls = 0;
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 1, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    e->destructor = (de_state)de_state_change_via_destructor;
    e->state = DE_STATE_DELETE;
    de_manager_update(&m);
    CHECK("destructor change: size 1", m.size == 1);
    CHECK("destructor change: llamado 1 vez", g_destructorChangeCalls == 1);
    CHECK("destructor change: estado cambiado a noop", e->state == (de_state)de_state_noop);
}

static void de_test_entity_update_direct(void)
{
    kprintf("-- test_entity_update_direct --");
    g_walkCalls = 0;
    struct de_manager m;
    DE_MANAGER_STORAGE(m_storage, 1, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(m_storage));
    de_entity e = de_manager_new(&m);
    struct MyComponent *c = (struct MyComponent *)e->data;
    c->x = 0;
    e->state = (de_state)de_state_walk;
    de_entity_update(e);
    CHECK("update direct: ejecuta estado", g_walkCalls == 1 && c->x == 1);
    CHECK("update direct: estado sigue activo", e->state == (de_state)de_state_walk);
}

void de_run_all_tests(void)
{
    g_testsRun = 0;
    g_testsPassed = 0;
    kprintf("========== TESTS ==========");
    de_test_alignment();
    de_test_creation();
    de_test_update();
    de_test_pause_resume();
    de_test_delete();
    de_test_apply();
    de_test_reset();
    de_test_destructor_abort();
    de_test_self_delete();
    de_test_slot_stability();
    de_test_delete_paused();
    de_test_apply_pause();
    de_test_reuse();
    de_test_mixed_stress();
    de_test_data_integrity();
    de_test_empty_manager();
    de_test_delete_last();
    de_test_stress_capacity();
    de_test_stress_many_entities();
    de_test_stress_fragmentation();
    de_test_entity_exec();
    de_test_state_transition();
    de_test_delete_twice();
    de_test_pause_already_paused();
    de_test_resume_already_active();
    de_test_manager_iterate_active_only();
    de_test_apply_active_only();
    de_test_destructor_state_change();
    de_test_entity_update_direct();
    kprintf("============================");
    kprintf("Resultado: %d/%d tests OK", g_testsPassed, g_testsRun);
}