#pragma once

#include <stdint.h>

#define VSMAP_INVALID_HANDLE 0xFFFF

typedef struct
{
    struct vsmap_item_t
    {
        void *value;
        uint16_t handle;
    } *pool; // Pool denso de elementos activos.

    uint16_t *lookup;
    uint16_t capacity; // Número máximo de elementos.
    uint16_t count;    // Número de elementos activos.
    uint16_t free_head;

} vsmap_t;

/*
 * Reserva dinámicamente pool y lookup.
 *
 * Ejemplo:
 *
 *     vsmap_t map = VSMAP_ALLOC(malloc, 100);
 *
 * La liberación es responsabilidad del usuario:
 *
 *     free(map.pool);
 *     free(map.lookup);
 *
 * CAPACITY debe ser <= 65534.
 */
#define VSMAP_ALLOC(ALLOC, CAPACITY)                                                    \
    {                                                                                   \
        .pool = (struct vsmap_item_t *)ALLOC((CAPACITY) * sizeof(struct vsmap_item_t)), \
        .lookup = (uint16_t *)ALLOC((CAPACITY) * sizeof(uint16_t)),                     \
        .capacity = (CAPACITY),                                                         \
        .count = 0,                                                                     \
        .free_head = VSMAP_INVALID_HANDLE,                                              \
    }

/*
 * Declara almacenamiento estático.
 *
 * Ejemplo:
 *
 *     VSMAP_DECLARE(entities, 100);
 *     vsmap_t map = VSMAP_BIND(entities);
 */
#define VSMAP_DECLARE(NAME, CAPACITY)          \
    struct vsmap_item_t NAME##_pool[CAPACITY]; \
    uint16_t NAME##_lookup[CAPACITY]

/*
 * Une el almacenamiento declarado mediante VSMAP_DECLARE()
 * a un vsmap_t.
 */
#define VSMAP_BIND(NAME)                                          \
    {                                                             \
        .pool = NAME##_pool,                                      \
        .lookup = NAME##_lookup,                                  \
        .capacity = sizeof(NAME##_pool) / sizeof(NAME##_pool[0]), \
        .count = 0,                                               \
        .free_head = VSMAP_INVALID_HANDLE,                        \
    }

/* ============================================================================
 * API
 * ========================================================================== */

uint16_t vsmap_add(vsmap_t *, void *);
void *vsmap_data(vsmap_t *, uint16_t);
int16_t vsmap_valid(vsmap_t *, uint16_t);
int16_t vsmap_remove(vsmap_t *, uint16_t);
void vsmap_reset(vsmap_t *);

//
// Itera sobre todos los punteros activos.
// ITEM debe ser una variable ya declarada.
//
// Ejemplo:
//     void *item;
//     VSMAP_FOREACH(&map, item,
//     {
//         Entity *entity = item;
//         ...
//     });
//
// NO eliminar elementos dentro de CODE.
//
#define VSMAP_FOREACH(MAP, ITEM, CODE)               \
    do                                               \
    {                                                \
        vsmap_t *_map = (MAP);                       \
        uint16_t _i;                                 \
                                                     \
        for (_i = 0; _map && _i < _map->count; ++_i) \
        {                                            \
            ITEM = _map->pool[_i].value;             \
            CODE;                                    \
        }                                            \
    } while (0)

/* ============================================================================
 * IMPLEMENTATION
 * ========================================================================== */

#ifdef VSMAP_IMPLEMENTATION

static void _vsmap_build_free_list(vsmap_t *map)
{
    if (map->capacity == 0)
    {
        map->free_head = VSMAP_INVALID_HANDLE;
        return;
    }

    for (uint16_t i = 0; i < map->capacity - 1; ++i)
        map->lookup[i] = i + 1;

    map->lookup[map->capacity - 1] = VSMAP_INVALID_HANDLE;

    map->free_head = 0;
}

static void _vsmap_init_free_list(vsmap_t *map)
{
    if (map->free_head != VSMAP_INVALID_HANDLE)
        return;

    if (map->count >= map->capacity)
        return;

    _vsmap_build_free_list(map);
}

uint16_t vsmap_add(vsmap_t *map, void *value)
{
    if (map->capacity >= VSMAP_INVALID_HANDLE)
        return VSMAP_INVALID_HANDLE;

    if (map->count >= map->capacity)
        return VSMAP_INVALID_HANDLE;

    _vsmap_init_free_list(map);

    if (map->free_head == VSMAP_INVALID_HANDLE)
        return VSMAP_INVALID_HANDLE;

    uint16_t handle = map->free_head;

    map->free_head = map->lookup[handle];

    uint16_t slot = map->count++;

    map->pool[slot].value = value;
    map->pool[slot].handle = handle;
    map->lookup[handle] = slot;

    return handle;
}

void *vsmap_data(vsmap_t *map, uint16_t handle)
{
    if (handle == VSMAP_INVALID_HANDLE)
        return 0;

    if (handle >= map->capacity)
        return 0;

    uint16_t slot = map->lookup[handle];

    if (slot >= map->count)
        return 0;

    if (map->pool[slot].handle != handle)
        return 0;

    return map->pool[slot].value;
}

int16_t vsmap_valid(vsmap_t *map, uint16_t handle)
{
    if (handle == VSMAP_INVALID_HANDLE)
        return -1;

    if (handle >= map->capacity)
        return -2;

    uint16_t slot = map->lookup[handle];

    if (slot >= map->count)
        return -3;

    if (map->pool[slot].handle != handle)
        return -4;

    return 1;
}

int16_t vsmap_remove(vsmap_t *map, uint16_t handle)
{
    if (handle == VSMAP_INVALID_HANDLE)
        return -1;

    if (handle >= map->capacity)
        return -2;

    uint16_t slot = map->lookup[handle];

    if (slot >= map->count)
        return -3;

    if (map->pool[slot].handle != handle)
        return -4;

    uint16_t last = map->count - 1;

    if (slot != last)
    {
        map->pool[slot] = map->pool[last];
        map->lookup[map->pool[slot].handle] = slot;
    }

    map->count = last;
    map->lookup[handle] = map->free_head;
    map->free_head = handle;

    return slot;
}

void vsmap_reset(vsmap_t *map)
{
    map->count = 0;
    _vsmap_build_free_list(map);
}

#endif // VSMAP_IMPLEMENTATION
