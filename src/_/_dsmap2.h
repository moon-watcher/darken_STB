#pragma once

#include <stdint.h>

typedef uint16_t dsmap2_handle_t;

#define DSMAP2_INVALID_HANDLE ((dsmap2_handle_t)0xFFFFu)

typedef struct
{
    char *pool;
    uint16_t *lookup;
    uint16_t *active;
    uint16_t capacity;
    uint16_t size;
    uint16_t count;
    uint16_t free_head;
} dsmap2_t;

#define DSMAP2_DECLARE(NAME, CAPACITY, TYPE) \
    struct                                   \
    {                                        \
        TYPE pool[(CAPACITY)];               \
        uint16_t lookup[(CAPACITY)];         \
        uint16_t active[(CAPACITY)];         \
    } NAME

#define DSMAP2_INIT(STORAGE, TYPE)                                          \
    {                                                                       \
        .pool = (char *)(STORAGE).pool,                                     \
        .lookup = (STORAGE).lookup,                                         \
        .active = (STORAGE).active,                                         \
        .capacity = sizeof((STORAGE).lookup) / sizeof((STORAGE).lookup[0]), \
        .size = sizeof(TYPE),                                               \
        .count = 0,                                                         \
        .free_head = DSMAP2_INVALID_HANDLE,                                 \
    }

#define DSMAP2_BIND(NAME, LOOKUP, ACTIVE)                 \
    {                                                     \
        .pool = (char *)(NAME),                           \
        .lookup = (LOOKUP),                               \
        .active = (ACTIVE),                               \
        .capacity = sizeof(LOOKUP) / sizeof((LOOKUP)[0]), \
        .size = sizeof((NAME)[0]),                        \
        .count = 0,                                       \
        .free_head = DSMAP2_INVALID_HANDLE,               \
    }

#define DSMAP2_ALLOC(ALLOC, CAPACITY, SIZE)                           \
    {                                                                 \
        .pool = (char *)(ALLOC)((CAPACITY) * (SIZE)),                 \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)), \
        .active = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)), \
        .capacity = (CAPACITY),                                       \
        .size = (SIZE),                                               \
        .count = 0,                                                   \
        .free_head = DSMAP2_INVALID_HANDLE,                           \
    }

static inline void dsmap2_init(dsmap2_t *map)
{
    map->count = 0;
    map->free_head = DSMAP2_INVALID_HANDLE;

    if (!map->capacity)
        return;

    map->free_head = 0;

    for (uint16_t i = 0; i < map->capacity; i++)
        map->lookup[i] = i + 1;

    map->lookup[map->capacity - 1] = DSMAP2_INVALID_HANDLE;
}

static inline uint16_t dsmap2_valid(dsmap2_t *map, dsmap2_handle_t handle)
{
    if (handle >= map->capacity)
        return 0;

    uint16_t index = map->lookup[handle];

    if (index >= map->count)
        return 0;

    return map->active[index] == handle;
}

static inline void *dsmap2_data(dsmap2_t *map, dsmap2_handle_t handle)
{
    if (!dsmap2_valid(map, handle))
        return 0;

    return map->pool + ((uint32_t)handle * map->size);
}

static inline dsmap2_handle_t dsmap2_alloc(dsmap2_t *map)
{
    if (map->count >= map->capacity || map->free_head == DSMAP2_INVALID_HANDLE)
        return DSMAP2_INVALID_HANDLE;

    dsmap2_handle_t handle = map->free_head;
    map->free_head = map->lookup[handle];

    uint16_t index = map->count++;
    map->active[index] = handle;
    map->lookup[handle] = index;

    return handle;
}

static inline void *dsmap2_remove(dsmap2_t *map, dsmap2_handle_t handle)
{
    if (!dsmap2_valid(map, handle))
        return 0;

    uint16_t index = map->lookup[handle];
    uint16_t last = --map->count;

    if (index != last)
    {
        dsmap2_handle_t moved_handle = map->active[last];
        map->active[index] = moved_handle;
        map->lookup[moved_handle] = index;
    }

    map->lookup[handle] = map->free_head;
    map->free_head = handle;

    return map->pool + ((uint32_t)handle * map->size);
}

/*
#include <genesis.h>
#include "dsmap2.h"

typedef struct
{
    uint16_t x;
    uint16_t y;
    uint16_t vx;
    uint16_t vy;
} Entity;

#define MAX_ENTITIES 128

DSMAP2_DECLARE(storage_dsmap2, MAX_ENTITIES, Entity);

int main(void)
{
    dsmap2_t map = DSMAP2_INIT(storage_dsmap2, Entity);
    dsmap2_init(&map);

    dsmap2_handle_t handle = dsmap2_alloc(&map);
    Entity *entity = dsmap2_data(&map, handle);

    if (entity)
    {
        entity->x = 100;
        entity->y = 50;
        entity->vx = 2;
        entity->vy = 1;
    }

    dsmap2_remove(&map, handle);

    while (1)
        SYS_doVBlankProcess();

    return 0;
}
*/