#pragma once

#include <stdint.h>

// #define DSMAP3_STABLE

/**
 * DSMAP3_STABLE:
 * Use when objects must never move and removals are frequent.
 * The object stays at pool[handle], while handles[] keeps iteration dense.
 *
 * Default mode:
 * Use when objects are updated/iterated frequently.
 * The pool stays physically dense, giving faster sequential iteration.
 *
 * Stable mode makes remove() much cheaper, but iteration is slower.
 */

typedef uint16_t dsmap3_handle_t;

#define DSMAP3_INVALID_HANDLE ((dsmap3_handle_t)0xFFFFu)

typedef struct
{
    char *pool;
    uint16_t *lookup;
    uint16_t *handles;
    uint16_t capacity;
    uint16_t size;
    uint16_t count;
    uint16_t free_head;
} dsmap3_t;

#define DSMAP3_DECLARE(NAME, CAPACITY, TYPE) \
    struct                                   \
    {                                        \
        TYPE pool[(CAPACITY)];               \
        uint16_t lookup[(CAPACITY)];         \
        uint16_t handles[(CAPACITY)];        \
    } NAME

#define DSMAP3_INIT(STORAGE, TYPE)                                          \
    {                                                                       \
        .pool = (char *)(STORAGE).pool,                                     \
        .lookup = (STORAGE).lookup,                                         \
        .handles = (STORAGE).handles,                                       \
        .capacity = sizeof((STORAGE).lookup) / sizeof((STORAGE).lookup[0]), \
        .size = sizeof(TYPE),                                               \
        .count = 0,                                                         \
        .free_head = DSMAP3_INVALID_HANDLE,                                 \
    }

#define DSMAP3_BIND(NAME, LOOKUP, HANDLES)                \
    {                                                     \
        .pool = (char *)(NAME),                           \
        .lookup = (LOOKUP),                               \
        .handles = (HANDLES),                             \
        .capacity = sizeof(LOOKUP) / sizeof((LOOKUP)[0]), \
        .size = sizeof((NAME)[0]),                        \
        .count = 0,                                       \
        .free_head = DSMAP3_INVALID_HANDLE,               \
    }

#define DSMAP3_ALLOC(ALLOC, CAPACITY, SIZE)                            \
    {                                                                  \
        .pool = (char *)(ALLOC)((CAPACITY) * (SIZE)),                  \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),  \
        .handles = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)), \
        .capacity = (CAPACITY),                                        \
        .size = (SIZE),                                                \
        .count = 0,                                                    \
        .free_head = DSMAP3_INVALID_HANDLE,                            \
    }

static inline void dsmap3_init(dsmap3_t *map)
{
    map->count = 0;
    map->free_head = DSMAP3_INVALID_HANDLE;

    if (!map->capacity)
        return;

    map->free_head = 0;

    for (uint16_t i = 0; i < map->capacity; i++)
        map->lookup[i] = i + 1;

    map->lookup[map->capacity - 1] = DSMAP3_INVALID_HANDLE;
}

static inline dsmap3_handle_t dsmap3_alloc(dsmap3_t *map)
{
    if (map->count >= map->capacity)
        return DSMAP3_INVALID_HANDLE;

    dsmap3_handle_t handle = map->free_head;

    if (handle == DSMAP3_INVALID_HANDLE)
        return DSMAP3_INVALID_HANDLE;

    map->free_head = map->lookup[handle];

    uint16_t slot = map->count++;

    map->lookup[handle] = slot;
    map->handles[slot] = handle;

    return handle;
}

static inline uint16_t dsmap3_valid(dsmap3_t *map, dsmap3_handle_t handle)
{
    if (handle >= map->capacity)
        return 0;

    uint16_t slot = map->lookup[handle];

    if (slot >= map->count)
        return 0;

    return map->handles[slot] == handle;
}

static inline void *dsmap3_data(dsmap3_t *map, dsmap3_handle_t handle)
{
    if (!dsmap3_valid(map, handle))
        return 0;

    uint32_t value = map->lookup[handle];

#ifdef DSMAP3_STABLE
    value = handle;
#endif

    return map->pool + ((uint32_t)value * map->size);
}

static inline void *dsmap3_remove(dsmap3_t *map, dsmap3_handle_t handle)
{
    if (!dsmap3_valid(map, handle))
        return 0;

    uint16_t slot = map->lookup[handle];
    uint16_t last = --map->count;

    if (slot != last)
    {
        dsmap3_handle_t moved_handle = map->handles[last];

#ifndef DSMAP3_STABLE
        char *data = map->pool + ((uint32_t)slot * map->size);
        char *last_data = map->pool + ((uint32_t)last * map->size);
        uint16_t size = map->size;

        while (size--)
        {
            char temp = *data;
            *data++ = *last_data;
            *last_data++ = temp;
        }
#endif

        map->handles[slot] = moved_handle;
        map->lookup[moved_handle] = slot;
    }

    map->lookup[handle] = map->free_head;
    map->free_head = handle;

    uint32_t value = last;

#ifdef DSMAP3_STABLE
    value = handle;
#endif

    return map->pool + ((uint32_t)value * map->size);
}
