#pragma once

#include <stdint.h>

typedef uint8_t dsmap8_handle_t;

#define DSMAP8_INVALID_HANDLE ((dsmap8_handle_t)0xFFu)

typedef struct
{
    char *pool;
    char **ptrs;
    char **addrs;
    uint8_t *lookup;
    uint8_t *handles;
    uint8_t capacity;
    uint16_t size;
    uint8_t count;
} dsmap8_t;

#define DSMAP8_DECLARE(NAME, CAPACITY, TYPE) \
    struct                                   \
    {                                        \
        TYPE pool[(CAPACITY)];               \
        char *ptrs[(CAPACITY)];              \
        char *addrs[(CAPACITY)];             \
        uint8_t lookup[(CAPACITY)];          \
        uint8_t handles[(CAPACITY)];         \
    } NAME

#define DSMAP8_ALLOC(ALLOC, CAPACITY, SIZE)                          \
    {                                                                \
        .pool = (char *)(ALLOC)((CAPACITY) * (SIZE)),                \
        .ptrs = (char **)(ALLOC)((CAPACITY) * sizeof(char *)),       \
        .addrs = (char **)(ALLOC)((CAPACITY) * sizeof(char *)),      \
        .lookup = (uint8_t *)(ALLOC)((CAPACITY) * sizeof(uint8_t)),  \
        .handles = (uint8_t *)(ALLOC)((CAPACITY) * sizeof(uint8_t)), \
        .capacity = (CAPACITY),                                      \
        .size = (SIZE),                                              \
        .count = 0,                                                  \
    }

#define DSMAP8_FREE(FREE, MAP)                            \
    do                                                    \
    {                                                     \
        (FREE)((MAP)->pool);                              \
        (FREE)((MAP)->addrs);                             \
        (FREE)((MAP)->ptrs);                              \
        (FREE)((MAP)->lookup);                            \
        (FREE)((MAP)->handles);                           \
        (MAP)->pool = 0;                                  \
        (MAP)->addrs = (MAP)->ptrs = 0;                   \
        (MAP)->lookup = (MAP)->handles = 0;               \
        (MAP)->capacity = (MAP)->size = (MAP)->count = 0; \
    } while (0)

#define DSMAP8_BIND(NAME)                                             \
    (dsmap8_t)                                                        \
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

#define DSMAP8_DATA(MAP, HANDLE) ((void *)((MAP)->addrs[(HANDLE)]))

#define DSMAP8_FOREACH(MAP, CODE)                  \
    for (uint16_t _i = 0; _i < (MAP)->count; _i++) \
    {                                              \
        void *_data = (MAP)->ptrs[_i];             \
        CODE;                                      \
    }

static inline void dsmap8_init(dsmap8_t *map)
{
    map->count = 0;

    for (uint16_t i = 0; i < map->capacity; i++)
    {
        map->ptrs[i] = map->addrs[i] = map->pool + i * map->size;
        map->handles[i] = i;
    }
}

static inline dsmap8_handle_t dsmap8_alloc(dsmap8_t *map)
{
    if (map->count >= map->capacity)
        return DSMAP8_INVALID_HANDLE;

    dsmap8_handle_t handle = map->handles[map->count];

    map->lookup[handle] = map->count;
    map->count++;

    return handle;
}

static inline uint8_t dsmap8_valid(dsmap8_t *map, dsmap8_handle_t handle)
{
    if (handle >= map->capacity)
        return 0;

    uint16_t slot = map->lookup[handle];

    if (slot >= map->count)
        return 0;

    return map->handles[slot] == handle;
}

static inline void dsmap8_remove(dsmap8_t *map, dsmap8_handle_t handle)
{
    uint16_t slot = map->lookup[handle];
    uint16_t last = --map->count;

    if (slot != last)
    {
        dsmap8_handle_t moved_handle = map->handles[last];

        map->handles[slot] = moved_handle;
        map->lookup[moved_handle] = slot;
        map->ptrs[slot] = map->ptrs[last];
    }
    map->ptrs[last] = map->addrs[handle];

    map->handles[last] = handle;
}
