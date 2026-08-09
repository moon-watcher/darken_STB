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
 * TEST 1: alineacion
 * ============================================================ */

static void test_alignment(void)
{
    kprintf("-- test_alignment --");

    de_manager m1;
    de_manager_create(&m1, 8, sizeof(struct MyComponent));
    CHECK("payload par: todos los slots alineados a 4", all_slots_aligned(&m1));

    de_manager m2;
    de_manager_create(&m2, 8, sizeof(struct MyComponent) + 73);
    CHECK("payload impar (+73): todos los slots alineados a 4", all_slots_aligned(&m2));

    de_manager m3;
    de_manager_create(&m3, 8, 1);
    CHECK("payload=1: todos los slots alineados a 4", all_slots_aligned(&m3));
}

/* ============================================================
 * TEST 2: creacion / capacidad
 * ============================================================ */

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

    CHECK("de_manager_new devuelve entidad valida (1)", e0 != 0);
    CHECK("de_manager_new devuelve entidad valida (2)", e1 != 0);
    CHECK("de_manager_new devuelve entidad valida (3)", e2 != 0);
    CHECK("de_manager_new devuelve 0 al llenarse", e3 == 0);
    CHECK("size == capacity tras llenar", m.size == 3);
    CHECK("entidad nueva arranca en state delete", e0->state == de_state_delete);
    CHECK("entidad nueva arranca con tag 0", e0->tag == 0);
}

/* ============================================================
 * TEST 3: update, estados loop / transicion
 * ============================================================ */

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

    CHECK("de_state_loop no cambia el puntero de estado", e->state == (de_state)state_walk);
    CHECK("de_state_loop SI ejecuta la funcion cada vez (x3)", g_walkCalls == 3 && c->x == 3);

    de_entity *e2 = de_manager_new(&m);
    e2->state = (de_state)state_once_then_idle;
    de_manager_update(&m);
    CHECK("estado no-loop SI actualiza el puntero de estado", e2->state == (de_state)state_walk);
}

/* ============================================================
 * TEST 4: pause / resume
 * ============================================================ */

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
    CHECK("ambas activas: 2 llamadas en frame 1", g_idleCalls == 2);

    de_entity_pause(b);
    CHECK("tras pausar, b cae antes de pause_index", b->slot < m.pause_index);

    g_idleCalls = 0;
    de_manager_update(&m);
    CHECK("con b pausada: solo 1 llamada", g_idleCalls == 1);

    de_entity_resume(b);
    CHECK("tras resumir, b vuelve a estar en zona activa", b->slot >= m.pause_index);

    g_idleCalls = 0;
    de_manager_update(&m);
    CHECK("tras resumir: 2 llamadas otra vez", g_idleCalls == 2);
}

/* ============================================================
 * TEST 5: delete (via estado y directo) + destructor
 * ============================================================ */

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

    CHECK("delete via estado: size baja a 2", m.size == 2);
    CHECK("delete via estado: destructor llamado 1 vez", g_destructorCalls == 1);

    de_entity_delete(b);
    CHECK("delete directo: size baja a 1", m.size == 1);
    CHECK("delete directo: destructor llamado 2 veces en total", g_destructorCalls == 2);
}

/* ============================================================
 * TEST 6: apply / applyAll
 * ============================================================ */

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

    CHECK("apply: quedan 2 entidades (tags impares)", m.size == 2);

    bool onlyOdd = TRUE;
    de_manager_iterateAll(&m, {
        if ((ENTITY->tag % 2) == 0)
            onlyOdd = FALSE;
    });
    CHECK("apply: las que quedan tienen tag impar", onlyOdd);
}

/* ============================================================
 * TEST 7: reset
 * ============================================================ */

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

    CHECK("reset: size vuelve a 0", m.size == 0);
    CHECK("reset: destructor llamado para cada entidad", g_destructorCalls == 6);
}

/* ============================================================
 * TEST 8: destructor que ABORTA el delete
 * ============================================================ */

static int g_abortDestructorCalls = 0;

static void *state_abort_destructor(void *data)
{
    (void)data;
    ++g_abortDestructorCalls;
    return (void *)de_state_loop; /* aborta: la entidad sobrevive */
}

static void test_destructor_abort(void)
{
    kprintf("-- test_destructor_abort --");
    g_abortDestructorCalls = 0;

    de_manager m;
    de_manager_create(&m, 2, sizeof(struct MyComponent));

    de_entity *e = de_manager_new(&m);
    e->destructor = (de_state)state_abort_destructor;
    e->state = de_state_delete; /* marcada para morir */

    de_manager_update(&m);

    CHECK("destructor aborta: size sigue siendo 1", m.size == 1);
    CHECK("destructor aborta: destructor llamado 1 vez", g_abortDestructorCalls == 1);
    CHECK("destructor aborta: entidad sigue viva (slot valido)", e->slot < m.size);
    /* El destructor retorno de_state_loop, asi que el estado final es loop */
    CHECK("destructor aborta: estado cambio a loop", e->state == de_state_loop);
}

