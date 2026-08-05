#include <genesis.h>
#include "darken.h"
// #include "darken.h"

/* ============================================================
 * Definición del componente (payload)
 * ============================================================ */

struct MyComponent
{
    int x, y;
    uint8_t health;
};

/* ============================================================
 * Funciones de estado (update)
 * ============================================================ */

void *update_walk(struct MyComponent *data)
{
    data->x += 1;
    data->y += 1;
    kprintf("Walking: (%d, %d) health=%d", data->x, data->y, data->health);
    return 1;
}

void *update_idle(struct MyComponent *data)
{
    kprintf("Idle: (%d, %d) health=%d", data->x, data->y, data->health);
    return 1;
}

void *destructor(struct MyComponent *data)
{
    kprintf("Destructor llamado para entidad en (%d, %d)", data->x, data->y);
    return de_state_delete; // confirmar eliminación
}

de_manager g_manager;

int main(void)
{
    struct DE_MANAGER_STORAGE(10, sizeof(struct MyComponent)) storage;
    de_manager_init(&g_manager, DE_MANAGER_DEFINITION(storage));

    // 3. Crear algunas entidades
    de_entity *e1 = de_manager_new(&g_manager);
    struct MyComponent *data1 = e1->data;
    data1->x = 0;
    data1->y = 0;
    data1->health = 100;
    e1->state = (de_state)update_walk;
    e1->destructor = destructor;
    e1->tag = 1;

    de_entity *e2 = de_manager_new(&g_manager);
    struct MyComponent *data2 = e2->data;
    data2->x = 10;
    data2->y = 20;
    data2->health = 80;
    e2->state = (de_state)update_idle;
    e2->destructor = destructor;
    e2->tag = 2;

    de_entity *e3 = de_manager_new(&g_manager);
    struct MyComponent *data3 = e3->data;
    data3->x = 5;
    data3->y = 5;
    data3->health = 50;
    e3->state = (de_state)update_walk;
    e3->destructor = destructor;
    e3->tag = 3;

    // 4. Simular unos cuantos frames de actualización
    kprintf("--- Frame 1 ---");
    de_manager_update(&g_manager);

    // 5. Pausar la entidad con tag 2
    de_manager_iterateAll(&g_manager, {
        if (ENTITY->tag == 2)
        {
            de_entity_pause(ENTITY);
            kprintf("Pausada entidad tag %d", ENTITY->tag);
        }
    });

    kprintf("--- Frame 2 ---");
    de_manager_update(&g_manager);

    // 6. Eliminar la entidad con tag 1 usando apply (seguro)
    de_manager_applyAll(&g_manager, ENTITY->tag == 1, de_entity_delete);

    kprintf("--- Frame 3 ---");
    de_manager_update(&g_manager);

    // 7. Eliminar todas las entidades restantes con reset
    kprintf("--- Reset ---");
    de_manager_reset(&g_manager);

    // 8. Verificar que no quedan entidades
    kprintf("Tamaño final: %d", g_manager.size);

    return 0;
}