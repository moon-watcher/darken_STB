#pragma once

#include <stdint.h>

typedef uint16_t dsmap_handle_t;

#define DSMAP_INVALID_HANDLE ((dsmap_handle_t)0xFFFFu)

typedef struct
{
    char *pool;
    uint16_t *lookup;
    uint16_t *handles;

    uint16_t capacity;
    uint16_t size;
    uint16_t count;
    uint16_t free_head;

} dsmap_t;

#define DSMAP_DECLARE(NAME, CAPACITY, TYPE) \
    struct                                  \
    {                                       \
        TYPE pool[(CAPACITY)];              \
        uint16_t lookup[(CAPACITY)];        \
        uint16_t handles[(CAPACITY)];       \
    } NAME

#define DSMAP_INIT(STORAGE, TYPE)                                           \
    {                                                                       \
        .pool = (char *)(STORAGE).pool,                                     \
        .lookup = (STORAGE).lookup,                                         \
        .handles = (STORAGE).handles,                                       \
        .capacity = sizeof((STORAGE).lookup) / sizeof((STORAGE).lookup[0]), \
        .size = sizeof(TYPE),                                               \
        .count = 0,                                                         \
        .free_head = DSMAP_INVALID_HANDLE,                                  \
    }

#define DSMAP_ALLOC(ALLOC, CAPACITY, SIZE)                             \
    {                                                                  \
        .pool = (char *)(ALLOC)((CAPACITY) * (SIZE)),                  \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),  \
        .handles = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)), \
        .capacity = (CAPACITY),                                        \
        .size = (SIZE),                                                \
        .count = 0,                                                    \
        .free_head = DSMAP_INVALID_HANDLE,                             \
    }

#define DSMAP_BIND(NAME, LOOKUP, HANDLES)                 \
    {                                                     \
        .pool = (char *)(NAME),                           \
        .lookup = (LOOKUP),                               \
        .handles = (HANDLES),                             \
        .capacity = sizeof(LOOKUP) / sizeof((LOOKUP)[0]), \
        .size = sizeof((NAME)[0]),                        \
        .count = 0,                                       \
        .free_head = DSMAP_INVALID_HANDLE,                \
    }

static inline void dsmap_swap(char *a, char *b, uint16_t size)
{
    while (size--)
    {
        char temp = *a;

        *a++ = *b;
        *b++ = temp;
    }
}

static inline void dsmap_init(dsmap_t *map)
{
    map->count = 0;
    map->free_head = DSMAP_INVALID_HANDLE;

    if (!map->capacity)
        return;

    map->free_head = 0;

    for (uint16_t i = 0; i < map->capacity; i++)
        map->lookup[i] = i + 1;

    map->lookup[map->capacity - 1] = DSMAP_INVALID_HANDLE;
}

static inline uint16_t dsmap_valid(dsmap_t *map, dsmap_handle_t handle)
{
    if (handle >= map->capacity)
        return 0;

    uint16_t slot = map->lookup[handle];
    if (slot >= map->count)
        return 0;

    return map->handles[slot] == handle;
}

static inline void *dsmap_data(dsmap_t *map, dsmap_handle_t handle)
{
    if (!dsmap_valid(map, handle))
        return 0;

    return map->pool + ((uint32_t)map->lookup[handle] * map->size);
}

static inline dsmap_handle_t dsmap_alloc(dsmap_t *map)
{
    if (map->count >= map->capacity || map->free_head == DSMAP_INVALID_HANDLE)
        return DSMAP_INVALID_HANDLE;

    dsmap_handle_t handle = map->free_head;
    map->free_head = map->lookup[handle];

    uint16_t slot = map->count;
    map->count = slot + 1;
    map->lookup[handle] = slot;
    map->handles[slot] = handle;

    return handle;
}

static inline void *dsmap_remove(dsmap_t *map, dsmap_handle_t handle)
{
    if (!dsmap_valid(map, handle))
        return 0;

    uint16_t slot = map->lookup[handle];

    if (slot >= map->count)
        return 0;

    if (map->handles[slot] != handle)
        return 0;

    uint16_t last = map->count - 1;

    map->count = last;

    if (slot != last)
    {
        char *data = map->pool + ((uint32_t)slot * map->size);
        char *last_data = map->pool + ((uint32_t)last * map->size);

        dsmap_handle_t moved_handle = map->handles[last];

        dsmap_swap(data, last_data, map->size);

        map->handles[slot] = moved_handle;
        map->lookup[moved_handle] = slot;
    }

    map->lookup[handle] = map->free_head;
    map->free_head = handle;

    return map->pool + ((uint32_t)last * map->size);
}