/* ============================================================
 * TEST 9: entidad que se auto-destruye desde su estado
 * ============================================================
 * Patron comun: una entidad decide durante su update que debe morir.
 * NOTA: darken.h marca para borrar en este frame, pero el borrado
 * efectivo ocurre en el SIGUIENTE update.
 */

static int g_selfKillCalls = 0;

static void *state_self_kill(void *data)
{
    (void)data;
    ++g_selfKillCalls;
    return (void *)de_state_delete; /* me quiero morir */
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
    b->state = (de_state)state_noop; /* sobrevive */

    /* Frame 1: a ejecuta su estado y retorna de_state_delete */
    de_manager_update(&m);

    CHECK("self-delete frame 1: estado ejecutado 1 vez", g_selfKillCalls == 1);
    CHECK("self-delete frame 1: a esta marcada como deleted", a->state == de_state_delete);
    CHECK("self-delete frame 1: size sigue siendo 2 (borrado diferido)", m.size == 2);

    /* Frame 2: de_manager_update ve a->state == delete y la elimina */
    de_manager_update(&m);

    CHECK("self-delete frame 2: size baja a 1", m.size == 1);
    CHECK("self-delete frame 2: la que queda es b", m.items[0] == b);
}

/* ============================================================
 * TEST 10: estabilidad de slots tras swaps
 * ============================================================ */

static void test_slot_stability(void)
{
    kprintf("-- test_slot_stability --");

    de_manager m;
    de_manager_create(&m, 4, sizeof(struct MyComponent));

    de_entity *e0 = de_manager_new(&m);
    de_entity *e1 = de_manager_new(&m);
    de_entity *e2 = de_manager_new(&m);
    de_entity *e3 = de_manager_new(&m);

    e0->tag = 0; e1->tag = 1; e2->tag = 2; e3->tag = 3;
    e0->state = (de_state)state_noop;
    e1->state = (de_state)state_noop;
    e2->state = (de_state)state_noop;
    e3->state = (de_state)state_noop;

    de_entity_pause(e1);
    de_entity_pause(e3);

    bool stable = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
        if (m.items[i]->slot != i)
            stable = FALSE;

    CHECK("slots coherentes tras pausar", stable);
    CHECK("pause_index vale 2", m.pause_index == 2);

    de_entity_delete(e0);

    stable = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
        if (m.items[i]->slot != i)
            stable = FALSE;

    CHECK("slots coherentes tras delete", stable);
    CHECK("size tras delete es 3", m.size == 3);
}

/* ============================================================
 * TEST 11: delete de entidad PAUSADA
 * ============================================================ */

static void test_delete_paused(void)
{
    kprintf("-- test_delete_paused --");

    de_manager m;
    de_manager_create(&m, 4, sizeof(struct MyComponent));

    de_entity *a = de_manager_new(&m);
    de_entity *b = de_manager_new(&m);
    de_entity *c = de_manager_new(&m);

    a->state = (de_state)state_noop;
    b->state = (de_state)state_noop;
    c->state = (de_state)state_noop;

    de_entity_pause(b);

    de_entity_delete(b);

    CHECK("delete pausado: size baja a 2", m.size == 2);
    CHECK("delete pausado: pause_index baja a 0", m.pause_index == 0);
    CHECK("delete pausado: a sigue en zona activa", a->slot >= m.pause_index);
    CHECK("delete pausado: c sigue en zona activa", c->slot >= m.pause_index);
}

/* ============================================================
 * TEST 12: applyAll con PAUSE
 * ============================================================ */

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

    CHECK("applyAll pause: pause_index vale 3", m.pause_index == 3);
    CHECK("applyAll pause: size sigue siendo 6", m.size == 6);

    bool correct = TRUE;
    de_manager_iterateAll(&m, {
        bool shouldBePaused = (ENTITY->tag % 2) == 0;
        bool isPaused = ENTITY->slot < m.pause_index;
        if (shouldBePaused != isPaused)
            correct = FALSE;
    });
    CHECK("applyAll pause: pares pausadas, impares activas", correct);
}

/* ============================================================
 * TEST 13: reutilizacion de slot tras delete
 * ============================================================ */

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
    CHECK("reuse: nueva entidad no es NULL", e1 != 0);
    CHECK("reuse: ocupa slot 0", e1->slot == 0);
    CHECK("reuse: size vuelve a 1", m.size == 1);

    struct MyComponent *d1 = (struct MyComponent *)e1->data;
    CHECK("reuse: datos antiguos siguen ahi (no zero-fill)", d1->x == 42);
}

