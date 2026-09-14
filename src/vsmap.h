#pragma once

#include <stdint.h>

typedef uint16_t vsmap_handle_t;

typedef struct
{
    void *value;    // caller's pointer; opaque to us
    uint16_t index; // which `lookup` slot this pool entry currently represents
} vsmap_item_t;

typedef struct
{
    vsmap_item_t *pool; // pool[0, count) — live elements, no gaps
    uint16_t *lookup;   // see the big comment above: dense position, or free-list link
    uint16_t capacity;  // must be <= 0xFFFE
    uint16_t free_head; // head of the free list, or VSMAP_INVALID_HANDLE if empty
    uint16_t count;
} vsmap_t;

#define VSMAP_INVALID_HANDLE ((vsmap_handle_t)0xFFFFu)

/* ============================================================================
 * PUBLIC API — all static inline, always available, nothing to compile separately
 * ============================================================================ */

// Free .pool / .lookup
//
//     vsmap_t map = VSMAP_ALLOC(malloc, 100);
//     vsmap_init(&map);
//     ...
//     free(map.pool);
//     free(map.lookup);
#define VSMAP_ALLOC(ALLOC, CAPACITY)                                        \
    {                                                                       \
        .pool = (vsmap_item_t *)(ALLOC)((CAPACITY) * sizeof(vsmap_item_t)), \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),       \
        .capacity = (CAPACITY),                                             \
    }

// Static allocation with automatic storage duration (stack or global).
//
//     VSMAP_DECLARE(storage, 100);
//     vsmap_t map = VSMAP_BIND(storage);
//     vsmap_init(&map);
#define VSMAP_DECLARE(NAME, CAPACITY)  \
    struct                             \
    {                                  \
        uint16_t capacity;             \
        vsmap_item_t pool[(CAPACITY)]; \
        uint16_t lookup[(CAPACITY)];   \
    } NAME = {                         \
        .capacity = (CAPACITY),        \
    }

// Static/global initialization: compile-time constants.
#define VSMAP_INIT(STORAGE)                                                 \
    {                                                                       \
        .pool = (STORAGE).pool,                                             \
        .lookup = (STORAGE).lookup,                                         \
        .capacity = sizeof((STORAGE).lookup) / sizeof((STORAGE).lookup[0]), \
    }

// Runtime binding: locals, reassignment, any context.
#define VSMAP_BIND(NAME)             \
    {                                \
        .pool = (NAME).pool,         \
        .lookup = (NAME).lookup,     \
        .capacity = (NAME).capacity, \
    }

// Visits every live value, from last to first (safe to vsmap_remove() the current ITEM's
// handle from inside CODE — same swap-with-last compaction trick as darken.h's
// DARKEN_FOREACH, and safe for the exact same reason: whatever gets swapped into the slot
// you just vacated was already visited, or is about to be).
// Bound to `item` (a `vsmap_item_t *`) inside CODE; `item->value` is your pointer.
#define VSMAP_FOREACH(MAP, CODE)                      \
    do                                                \
    {                                                 \
        uint16_t _index = (MAP)->count;               \
        if (_index)                                   \
        {                                             \
            vsmap_item_t *_pool = (MAP)->pool;        \
            while (_index--)                          \
            {                                         \
                vsmap_item_t *item = &_pool[_index];  \
                CODE;                                 \
            }                                         \
        }                                             \
    } while (0)

#define VSMAP_ITEM(MAP, H) (MAP)->pool[(MAP)->lookup[H]]

// Must be called once after ALLOC/BIND, before the first vsmap_add(). Also  doubles as a
// full reset: call it again any time to drop every element and start over (every handle
// issued before that call is no longer valid — same "everything gets freed" contract as
// darken_reset()).
static inline void vsmap_init(vsmap_t *map)
{
    map->count = 0;
    map->free_head = VSMAP_INVALID_HANDLE;

    if (map->capacity)
    {
        map->free_head = 0;

        for (uint16_t i = 0; i < map->capacity; i++)
            map->lookup[i] = i + 1; // chain every slot into the free list...

        map->lookup[map->capacity - 1] = VSMAP_INVALID_HANDLE; // ...terminated here
    }
}

// Adds `value`, returns its handle, or VSMAP_INVALID_HANDLE if the pool is full.
static inline vsmap_handle_t vsmap_add(vsmap_t *map, void *value)
{
    if (map->count >= map->capacity || map->free_head == VSMAP_INVALID_HANDLE)
        return VSMAP_INVALID_HANDLE;

    uint16_t index = map->free_head;
    map->free_head = map->lookup[index]; // pop the free list

    uint16_t slot = map->count++;
    map->pool[slot].value = value;
    map->pool[slot].index = index;
    map->lookup[index] = slot;

    return index;
}

// True if `handle` currently refers to a live element.
static inline uint16_t vsmap_valid(vsmap_t *map, vsmap_handle_t h)
{
    if (h >= map->capacity)
        return 0;

    uint16_t slot = map->lookup[h];
    if (slot >= map->count)
        return 0;

    return map->pool[slot].index == h;
}

// Returns the value for `handle`, or NULL if it's not currently valid.
// NOTE: this is ambiguous if you legitimately store NULL as a value — use vsmap_valid()
// when you need to tell "invalid handle" apart from "valid handle whose value happens
// to be NULL".
static inline void *vsmap_data(vsmap_t *map, vsmap_handle_t h)
{
    return vsmap_valid(map, h) ? VSMAP_ITEM(map, h).value : 0;
}

// Removes `handle` if valid and returns the value it held, or NULL (and does nothing)
// if the handle was already invalid.
static inline void *vsmap_remove(vsmap_t *map, vsmap_handle_t h)
{
    if (!vsmap_valid(map, h))
        return 0;

    uint16_t slot = map->lookup[h];
    void *value = map->pool[slot].value;

    uint16_t last = --map->count;
    if (slot != last)
    {
        map->pool[slot] = map->pool[last];
        map->lookup[map->pool[slot].index] = slot;
    }

    map->lookup[h] = map->free_head; // push back onto the free list
    map->free_head = h;

    return value;
}
