#pragma once

#include <stdint.h>

typedef uint16_t dsmap_handle_t;

typedef struct
{
    char *pool;
    uint16_t *lookup;
    uint16_t *handles;

    uint16_t capacity;
    uint16_t size;
    uint16_t count;
    uint16_t free_head;

    void (*copy)();
} dsmap_t;

#define DSMAP_INVALID_HANDLE ((dsmap_handle_t)0xFFFFu)

#define DSMAP_ALLOC(ALLOC, CAPACITY, SIZE, COPY)                       \
    {                                                                  \
        .pool = (char *)(ALLOC)((CAPACITY) * (SIZE)),                  \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),  \
        .handles = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)), \
        .capacity = (CAPACITY),                                        \
        .size = (SIZE),                                                \
        .copy = (COPY),                                                \
    }

#define DSMAP_DECLARE(NAME, CAPACITY, TYPE) \
    struct                                  \
    {                                       \
        uint16_t capacity;                  \
        TYPE pool[(CAPACITY)];              \
        uint16_t lookup[(CAPACITY)];        \
        uint16_t handles[(CAPACITY)];       \
    } NAME = {                              \
        .capacity = (CAPACITY),             \
    }

#define DSMAP_INIT(STORAGE, TYPE, COPY)                                     \
    {                                                                       \
        .pool = (char *)(STORAGE).pool,                                     \
        .lookup = (STORAGE).lookup,                                         \
        .handles = (STORAGE).handles,                                       \
        .capacity = sizeof((STORAGE).lookup) / sizeof((STORAGE).lookup[0]), \
        .size = sizeof(TYPE),                                               \
        .copy = (COPY),                                                     \
    }

#define DSMAP_BIND(NAME, LOOKUP, HANDLES, COPY)       \
    {                                                 \
        .pool = (char *)(NAME),                       \
        .lookup = (LOOKUP),                           \
        .handles = (HANDLES),                         \
        .capacity = sizeof(NAME) / sizeof((NAME)[0]), \
        .size = sizeof((NAME)[0]),                    \
        .count = 0,                                   \
        .free_head = 0,                               \
        .copy = (COPY),                               \
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
    return dsmap_valid(map, handle) ? map->pool + ((uint32_t)map->lookup[handle] * map->size) : 0;
}

static inline void dsmap_init(dsmap_t *map)
{
    map->count = 0;
    map->free_head = DSMAP_INVALID_HANDLE;

    if (map->capacity)
    {
        map->free_head = 0;

        for (uint16_t i = 0; i < map->capacity; i++)
            map->lookup[i] = i + 1;

        map->lookup[map->capacity - 1] = DSMAP_INVALID_HANDLE;
    }
}

static inline dsmap_handle_t dsmap_alloc(dsmap_t *map)
{
    if (map->count >= map->capacity || map->free_head == DSMAP_INVALID_HANDLE)
        return DSMAP_INVALID_HANDLE;

    dsmap_handle_t handle = map->free_head;

    map->free_head = map->lookup[handle];

    uint16_t slot = map->count++;

    map->handles[slot] = handle;
    map->lookup[handle] = slot;

    return handle;
}

static inline uint16_t dsmap_remove(dsmap_t *map, dsmap_handle_t handle)
{
    if (!dsmap_valid(map, handle))
        return 0;

    uint16_t slot = map->lookup[handle];
    uint16_t last = --map->count;

    if (slot != last)
    {
        void *to = map->pool + ((uint32_t)slot * map->size);
        void *from = map->pool + ((uint32_t)last * map->size);

        map->copy(to, from, map->size);

        map->handles[slot] = map->handles[last];
        map->lookup[map->handles[slot]] = slot;
    }

    map->lookup[handle] = map->free_head;
    map->free_head = handle;

    return 1;
}
