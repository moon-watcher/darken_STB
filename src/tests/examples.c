#include <genesis.h>

#include "../darken.h"
#include "examples.h"

struct MyComponent
{
    int x, y;
    uint8_t health;
};

static void *update_walk(struct MyComponent *data)
{
    data->x += 1;
    data->y += 1;
    kprintf("Walking: (%d, %d) health=%d", data->x, data->y, data->health);
    return DE_STATE_LOOP;
}

static void *update_idle(struct MyComponent *data)
{
    kprintf("Idle: (%d, %d) health=%d", data->x, data->y, data->health);
    return DE_STATE_LOOP;
}

static void *destructor(struct MyComponent *data)
{
    kprintf("Destructor llamado para entidad en (%d, %d)", data->x, data->y);
    return DE_STATE_DELETE;
}

void run_usage_example(void)
{
    kprintf("========== EJEMPLO DE USO ==========");
    de_manager g_manager, g_manager2;
    DE_MANAGER_STORAGE(g_manager_storage, 10, sizeof(struct MyComponent));
    de_manager_init(&g_manager, DE_MANAGER_ARGS(g_manager_storage));
    DE_MANAGER_STORAGE(g_manager2_storage, 20, sizeof(struct MyComponent) + 73);
    de_manager_init(&g_manager2, DE_MANAGER_ARGS(g_manager2_storage));
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
    DE_MANAGER_ITERATE(&g_manager, if (ENTITY->tag == 2) de_entity_pause(ENTITY));
    // DE_MANAGER_APPLY(&g_manager, ENTITY->tag == 2, de_entity_pause);
    // DE_MANAGER_APPLY_PAUSED(&g_manager, ENTITY->tag == 2, de_entity_pause);
    kprintf("Pausada entidad tag 2");
    kprintf("--- Frame 2 ---");
    de_manager_update(&g_manager);
    DE_MANAGER_ITERATE(&g_manager, if (ENTITY->tag == 1) de_entity_pause(ENTITY));
    // DE_MANAGER_APPLY(&g_manager, ENTITY->tag == 1, de_entity_delete);
    // DE_MANAGER_APPLY_PAUSED(&g_manager, ENTITY->tag == 1, de_entity_delete);
    kprintf("--- Frame 3 ---");
    de_manager_update(&g_manager);
    kprintf("--- Reset ---");
    de_manager_reset(&g_manager);
    kprintf("Tamano final g_manager: %d", g_manager.active_count);
    kprintf("Tamano final g_manager2: %d", g_manager2.active_count);
    kprintf("=====================================");
}
