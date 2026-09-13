#pragma once

#include <stdint.h>

typedef uint16_t dsmap8_handle_t;

#define DSMAP8_INVALID_HANDLE ((dsmap8_handle_t)0xFFFFu)

typedef struct
{
    char *pool;
    uint16_t *lookup;
    uint16_t *handles;
    uint16_t capacity;
    uint16_t size;
    uint16_t count;
} dsmap8_t;

#define DSMAP8_DECLARE(NAME, CAPACITY, TYPE) \
    struct                                   \
    {                                        \
        TYPE pool[(CAPACITY)];               \
        uint16_t lookup[(CAPACITY)];         \
        uint16_t handles[(CAPACITY)];        \
    } NAME

#define DSMAP8_ALLOC(ALLOC, CAPACITY, SIZE)                            \
    {                                                                  \
        .pool = (char *)(ALLOC)((CAPACITY) * (SIZE)),                  \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),  \
        .handles = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)), \
        .capacity = (CAPACITY),                                        \
        .size = (SIZE),                                                \
        .count = 0,                                                    \
    }

#define DSMAP8_FREE(FREE, MAP)  \
    do                          \
    {                           \
        (FREE)((MAP)->pool);    \
        (FREE)((MAP)->lookup);  \
        (FREE)((MAP)->handles); \
    } while (0)

#define DSMAP8_BIND(NAME)                                             \
    (dsmap8_t)                                                        \
    {                                                                 \
        .pool = (char *)(NAME).pool,                                  \
        .lookup = (NAME).lookup,                                      \
        .handles = (NAME).handles,                                    \
        .capacity = sizeof((NAME).lookup) / sizeof((NAME).lookup[0]), \
        .size = sizeof((NAME).pool[0]),                               \
        .count = 0,                                                   \
    }

#define DSMAP8_DATA(MAP, HANDLE) ((void *)((MAP)->pool + (MAP)->lookup[(HANDLE)]))

#define DSMAP8_FOREACH(MAP, CODE)                                         \
    do                                                                    \
    {                                                                     \
        char *_p = (MAP)->pool;                                           \
        for (uint16_t _i = 0; _i < (MAP)->count; _i++, _p += (MAP)->size) \
        {                                                                 \
            void *data = _p;                                              \
            CODE;                                                         \
        }                                                                 \
    } while (0)

static inline void dsmap8_init(dsmap8_t *map)
{
    map->count = 0;

    for (uint16_t i = 0; i < map->capacity; i++)
    {
        map->handles[i] = i;
        map->lookup[i] = i * map->size;
    }
}

static inline dsmap8_handle_t dsmap8_alloc(dsmap8_t *map)
{
    if (map->count >= map->capacity)
        return DSMAP8_INVALID_HANDLE;

    dsmap8_handle_t handle = map->handles[map->count];
    map->lookup[handle] = map->size * map->count++;

    return handle;
}

static inline uint16_t dsmap8_valid(dsmap8_t *map, dsmap8_handle_t handle)
{
    if (handle >= map->capacity)
        return 0;

    uint16_t offset = map->lookup[handle];

    if (offset >= map->count * map->size)
        return 0;

    return map->handles[offset / map->size] == handle;
}

static inline void dsmap8_remove(dsmap8_t *map, dsmap8_handle_t handle)
{
    uint16_t slot = map->lookup[handle] / map->size;
    uint16_t last = --map->count;

    if (slot != last)
    {
        dsmap8_handle_t moved_handle = map->handles[last];
        map->handles[slot] = moved_handle;
        map->lookup[moved_handle] = last * map->size;
    }

    map->handles[last] = handle;
}
