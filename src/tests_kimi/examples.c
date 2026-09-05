#include <genesis.h>

#include "../_bbb.h"
#include "examples.h"

struct MyComponent
{
    int x, y;
    uint8_t health;
};

static void bbb_update_idle(struct MyComponent *data)
{
    kprintf("Idle: (%d, %d) health=%d", data->x, data->y, data->health);
}

static void bbb_update_walk(struct MyComponent *data)
{
    data->x += 1;
    data->y += 1;
    kprintf("Walking: (%d, %d) health=%d", data->x, data->y, data->health);

    /* Ejemplo: cambiar a idle tras 5 pasos */
    if (data->x >= 5)
        bbb_entity_set_update(data, bbb_update_idle);
}

static void bbb_destructor(struct MyComponent *data)
{
    kprintf("Destructor llamado para entidad en (%d, %d)", data->x, data->y);
    kprintf("Destructor tag=%d slot=%d", bbb_this(data)->tag, bbb_this(data)->slot);
    bbb_entity_pause_data(data);   /* equivalente a bbb_entity_pause(BBB_DATA_GET_ENTITY(data)) */
}

void kimi_run_usage_example(void)
{
    kprintf("========== BBB EJEMPLO DE USO ==========");
    BBB_POOL_DECLARE(g_manager_storage, 10, sizeof(struct MyComponent));
    BBB_POOL_DECLARE(g_manager2_storage, 20, sizeof(struct MyComponent) + 73);
    bbb g_manager = BBB_POOL_BIND(g_manager_storage), g_manager2 = BBB_POOL_BIND(g_manager2_storage);

    bbb_init(&g_manager);
    bbb_init(&g_manager2);
    bbb_entity e1 = BBB_SPAWN(&g_manager);
    struct MyComponent *data1 = (struct MyComponent *)e1->data;
    data1->x = 0;
    data1->y = 0;
    data1->health = 100;
    e1->update = bbb_update_walk;
    e1->destroy = bbb_destructor;
    e1->tag = 1;
    bbb_entity e2 = BBB_SPAWN(&g_manager);
    struct MyComponent *data2 = (struct MyComponent *)e2->data;
    data2->x = 10;
    data2->y = 20;
    data2->health = 80;
    e2->update = bbb_update_idle;
    e2->destroy = bbb_destructor;
    e2->tag = 2;
    bbb_entity e3 = BBB_SPAWN(&g_manager);
    struct MyComponent *data3 = (struct MyComponent *)e3->data;
    data3->x = 5;
    data3->y = 5;
    data3->health = 50;
    e3->update = bbb_update_walk;
    e3->destroy = bbb_destructor;
    e3->tag = 3;
    kprintf("--- Frame 1 ---");
    bbb_update(&g_manager);
    BBB_FOREACH(&g_manager, if (_entity->tag == 2) bbb_entity_pause(_entity));
    // BBB_MANAGER_APPLY(&g_manager, ENTITY->tag == 2, bbb_entity_pause);
    // BBB_MANAGER_APPLY_PAUSED(&g_manager, ENTITY->tag == 2, bbb_entity_pause);
    kprintf("Pausada entidad tag 2");
    kprintf("--- Frame 2 ---");
    bbb_update(&g_manager);
    BBB_FOREACH(&g_manager, if (_entity->tag == 1) bbb_entity_pause(_entity));
    // BBB_MANAGER_APPLY(&g_manager, ENTITY->tag == 1, bbb_entity_delete);
    // BBB_MANAGER_APPLY_PAUSED(&g_manager, ENTITY->tag == 1, bbb_entity_delete);
    kprintf("--- Frame 3 ---");
    bbb_update(&g_manager);
    kprintf("--- Reset ---");
    bbb_reset(&g_manager);
    kprintf("Tamano final g_manager: %d", g_manager.size);
    kprintf("Tamano final g_manager2: %d", g_manager2.size);
    kprintf("=====================================");
}
