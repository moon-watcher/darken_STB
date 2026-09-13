#pragma once

#include <stdint.h>

typedef uint16_t dsmap0_handle_t;

#define DSMAP0_INVALID_HANDLE ((dsmap0_handle_t)0xFFFFu)

typedef struct
{
    char *pool;
    uint16_t *lookup;
    uint16_t *handles;

    uint16_t capacity;
    uint16_t size;
    uint16_t count;
    uint16_t free_head;

} dsmap0_t;

#define DSMAP0_DECLARE(NAME, CAPACITY, TYPE) \
    struct                                  \
    {                                       \
        TYPE pool[(CAPACITY)];              \
        uint16_t lookup[(CAPACITY)];        \
        uint16_t handles[(CAPACITY)];       \
    } NAME

#define DSMAP0_INIT(STORAGE, TYPE)                                           \
    {                                                                       \
        .pool = (char *)(STORAGE).pool,                                     \
        .lookup = (STORAGE).lookup,                                         \
        .handles = (STORAGE).handles,                                       \
        .capacity = sizeof((STORAGE).lookup) / sizeof((STORAGE).lookup[0]), \
        .size = sizeof(TYPE),                                               \
        .count = 0,                                                         \
        .free_head = DSMAP0_INVALID_HANDLE,                                  \
    }

#define DSMAP0_ALLOC(ALLOC, CAPACITY, SIZE)                             \
    {                                                                  \
        .pool = (char *)(ALLOC)((CAPACITY) * (SIZE)),                  \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),  \
        .handles = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)), \
        .capacity = (CAPACITY),                                        \
        .size = (SIZE),                                                \
        .count = 0,                                                    \
        .free_head = DSMAP0_INVALID_HANDLE,                             \
    }

#define DSMAP0_BIND(NAME, LOOKUP, HANDLES)                 \
    {                                                     \
        .pool = (char *)(NAME),                           \
        .lookup = (LOOKUP),                               \
        .handles = (HANDLES),                             \
        .capacity = sizeof(LOOKUP) / sizeof((LOOKUP)[0]), \
        .size = sizeof((NAME)[0]),                        \
        .count = 0,                                       \
        .free_head = DSMAP0_INVALID_HANDLE,                \
    }

static inline void dsmap0_swap(char *a, char *b, uint16_t size)
{
    while (size--)
    {
        char temp = *a;

        *a++ = *b;
        *b++ = temp;
    }
}

static inline void dsmap0_init(dsmap0_t *map)
{
    map->count = 0;
    map->free_head = DSMAP0_INVALID_HANDLE;

    if (!map->capacity)
        return;

    map->free_head = 0;

    for (uint16_t i = 0; i < map->capacity; i++)
        map->lookup[i] = i + 1;

    map->lookup[map->capacity - 1] = DSMAP0_INVALID_HANDLE;
}

static inline uint16_t dsmap0_valid(dsmap0_t *map, dsmap0_handle_t handle)
{
    if (handle >= map->capacity)
        return 0;

    uint16_t slot = map->lookup[handle];
    if (slot >= map->count)
        return 0;

    return map->handles[slot] == handle;
}

static inline void *dsmap0_data(dsmap0_t *map, dsmap0_handle_t handle)
{
    if (!dsmap0_valid(map, handle))
        return 0;

    return map->pool + ((uint32_t)map->lookup[handle] * map->size);
}

static inline dsmap0_handle_t dsmap0_alloc(dsmap0_t *map)
{
    if (map->count >= map->capacity || map->free_head == DSMAP0_INVALID_HANDLE)
        return DSMAP0_INVALID_HANDLE;

    dsmap0_handle_t handle = map->free_head;
    map->free_head = map->lookup[handle];

    uint16_t slot = map->count;
    map->count = slot + 1;
    map->lookup[handle] = slot;
    map->handles[slot] = handle;

    return handle;
}

static inline void *dsmap0_remove(dsmap0_t *map, dsmap0_handle_t handle)
{
    if (!dsmap0_valid(map, handle))
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

        dsmap0_handle_t moved_handle = map->handles[last];

        dsmap0_swap(data, last_data, map->size);

        map->handles[slot] = moved_handle;
        map->lookup[moved_handle] = slot;
    }

    map->lookup[handle] = map->free_head;
    map->free_head = handle;

    return map->pool + ((uint32_t)last * map->size);
}
