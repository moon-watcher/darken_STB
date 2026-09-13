#pragma once

#include <stdint.h>

typedef uint16_t dsmap_handle_t;

#define DSMAP_INVALID_HANDLE ((dsmap_handle_t)0xFFFFu)

typedef struct
{
    char *pool;
    char **ptrs;
    char **addrs;
    uint16_t *lookup;
    uint16_t *handles;
    uint16_t capacity;
    uint16_t size;
    uint16_t count;
} dsmap_t;

#define DSMAP_DECLARE(NAME, CAPACITY, TYPE) \
    struct                                  \
    {                                       \
        TYPE pool[(CAPACITY)];              \
        char *ptrs[(CAPACITY)];             \
        char *addrs[(CAPACITY)];            \
        uint16_t lookup[(CAPACITY)];        \
        uint16_t handles[(CAPACITY)];       \
    } NAME

#define DSMAP_ALLOC(ALLOC, CAPACITY, SIZE)                             \
    {                                                                  \
        .pool = (char *)(ALLOC)((CAPACITY) * (SIZE)),                  \
        .ptrs = (char **)(ALLOC)((CAPACITY) * sizeof(char *)),         \
        .addrs = (char **)(ALLOC)((CAPACITY) * sizeof(char *)),        \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),  \
        .handles = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)), \
        .capacity = (CAPACITY),                                        \
        .size = (SIZE),                                                \
        .count = 0,                                                    \
    }

#define DSMAP_FREE(FREE, MAP)   \
    do                          \
    {                           \
        (FREE)((MAP)->pool);    \
        (FREE)((MAP)->addrs);   \
        (FREE)((MAP)->ptrs);    \
        (FREE)((MAP)->lookup);  \
        (FREE)((MAP)->handles); \
    } while (0)

#define DSMAP_BIND(NAME)                                              \
    (dsmap_t)                                                         \
    {                                                                 \
        .pool = (char *)(NAME).pool,                                  \
        .ptrs = (NAME).ptrs,                                          \
        .addrs = (NAME).addrs,                                        \
        .lookup = (NAME).lookup,                                      \
        .handles = (NAME).handles,                                    \
        .capacity = sizeof((NAME).lookup) / sizeof((NAME).lookup[0]), \
        .size = sizeof((NAME).pool[0]),                               \
        .count = 0,                                                   \
    }

#define DSMAP_DATA(MAP, HANDLE) ((void *)((MAP)->addrs[(HANDLE)]))

#define DSMAP_FOREACH(MAP, CODE)                   \
    for (uint16_t _i = 0; _i < (MAP)->count; _i++) \
    {                                              \
        void *_data = (MAP)->ptrs[_i];             \
        CODE;                                      \
    }

static inline void dsmap_init(dsmap_t *map)
{
    map->count = 0;

    for (uint16_t i = 0; i < map->capacity; i++)
    {
        map->ptrs[i] = map->addrs[i] = map->pool + i * map->size;
        map->handles[i] = i;
    }
}

static inline dsmap_handle_t dsmap_alloc(dsmap_t *map)
{
    if (map->count >= map->capacity)
        return DSMAP_INVALID_HANDLE;

    dsmap_handle_t handle = map->handles[map->count];
    map->lookup[handle] = map->count++;

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

static inline void dsmap_remove(dsmap_t *map, dsmap_handle_t handle)
{
    uint16_t slot = map->lookup[handle];
    uint16_t last = --map->count;

    if (slot != last)
    {
        dsmap_handle_t moved_handle = map->handles[last];

        map->handles[slot] = moved_handle;
        map->lookup[moved_handle] = slot;
        map->ptrs[slot] = map->ptrs[last];
        map->ptrs[last] = map->addrs[handle];
    }

    map->handles[last] = handle;
}