/* ============================================================
 * TEST 14: stress mixto
 * ============================================================ */

static int g_stressCallsA = 0;
static int g_stressCallsB = 0;

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
    de_manager_create(&m, 5, sizeof(struct MyComponent));

    de_entity *e[5];
    for (int i = 0; i < 5; ++i)
    {
        e[i] = de_manager_new(&m);
        e[i]->tag = i;
        e[i]->state = (i % 2 == 0) ? (de_state)state_stress_a : (de_state)state_stress_b;
    }

    de_manager_update(&m);
    CHECK("stress: frame 1, 5 llamadas totales", g_stressCallsA + g_stressCallsB == 5);

    de_entity_pause(e[1]);
    e[3]->state = de_state_delete;
    g_stressCallsA = 0; g_stressCallsB = 0;
    de_manager_update(&m);

    CHECK("stress: frame 2, 3 activas (0,2,4)", g_stressCallsA + g_stressCallsB == 3);
    CHECK("stress: size tras delete es 4", m.size == 4);

    de_entity_resume(e[1]);
    g_stressCallsA = 0; g_stressCallsB = 0;
    de_manager_update(&m);

    CHECK("stress: frame 3, 4 activas de nuevo", g_stressCallsA + g_stressCallsB == 4);

    bool ok = TRUE;
    for (uint16_t i = 0; i < m.size; ++i)
        if (m.items[i]->slot != i)
            ok = FALSE;
    CHECK("stress: slots coherentes tras caos", ok);
}

/* ============================================================
 * TEST 15: integridad de datos tras swap
 * ============================================================ */

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

    da->x = 111; da->y = 222;
    db->x = 333; db->y = 444;
    dc->x = 555; dc->y = 666;

    a->state = (de_state)state_noop;
    b->state = (de_state)state_noop;
    c->state = (de_state)state_noop;

    de_entity_pause(b);
    de_entity_delete(a);

    CHECK("data integrity: a sigue teniendo sus datos",
          ((struct MyComponent *)a->data)->x == 111);
    CHECK("data integrity: b sigue teniendo sus datos",
          ((struct MyComponent *)b->data)->x == 333);
    CHECK("data integrity: c sigue teniendo sus datos",
          ((struct MyComponent *)c->data)->x == 555);
}

/* ============================================================
 * TEST 16: update de manager VACIO
 * ============================================================ */

static void test_empty_manager(void)
{
    kprintf("-- test_empty_manager --");

    de_manager m;
    de_manager_create(&m, 4, sizeof(struct MyComponent));

    CHECK("empty: size empieza en 0", m.size == 0);

    de_manager_update(&m);
    de_manager_pause(&m);
    de_manager_resume(&m);

    CHECK("empty: update no modifica size", m.size == 0);
    CHECK("empty: pause no rompe pause_index", m.pause_index == 0);
}

/* ============================================================
 * TEST 17: delete de la ULTIMA entidad
 * ============================================================ */

static void test_delete_last(void)
{
    kprintf("-- test_delete_last --");

    de_manager m;
    de_manager_create(&m, 4, sizeof(struct MyComponent));

    de_entity *e = de_manager_new(&m);
    e->state = (de_state)state_noop;

    de_entity_delete(e);

    CHECK("delete last: size es 0", m.size == 0);
    CHECK("delete last: pause_index es 0", m.pause_index == 0);
}

/* ============================================================
 * ORQUESTADOR
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
    kprintf("============================");
    kprintf("Resultado: %d/%d tests OK", g_testsPassed, g_testsRun);
}

/* ============================================================
 * BENCHMARKS
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

static void run_all_benchmarks(void)
{
    kprintf("========== BENCHMARKS ==========");
    bench_create_destroy();
    bench_update();
    bench_apply();
    bench_create_destroy_n(128, 500);
    bench_create_destroy_n(256, 250);
    bench_update_n(128, 500);
    bench_update_n(256, 250);
    bench_swap();
    kprintf("=================================");
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
    data1->x = 0;
    data1->y = 0;
    data1->health = 100;
    e1->state = (de_state)update_walk;
    e1->destructor = (de_state)destructor;
    e1->tag = 1;

    de_entity *e2 = de_manager_new(&g_manager);
    struct MyComponent *data2 = (struct MyComponent *)e2->data;
    data2->x = 10;
    data2->y = 20;
    data2->health = 80;
    e2->state = (de_state)update_idle;
    e2->destructor = (de_state)destructor;
    e2->tag = 2;

    de_entity *e3 = de_manager_new(&g_manager);
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
 * MAIN
 * ============================================================ */

int main(void)
{
    SYS_setVIntCallback(bench_vblank_cb);

    run_usage_example();
    run_all_tests();
    run_all_benchmarks();

    while (1)
        SYS_doVBlankProcess();

    return 0;
}