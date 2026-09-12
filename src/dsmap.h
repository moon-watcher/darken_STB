#pragma once

#include <stdint.h>

typedef uint16_t dsmap_handle_t;

#define DSMAP_INVALID_HANDLE ((dsmap_handle_t)0xFFFFu)

typedef struct
{
    char *pool;
    uint16_t *lookup;
    uint16_t *handles;
    void **addr;
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
        void *addr[(CAPACITY)];             \
    } NAME

#define DSMAP_ALLOC(ALLOC, CAPACITY, SIZE)                             \
    {                                                                  \
        .pool = (char *)(ALLOC)((CAPACITY) * (SIZE)),                  \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),  \
        .handles = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)), \
        .addr = (void **)(ALLOC)((CAPACITY) * sizeof(void *)),         \
        .capacity = (CAPACITY),                                        \
        .size = (SIZE),                                                \
        .count = 0,                                                    \
        .free_head = DSMAP_INVALID_HANDLE,                             \
    }

#define DSMAP_BIND(NAME)                                              \
    (dsmap_t)                                                         \
    {                                                                 \
        .pool = (char *)(NAME).pool,                                  \
        .lookup = (NAME).lookup,                                      \
        .handles = (NAME).handles,                                    \
        .addr = (NAME).addr,                                          \
        .capacity = sizeof((NAME).lookup) / sizeof((NAME).lookup[0]), \
        .size = sizeof((NAME).pool[0]),                               \
        .count = 0,                                                   \
        .free_head = DSMAP_INVALID_HANDLE,                            \
    }

#define DSMAP_FOREACH(MAP, CODE)                               \
    do                                                         \
    {                                                          \
        uint16_t _index = (MAP)->count;                        \
        while (_index--)                                       \
        {                                                      \
            void *_data = (MAP)->addr[(MAP)->handles[_index]]; \
            CODE;                                              \
        }                                                      \
    } while (0)

static inline void dsmap_init(dsmap_t *map)
{
    map->count = 0;
    map->free_head = 0;

    for (uint16_t i = 0; i < map->capacity; i++)
    {
        map->lookup[i] = i + 1;
        map->addr[i] = map->pool + ((uint32_t)i * map->size);
    }

    map->lookup[map->capacity - 1] = DSMAP_INVALID_HANDLE;
}

static inline dsmap_handle_t dsmap_alloc(dsmap_t *map)
{
    if (map->free_head == DSMAP_INVALID_HANDLE)
        return DSMAP_INVALID_HANDLE;

    dsmap_handle_t handle = map->free_head;
    map->free_head = map->lookup[handle];

    uint16_t slot = map->count++;

    map->lookup[handle] = slot;
    map->handles[slot] = handle;

    return handle;
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
    return dsmap_valid(map, handle) ? map->addr[handle] : 0;
}

static inline void *dsmap_remove(dsmap_t *map, dsmap_handle_t handle)
{
    if (!dsmap_valid(map, handle))
        return 0;

    uint16_t slot = map->lookup[handle];
    uint16_t last = --map->count;

    if (slot != last)
    {
        dsmap_handle_t moved_handle = map->handles[last];

        map->handles[slot] = moved_handle;
        map->lookup[moved_handle] = slot;
    }

    map->lookup[handle] = map->free_head;
    map->free_head = handle;

    return map->addr[handle];
}
