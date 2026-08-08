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

/* Comprueba que TODOS los slots reservados por el manager caen en una
 * direccion multiplo de 4 -- exactamente lo que costo tanto arreglar. */
static bool all_slots_aligned(de_manager *mgr)
{
    for (uint16_t i = 0; i < mgr->capacity; ++i)
        if (((u32)mgr->items[i]) & 3)
            return FALSE;
    return TRUE;
}

/* ============================================================
 * TEST 1: alineacion (payload par e impar)
 * ============================================================ */

static void test_alignment(void)
{
    kprintf("-- test_alignment --");

    de_manager m1;
    de_manager_create(&m1, 8, sizeof(struct MyComponent)); /* payload par */
    CHECK("payload par: todos los slots alineados a 4", all_slots_aligned(&m1));

    de_manager m2;
    de_manager_create(&m2, 8, sizeof(struct MyComponent) + 73); /* payload impar */
    CHECK("payload impar (+73): todos los slots alineados a 4", all_slots_aligned(&m2));

    de_manager m3;
    de_manager_create(&m3, 8, 1); /* payload minimo, impar */
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
    de_entity *e3 = de_manager_new(&m); /* deberia fallar: capacidad 3 */

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
    return (void *)de_state_loop; /* se queda en este mismo estado */
}

static void *state_once_then_idle(void *data)
{
    (void)data;
    return (void *)state_walk; /* la proxima vez pasa a state_walk */
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
    de_manager_update(&m); /* debe transicionar a state_walk */
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

    a->state = de_state_delete; /* se borrara en el proximo update */
    de_manager_update(&m);

    CHECK("delete via estado: size baja a 2", m.size == 2);
    CHECK("delete via estado: destructor llamado 1 vez", g_destructorCalls == 1);

    de_entity_delete(b); /* borrado directo, sin pasar por update */
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

    /* Borra solo las entidades con tag par (0, 2, 4) */
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
    kprintf("============================");
    kprintf("Resultado: %d/%d tests OK", g_testsPassed, g_testsRun);
}

/* ============================================================
 * BENCHMARKS
 * ============================================================
 * Cuenta frames (VBlank) transcurridos alrededor de cada operacion.
 * Como cada operacion individual dura muchisimo menos que un frame,
 * repetimos cada una muchas veces (REPS) para obtener una medida.
 *
 * OJO: SYS_setVIntCallback es el nombre habitual en SGDK para
 * registrar un callback de VBlank, pero puede variar segun tu version
 * de SGDK -- si no compila, mira en sys.h cual es el nombre exacto en
 * la tuya y sustituyelo aqui.
 */

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

static void run_all_benchmarks(void)
{
    kprintf("========== BENCHMARKS ==========");
    bench_create_destroy();
    bench_update();
    bench_apply();
    kprintf("=================================");
}

/* ============================================================
 * EJEMPLO DE USO NORMAL (igual que en tu main original)
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

    de_manager_iterateAll(&g_manager, {
        if (ENTITY->tag == 2)
        {
            de_entity_pause(ENTITY);
            kprintf("Pausada entidad tag %d", ENTITY->tag);
        }
    });

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
    SYS_setVIntCallback(bench_vblank_cb); /* ajusta el nombre si tu SGDK difiere */

    run_usage_example();
    run_all_tests();
    run_all_benchmarks();

    while (1)
        SYS_doVBlankProcess();

    return 0;
}