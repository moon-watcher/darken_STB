#include <genesis.h>
#include "../../darken.h"
#include "../../darksys.h"
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
    struct de_manager entities, systems;
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
    CHECK("entity systems: manager de sistemas tiene 3 entidades", systems.size == 3);
    CHECK("entity systems: entidades normales siguen intactas", entities.size == 2);
}

static void test_entity_system_shared_data(void)
{
    kprintf("-- test_entity_system_shared_data --");
    struct de_manager entities, systems;
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
    struct de_manager entities, systems;
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
    CHECK("paused entity: sigue pausada en su manager", paused_entity->slot >= entities.paused);
}

typedef struct TestDeSystemEntity
{
    int16_t x, y;
    int16_t vx, vy;
    uint16_t frame;
} TestDeSystemEntity;

static void test_de_system_movement(de_system system)
{
    DE_SYSTEM_FOREACH(system, int16_t *x, int16_t *y, int16_t *vx, int16_t *vy, {
        *x += *vx;
        *y += *vy;
    });
}

static void test_de_system_physics(de_system system)
{
    DE_SYSTEM_FOREACH(system, int16_t *vy, {
        *vy += 1;
    });
}

static void test_de_system_frames(de_system system)
{
    DE_SYSTEM_FOREACH(system, uint16_t *frame, {
        *frame += 1;
    });
}

static void test_de_system_init_add(void)
{
    kprintf("-- test_de_system_init_add --");
    struct de_system sys;
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
    struct de_system sys;
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
    struct de_system sys;
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
    struct de_system sys;
    void *pool[8];
    de_system_init(&sys, pool, 2, 4);
    int a[2], b[2], c[2], d[2];
    CHECK("de_system capacity: primer grupo", DE_SYSTEM_ADD(&sys, &a[0], &a[1], &b[0], &b[1]) == 1);
    CHECK("de_system capacity: segundo grupo", DE_SYSTEM_ADD(&sys, &b[0], &b[1], &c[0], &c[1]) == 1);
    CHECK("de_system capacity: rechaza grupo lleno", DE_SYSTEM_ADD(&sys, &c[0], &c[1], &d[0], &d[1]) == 0);
    kprintf("de_system capacity: size: %d", sys.size);
    CHECK("de_system capacity: size no cambia", sys.size == 8);
}

static void test_de_system_as_entities(void)
{
    kprintf("-- test_de_system_as_entities --");

    struct de_manager entities, systems;
    DE_MANAGER_STORAGE(entities_storage, 2, sizeof(TestDeSystemEntity));
    de_manager_init(&entities, DE_MANAGER_ARGS(entities_storage));
    DE_MANAGER_STORAGE(systems_storage, 3, sizeof(struct de_system));
    de_manager_init(&systems, DE_MANAGER_ARGS(systems_storage));

    de_entity frames_entity = de_manager_new(&systems);
    de_entity movement_entity = de_manager_new(&systems);
    de_entity physics_entity = de_manager_new(&systems);

    de_system frames = (de_system)frames_entity->data;
    de_system movement = (de_system)movement_entity->data;
    de_system physics = (de_system)physics_entity->data;

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
    CHECK("de_system entities: manager contiene 3 sistemas", systems.size == 3);
    CHECK("de_system entities: manager de entidades intacto", entities.size == 2);
}

static void test_de_system_shared_payload(void)
{
    kprintf("-- test_de_system_shared_payload --");
    struct de_manager entities, systems;
    DE_MANAGER_STORAGE(entities_storage, 1, sizeof(TestDeSystemEntity));
    de_manager_init(&entities, DE_MANAGER_ARGS(entities_storage));
    DE_MANAGER_STORAGE(systems_storage, 2, sizeof(struct de_system));
    de_manager_init(&systems, DE_MANAGER_ARGS(systems_storage));
    void *movement_pool[4], *frames_pool[1];
    de_entity movement_entity = de_manager_new(&systems), frames_entity = de_manager_new(&systems);
    de_system movement = (de_system)movement_entity->data;
    de_system frames = (de_system)frames_entity->data;
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

struct de_system sys;

static void test_system_foreach_direct(void)
{
    kprintf("-- test_system_foreach_direct --");

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
    struct de_system sys1, sys2, sys3, sys5;
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
    struct de_system sys;
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
    struct de_system sys;
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
    struct de_system sys;
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

void ds_run_all_tests(void)
{
    g_testsRun = 0;
    g_testsPassed = 0;
    kprintf("========== TESTS ==========");
    test_de_system_init_add();
    test_de_system_multiple_groups();
    test_de_system_remove();
    test_de_system_capacity();
    test_de_system_as_entities();
    test_de_system_shared_payload();
    test_entity_system_basic();
    test_entity_system_shared_data();
    test_entity_system_paused_entity();
    test_system_foreach_direct();
    test_system_add_various_arity();
    test_system_remove_first();
    test_system_remove_last();
    test_empty_system();

    kprintf("============================");
    kprintf("Resultado: %d/%d tests OK", g_testsPassed, g_testsRun);
}