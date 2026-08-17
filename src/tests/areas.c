#include <genesis.h>

#include "../darken.h"

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

/* ============================================================
 * FUNCIONES AUXILIARES
 * ============================================================ */

static bool entity_is_active(de_manager *m, de_entity e)
{
    return e->slot < m->size;
}

static bool entity_is_paused(de_manager *m, de_entity e)
{
    return e->slot >= m->pause_index && e->slot < m->capacity;
}

static bool entity_is_free(de_manager *m, de_entity e)
{
    return e->slot >= m->size && e->slot < m->pause_index;
}

static bool all_active_entities_are_contiguous(de_manager *m)
{
    for (uint16_t i = 0; i < m->size; ++i)
        if (!m->items[i] || m->items[i]->slot != i)
            return FALSE;
    return TRUE;
}

static bool all_paused_entities_are_contiguous(de_manager *m)
{
    for (uint16_t i = m->pause_index; i < m->capacity; ++i)
        if (!m->items[i] || m->items[i]->slot != i)
            return FALSE;
    return TRUE;
}

static int g_destructorCalls = 0;

static void *test_destructor(void *data)
{
    (void)data;
    ++g_destructorCalls;
    return DE_STATE_DELETE;
}

static void *test_destructor_abort(void *data)
{
    (void)data;
    ++g_destructorCalls;
    return DE_STATE_LOOP;
}

static void *state_noop(void *data)
{
    (void)data;
    return DE_STATE_LOOP;
}

/* ============================================================
 * TEST 1: INICIALIZACIÓN DEL POOL
 * ============================================================ */

