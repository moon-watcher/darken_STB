#include <genesis.h>

#include "../../darken.h"
#include "examples.h"

struct MyComponent
{
    int x, y;
    uint8_t health;
};

static void *darken_update_walk(struct MyComponent *data)
{
    data->x += 1;
    data->y += 1;
    kprintf("Walking: (%d, %d) health=%d", data->x, data->y, data->health);
    return DARKEN_LOOP;
}

static void *darken_update_idle(struct MyComponent *data)
{
    kprintf("Idle: (%d, %d) health=%d", data->x, data->y, data->health);
    return DARKEN_LOOP;
}

static void *darken_destructor(struct MyComponent *data)
{
    kprintf("Destructor llamado para entidad en (%d, %d)", data->x, data->y);
    return DARKEN_PAUSE;
}

void darken_run_usage_example(void)
{
    kprintf("========== EJEMPLO DE USO ==========");
    struct darken g_manager, g_manager2;
    DARKEN_STORAGE(g_manager_storage, 10, sizeof(struct MyComponent));
    darken_init(&g_manager, DARKEN_ARGS(g_manager_storage));
    DARKEN_STORAGE(g_manager2_storage, 20, sizeof(struct MyComponent) + 73);
    darken_init(&g_manager2, DARKEN_ARGS(g_manager2_storage));
    darken_entity e1 = darken_spawn(&g_manager);
    struct MyComponent *data1 = (struct MyComponent *)e1->data;
    data1->x = 0;
    data1->y = 0;
    data1->health = 100;
    e1->state = (darken_state)darken_update_walk;
    e1->destructor = (darken_state)darken_destructor;
    e1->tag = 1;
    darken_entity e2 = darken_spawn(&g_manager);
    struct MyComponent *data2 = (struct MyComponent *)e2->data;
    data2->x = 10;
    data2->y = 20;
    data2->health = 80;
    e2->state = (darken_state)darken_update_idle;
    e2->destructor = (darken_state)darken_destructor;
    e2->tag = 2;
    darken_entity e3 = darken_spawn(&g_manager);
    struct MyComponent *data3 = (struct MyComponent *)e3->data;
    data3->x = 5;
    data3->y = 5;
    data3->health = 50;
    e3->state = (darken_state)darken_update_walk;
    e3->destructor = (darken_state)darken_destructor;
    e3->tag = 3;
    kprintf("--- Frame 1 ---");
    darken_update(&g_manager);
    DARKEN_FOREACH(&g_manager, if (ENTITY->tag == 2) darken_entity_pause(ENTITY));
    // DARKEN_MANAGER_APPLY(&g_manager, ENTITY->tag == 2, darken_entity_pause);
    // DARKEN_MANAGER_APPLY_PAUSED(&g_manager, ENTITY->tag == 2, darken_entity_pause);
    kprintf("Pausada entidad tag 2");
    kprintf("--- Frame 2 ---");
    darken_update(&g_manager);
    DARKEN_FOREACH(&g_manager, if (ENTITY->tag == 1) darken_entity_pause(ENTITY));
    // DARKEN_MANAGER_APPLY(&g_manager, ENTITY->tag == 1, darken_entity_delete);
    // DARKEN_MANAGER_APPLY_PAUSED(&g_manager, ENTITY->tag == 1, darken_entity_delete);
    kprintf("--- Frame 3 ---");
    darken_update(&g_manager);
    kprintf("--- Reset ---");
    darken_reset(&g_manager);
    kprintf("Tamano final g_manager: %d", g_manager.size);
    kprintf("Tamano final g_manager2: %d", g_manager2.size);
    kprintf("=====================================");
}