static void test_init_pool_layout(void)
{
    kprintf("-- test_init_pool_layout --");

    de_manager m;
    DE_MANAGER_STORAGE(storage, 10, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    CHECK("inicialmente size = 0", m.size == 0);
    CHECK("inicialmente pause_index = capacity", m.pause_index == 10);
    CHECK("capacidad correcta", m.capacity == 10);

    // Todas las entidades deben estar en zona libre
    bool all_free = TRUE;
    for (uint16_t i = 0; i < m.capacity; ++i)
        if (m.items[i]->slot != i)
            all_free = FALSE;
    CHECK("todos los slots iniciales son libres", all_free);
}

/* ============================================================
 * TEST 2: CREACIÓN DE ENTIDADES
 * ============================================================ */

static void test_new_entity_layout(void)
{
    kprintf("-- test_new_entity_layout --");

    de_manager m;
    DE_MANAGER_STORAGE(storage, 5, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);

    CHECK("e0 creada", e0 != NULL);
    CHECK("e1 creada", e1 != NULL);

    CHECK("size = 2 tras crear dos", m.size == 2);
    CHECK("pause_index sigue en capacity", m.pause_index == 5);

    CHECK("e0 es activa", entity_is_active(&m, e0));
    CHECK("e1 es activa", entity_is_active(&m, e1));

    CHECK("e0 en slot 0", e0->slot == 0);
    CHECK("e1 en slot 1", e1->slot == 1);

    CHECK("área activa contigua", all_active_entities_are_contiguous(&m));
}

/* ============================================================
 * TEST 3: LLENADO COMPLETO
 * ============================================================ */

static void test_full_capacity(void)
{
    kprintf("-- test_full_capacity --");

    de_manager m;
    DE_MANAGER_STORAGE(storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    for (int i = 0; i < 4; ++i)
    {
        de_entity e = de_manager_new(&m);
        CHECK("entidad creada", e != NULL);
    }

    CHECK("size = 4", m.size == 4);
    CHECK("pause_index = 4", m.pause_index == 4); // sin huecos libres
    de_entity e_extra = de_manager_new(&m);
    CHECK("no hay hueco para una quinta", e_extra == NULL);

    CHECK("área activa contigua", all_active_entities_are_contiguous(&m));
}

/* ============================================================
 * TEST 4: PAUSAR UNA ENTIDAD ACTIVA
 * ============================================================ */

static void test_pause_active_entity(void)
{
    kprintf("-- test_pause_active_entity --");

    de_manager m;
    DE_MANAGER_STORAGE(storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);
    de_entity e2 = de_manager_new(&m);

    // Pausamos e1
    de_entity_pause(e1);

    CHECK("size decrece a 2", m.size == 2);
    CHECK("pause_index decrece a 3", m.pause_index == 3);

    CHECK("e0 sigue activa", entity_is_active(&m, e0));
    CHECK("e2 sigue activa", entity_is_active(&m, e2));
    CHECK("e1 ahora pausada", entity_is_paused(&m, e1));

    CHECK("área activa contigua", all_active_entities_are_contiguous(&m));
    CHECK("área pausada contigua", all_paused_entities_are_contiguous(&m));
}

/* ============================================================
 * TEST 5: REANUDAR UNA ENTIDAD PAUSADA
 * ============================================================ */

static void test_resume_paused_entity(void)
{
    kprintf("-- test_resume_paused_entity --");

    de_manager m;
    DE_MANAGER_STORAGE(storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);
    de_entity e2 = de_manager_new(&m);

    de_entity_pause(e1);
    de_entity_resume(e1);

    CHECK("size vuelve a 3", m.size == 3);
    CHECK("pause_index vuelve a 4", m.pause_index == 4);

    CHECK("e1 activa de nuevo", entity_is_active(&m, e1));

    CHECK("área activa contigua", all_active_entities_are_contiguous(&m));
    CHECK("área pausada contigua", all_paused_entities_are_contiguous(&m));
}

/* ============================================================
 * TEST 6: PAUSAR Y REANUDAR MÚLTIPLES ENTIDADES
 * ============================================================ */

static void test_pause_resume_multiple(void)
{
    kprintf("-- test_pause_resume_multiple --");

    de_manager m;
    DE_MANAGER_STORAGE(storage, 6, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e[6];
    for (int i = 0; i < 6; ++i)
        e[i] = de_manager_new(&m);

    // Pausamos tres entidades
    de_entity_pause(e[1]);
    de_entity_pause(e[3]);
    de_entity_pause(e[5]);

    CHECK("size = 3", m.size == 3);
    CHECK("pause_index = 3", m.pause_index == 3);

    for (int i = 0; i < 6; ++i)
    {
        if (i == 1 || i == 3 || i == 5)
            CHECK("entidad pausada", entity_is_paused(&m, e[i]));
        else
            CHECK("entidad activa", entity_is_active(&m, e[i]));
    }

    // Reanudamos una entidad pausada
    de_entity_resume(e[3]);
    CHECK("size = 4", m.size == 4);
    CHECK("pause_index = 4", m.pause_index == 4);
    CHECK("e3 activa", entity_is_active(&m, e[3]));

    CHECK("área activa contigua", all_active_entities_are_contiguous(&m));
    CHECK("área pausada contigua", all_paused_entities_are_contiguous(&m));
}

/* ============================================================
 * TEST 7: ELIMINAR ENTIDAD ACTIVA
 * ============================================================ */

static void test_delete_active_entity(void)
{
    kprintf("-- test_delete_active_entity --");

    de_manager m;
    DE_MANAGER_STORAGE(storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);
    de_entity e2 = de_manager_new(&m);

    de_entity_delete(e1);

    CHECK("size = 2", m.size == 2);
    CHECK("pause_index = 4", m.pause_index == 4);
    CHECK("e0 sigue activa", entity_is_active(&m, e0));
    CHECK("e2 sigue activa", entity_is_active(&m, e2));
    CHECK("e1 en zona libre", entity_is_free(&m, e1));

    CHECK("área activa contigua", all_active_entities_are_contiguous(&m));
}

/* ============================================================
 * TEST 8: ELIMINAR ENTIDAD PAUSADA
 * ============================================================ */

static void test_delete_paused_entity(void)
{
    kprintf("-- test_delete_paused_entity --");

    de_manager m;
    DE_MANAGER_STORAGE(storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);
    de_entity e2 = de_manager_new(&m);

    de_entity_pause(e1);
    de_entity_delete(e1);

    CHECK("size = 2", m.size == 2);
    CHECK("pause_index = 4", m.pause_index == 4); // recuperó un hueco libre
    CHECK("e0 activa", entity_is_active(&m, e0));
    CHECK("e2 activa", entity_is_active(&m, e2));
    CHECK("e1 en zona libre", entity_is_free(&m, e1));

    CHECK("área activa contigua", all_active_entities_are_contiguous(&m));
}

/* ============================================================
 * TEST 9: DESTRUCTOR EN ELIMINACIÓN
 * ============================================================ */

static void test_delete_with_destructor(void)
{
    kprintf("-- test_delete_with_destructor --");

    g_destructorCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(storage, 3, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);

    e0->destructor = test_destructor;
    e1->destructor = test_destructor;

    de_entity_delete(e0);
    CHECK("destructor llamado 1 vez", g_destructorCalls == 1);
    CHECK("size decrece", m.size == 1);

    de_entity_delete(e1);
    CHECK("destructor llamado 2 veces", g_destructorCalls == 2);
    CHECK("size decrece a 0", m.size == 0);
}

/* ============================================================
 * TEST 10: DESTRUCTOR ABORTA ELIMINACIÓN
 * ============================================================ */

static void test_destructor_abort_delete(void)
{
    kprintf("-- test_destructor_abort_delete --");

    g_destructorCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(storage, 2, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e0 = de_manager_new(&m);
    e0->destructor = test_destructor_abort;
    e0->state = (de_state)state_noop;

    de_entity_delete(e0);

    CHECK("destructor llamado", g_destructorCalls == 1);
    CHECK("entidad no eliminada", m.size == 1);
    CHECK("entidad sigue activa", entity_is_active(&m, e0));
    CHECK("estado cambiado a loop", e0->state == (de_state)DE_STATE_LOOP);
}

/* ============================================================
 * TEST 11: UPDATE SOLO ACTIVE
 * ============================================================ */

static int g_updateCalls = 0;
static void *state_update_counter(void *data)
{
    (void)data;
    ++g_updateCalls;
    return DE_STATE_LOOP;
}

static void test_update_only_active(void)
{
    kprintf("-- test_update_only_active --");

    g_updateCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);
    de_entity e2 = de_manager_new(&m);

    e0->state = (de_state)state_update_counter;
    e1->state = (de_state)state_update_counter;
    e2->state = (de_state)state_update_counter;

    
    de_manager_update(&m);
    // de_entity_pause(e2);

    CHECK("solo activas actualizadas", g_updateCalls == 2);
}

/* ============================================================
 * TEST 12: ITERACIÓN MACROS
 * ============================================================ */

static void test_iteration_macros(void)
{
    // kprintf("-- test_iteration_macros --");

    // de_manager m;
    // DE_MANAGER_STORAGE(storage, 6, sizeof(struct MyComponent));
    // de_manager_init(&m, DE_MANAGER_ARGS(storage));

    // de_entity e[6];
    // for (int i = 0; i < 6; ++i)
    // {
    //     e[i] = de_manager_new(&m);
    //     e[i]->tag = i;
    // }

    // de_entity_pause(e[2]);
    // de_entity_pause(e[5]);

    // uint16_t active_count = 0;
    // DE_MANAGER_ITERATE(&m, {
    //     active_count++;
    //     CHECK("iteración activa", entity_is_active(&m, ENTITY));
    // });
    // CHECK("iteración activa cuenta 4", active_count == 4);

    // uint16_t all_count = 0;
    // DE_MANAGER_ITERATE_ALL(&m, {
    //     all_count++;
    // });
    // CHECK("iteración all cuenta 6", all_count == 6);
}

/* ============================================================
 * TEST 13: APPLY MACROS
 * ============================================================ */

static void test_apply_macros(void)
{
    // kprintf("-- test_apply_macros --");

    // de_manager m;
    // DE_MANAGER_STORAGE(storage, 6, sizeof(struct MyComponent));
    // de_manager_init(&m, DE_MANAGER_ARGS(storage));

    // for (int i = 0; i < 6; ++i)
    // {
    //     de_entity e = de_manager_new(&m);
    //     e->tag = i;
    //     e->state = (de_state)state_noop;
    // }

    // DE_MANAGER_APPLY(&m, (ENTITY->tag % 2) == 0, de_entity_pause);

    // CHECK("apply pause: 3 activas", m.size == 3);
    // CHECK("apply pause: 3 pausadas", m.capacity - m.pause_index == 3);

    // DE_MANAGER_APPLY_ALL(&m, (ENTITY->tag % 2) == 0, de_entity_delete);

    // // Después de borrar las pausadas pares, solo quedan impares
    // uint16_t remaining = 0;
    // DE_MANAGER_ITERATE_ALL(&m, {
    //     if ((ENTITY->tag % 2) != 0)
    //         remaining++;
    // });
    // CHECK("apply delete: quedan 3 impares", remaining == 3);
}

/* ============================================================
 * TEST 14: PAUSE GLOBAL
 * ============================================================ */

static void test_manager_pause(void)
{
    kprintf("-- test_manager_pause --");

    de_manager m;
    DE_MANAGER_STORAGE(storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);

    // de_manager_pause(&m);

    CHECK("size = 0", m.size == 0);
    CHECK("pause_index = 0", m.pause_index == 0);
    CHECK("e0 pausada", entity_is_paused(&m, e0));
    CHECK("e1 pausada", entity_is_paused(&m, e1));
}

/* ============================================================
 * TEST 15: RESUME GLOBAL
 * ============================================================ */

static void test_manager_resume(void)
{
    kprintf("-- test_manager_resume --");

    de_manager m;
    DE_MANAGER_STORAGE(storage, 6, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e[4];
    for (int i = 0; i < 4; ++i)
        e[i] = de_manager_new(&m);

    de_entity_pause(e[1]);
    de_entity_pause(e[3]);

    // Ahora: 2 activas, 2 pausadas, 2 libres
    // de_manager_resume(&m);

    CHECK("size = 4", m.size == 4);
    CHECK("pause_index = capacity", m.pause_index == 6);
    CHECK("área activa contigua", all_active_entities_are_contiguous(&m));
    CHECK("no hay pausadas", m.capacity - m.pause_index == 0);
}

/* ============================================================
 * TEST 16: RESET
 * ============================================================ */

static void test_manager_reset(void)
{
    kprintf("-- test_manager_reset --");

    g_destructorCalls = 0;
    de_manager m;
    DE_MANAGER_STORAGE(storage, 5, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e[4];
    for (int i = 0; i < 4; ++i)
    {
        e[i] = de_manager_new(&m);
        e[i]->destructor = test_destructor;
    }

    de_entity_pause(e[2]);

    de_manager_reset(&m);

    CHECK("reset: size = 0", m.size == 0);
    CHECK("reset: pause_index = capacity", m.pause_index == 5);
    CHECK("reset: destructor llamado 4 veces", g_destructorCalls == 4);
}

/* ============================================================
 * TEST 17: MOVE FRONT/BACK
 * ============================================================ */

static void test_move_front_back(void)
{
    kprintf("-- test_move_front_back --");

    de_manager m;
    DE_MANAGER_STORAGE(storage, 5, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e[5];
    for (int i = 0; i < 5; ++i)
        e[i] = de_manager_new(&m);

    // Orden inicial de actualización: de size-1 a 0
    // Mover e[1] al frente => debe quedar en el índice size-1
    de_entity_move_front(e[1]);
    CHECK("move_front: e[1] en última posición activa", e[1]->slot == m.size - 1);

    // Mover e[4] al fondo => debe quedar en el índice 0
    de_entity_move_back(e[4]);
    CHECK("move_back: e[4] en primera posición activa", e[4]->slot == 0);
}

/* ============================================================
 * TEST 18: PAUSA/RESUME CON HUECOS LIBRES INSUFICIENTES
 * ============================================================ */

static void test_pause_no_free_slot(void)
{
    kprintf("-- test_pause_no_free_slot --");

    de_manager m;
    DE_MANAGER_STORAGE(storage, 3, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);
    de_entity e2 = de_manager_new(&m);

    // Pool lleno: no hay huecos libres
    // Intentar pausar debe fallar
    de_entity_pause(e1);

    CHECK("sin hueco libre: size sigue 3", m.size == 3);
    CHECK("sin hueco libre: pause_index sigue 3", m.pause_index == 3);
    CHECK("e1 sigue activa", entity_is_active(&m, e1));
}

/* ============================================================
 * TEST 19: RESUME SIN HUECOS LIBRES
 * ============================================================ */

static void test_resume_no_free_slot(void)
{
    kprintf("-- test_resume_no_free_slot --");

    de_manager m;
    DE_MANAGER_STORAGE(storage, 4, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e0 = de_manager_new(&m);
    de_entity e1 = de_manager_new(&m);
    de_entity e2 = de_manager_new(&m);
    de_entity e3 = de_manager_new(&m);

    // de_manager_pause(&m); // todo pausado, sin libres

    // Intentar resumir sin huecos libres
    de_entity_resume(e2);

    CHECK("sin hueco libre: size sigue 0", m.size == 0);
    CHECK("sin hueco libre: pause_index sigue 0", m.pause_index == 0);
    CHECK("e2 sigue pausada", entity_is_paused(&m, e2));
}

/* ============================================================
 * TEST 20: ESTRÉS DE PAUSA/RESUME/DELETE
 * ============================================================ */

static void test_stress_pause_resume_delete(void)
{
    kprintf("-- test_stress_pause_resume_delete --");

    de_manager m;
    DE_MANAGER_STORAGE(storage, 10, sizeof(struct MyComponent));
    de_manager_init(&m, DE_MANAGER_ARGS(storage));

    de_entity e[10];
    for (int i = 0; i < 10; ++i)
        e[i] = de_manager_new(&m);

    // Pausar pares
    for (int i = 0; i < 10; i += 2)
        de_entity_pause(e[i]);

    CHECK("size = 5", m.size == 5);
    CHECK("pause_index = 5", m.pause_index == 5);

    // Eliminar algunas pausadas
    de_entity_delete(e[0]);
    de_entity_delete(e[2]);

    CHECK("size = 5 (no cambia por borrar pausadas)", m.size == 5);
    CHECK("pause_index = 7 (dos huecos libres más)", m.pause_index == 7);

    // Reanudar una pausada
    de_entity_resume(e[8]);
    CHECK("size = 6", m.size == 6);
    CHECK("pause_index = 7", m.pause_index == 7);

    CHECK("área activa contigua", all_active_entities_are_contiguous(&m));
    CHECK("área pausada contigua", all_paused_entities_are_contiguous(&m));
}

/* ============================================================
 * ORQUESTADOR DE TESTS
 * ============================================================ */

void run_three_areas_tests(void)
{
    g_testsRun = 0;
    g_testsPassed = 0;

    kprintf("========== TESTS TRES ÁREAS ==========");

    test_init_pool_layout();
    test_new_entity_layout();
    test_full_capacity();
    test_pause_active_entity();
    test_resume_paused_entity();
    test_pause_resume_multiple();
    test_delete_active_entity();
    test_delete_paused_entity();
    test_delete_with_destructor();
    test_destructor_abort_delete();
    test_update_only_active();
    test_iteration_macros();
    test_apply_macros();
    test_manager_pause();
    test_manager_resume();
    test_manager_reset();
    test_move_front_back();
    test_pause_no_free_slot();
    test_resume_no_free_slot();
    test_stress_pause_resume_delete();

    kprintf("=======================================");
    kprintf("Resultado: %d/%d tests OK", g_testsPassed, g_testsRun);
}