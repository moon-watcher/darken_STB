/**
 * vsmap.h — stable handle -> pointer table, with a dense swap-compact array
 *
 * vsmap-1.0.0
 *
 * Portable C99, no GNU extensions required — add()/data()/remove() are
 * plain functions, not statement-expression macros.
 *
 *
 *
 * What this is for
 * ==================================================
 *
 * You have objects that live somewhere else — individually allocated,
 * owned by another pool, static globals, whatever — and you want to hand
 * out a small, stable reference to each one instead of a raw pointer:
 * something you can store in a save file, pass across a message, or just
 * keep around without worrying that the object moved or was freed out
 * from under you. vsmap gives you that: vsmap_add() returns an opaque
 * vsmap_handle, vsmap_data() turns it back into your pointer (or NULL if
 * the handle is no longer valid), and vsmap_remove() frees the slot.
 *
 * If you also want the map to *own* the memory of what it stores (fixed
 * per-element size, no external pointer to manage, plus per-frame
 * update() callbacks), that's a different data structure — see darken.h,
 * which is built for exactly that instead.
 *
 *
 *
 * Design: dense array + sparse index
 * ==================================================
 *
 * Every handle is a uint16_t index.
 *
 * `index` is a stable slot number in [0, capacity) that never changes for
 * as long as that particular element is alive, and gets reused (via a
 * free list) once it's removed.
 *
 * Three parallel, capacity-sized arrays back this:
 *
 *     pool[0, count)    — the live elements themselves, packed with no
 *                          gaps: { value, index }. Iterate this directly
 *                          (or with VSMAP_FOREACH) to visit every live
 *                          value.
 *     lookup[index]     — while `index` is ALIVE: which position in
 *                          `pool` currently holds it.
 *                          while `index` is FREE: the next free index, so
 *                          the free list is threaded through this same
 *                          array at zero extra memory cost.
 *
 * Removing an element swaps the last live entry in `pool` into the
 * vacated slot, fixes up `lookup[]` for whichever element just moved,
 * and pushes the freed index back onto the free list.
 *
 * Capacity tops out at 0xFFFE: index 0xFFFF is reserved as the
 * free-list terminator.
 */

#pragma once

#include <stdint.h>

typedef uint16_t vsmap_handle;

#define VSMAP_INVALID_HANDLE ((vsmap_handle)0xFFFFu)

typedef struct
{
    struct vsmap_item
    {
        void *value;    // caller's pointer; opaque to us
        uint16_t index; // which `lookup` slot this pool entry currently represents
    } *pool;            // pool[0, count) — live elements, no gaps

    uint16_t *lookup;  // see the big comment above: dense position, or free-list link
    uint16_t capacity; // must be <= 0xFFFE
    uint16_t count;
    uint16_t free_head; // head of the free list, or VSMAP_NIL if empty
} vsmap_t;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

// Must be called once after ALLOC/BIND, before the first vsmap_add().
void vsmap_init(vsmap_t *);

// Adds `value`, returns its handle, or VSMAP_INVALID_HANDLE if the pool is full.
vsmap_handle vsmap_add(vsmap_t *, void *value);

// Returns the value for `handle`, or NULL if it's not currently valid.
// NOTE: this is ambiguous if you legitimately store NULL as a value —
// use vsmap_valid() when you need to tell "invalid handle" apart from
// "valid handle whose value happens to be NULL".
void *vsmap_data(vsmap_t *, vsmap_handle);

// True if `handle` currently refers to a live element.
uint16_t vsmap_valid(vsmap_t *, vsmap_handle);

// Removes `handle` if valid and returns the value it held, or NULL
// (and does nothing) if the handle was already invalid.
void *vsmap_remove(vsmap_t *, vsmap_handle);

// Visits every live value, from last to first (safe to vsmap_remove()
// the current ITEM's handle from inside CODE — same swap-with-last
// compaction trick as darken.h's DARKEN_FOREACH, and safe for the exact
// same reason: whatever gets swapped into the slot you just vacated was
// already visited, or is about to be).
// Bound to `_item` (a `struct vsmap_item *`) inside CODE; `_item->value`
// is your pointer.
#define VSMAP_FOREACH(MAP, CODE)                           \
    do                                                     \
    {                                                      \
        uint16_t _index = (MAP)->count;                    \
        if (_index)                                        \
        {                                                  \
            struct vsmap_item *_pool = (MAP)->pool;        \
            while (_index--)                               \
            {                                              \
                struct vsmap_item *_item = &_pool[_index]; \
                CODE;                                      \
            }                                              \
        }                                                  \
    } while (0)

// Dynamic allocation: use with malloc/calloc or a custom allocator.
// Call vsmap_init() once afterwards.
//
//     vsmap_t map = VSMAP_ALLOC(malloc, 100);
//     vsmap_init(&map);
//     ...
//     free(map.pool);
//     free(map.lookup);
#define VSMAP_ALLOC(ALLOC, CAPACITY)                                                  \
    {                                                                                 \
        .pool = (struct vsmap_item *)(ALLOC)((CAPACITY) * sizeof(struct vsmap_item)), \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),                 \
        .capacity = (CAPACITY),                                                       \
    }

// Static allocation with automatic storage duration (stack or global).
//
//     VSMAP_DECLARE(storage, 100);
//     vsmap_t map = VSMAP_BIND(storage);
//     vsmap_init(&map);
#define VSMAP_DECLARE(NAME, CAPACITY)       \
    struct                                  \
    {                                       \
        uint16_t capacity;                  \
        struct vsmap_item pool[(CAPACITY)]; \
        uint16_t lookup[(CAPACITY)];        \
    } NAME = {                              \
        .capacity = (CAPACITY),             \
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

/* ============================================================================
 * PRIVATE
 * ============================================================================ */

#define _VSMAP_NIL VSMAP_INVALID_HANDLE

#ifdef VSMAP_IMPLEMENTATION

void vsmap_init(vsmap_t *map)
{
    map->count = 0;
    map->free_head = map->capacity ? 0 : VSMAP_INVALID_HANDLE;

    for (uint16_t i = 0; i < map->capacity; i++)
        map->lookup[i] = i + 1; // chain every slot into the free list...

    if (map->capacity)
        map->lookup[map->capacity - 1] = VSMAP_INVALID_HANDLE; // ...terminated here
}

vsmap_handle vsmap_add(vsmap_t *map, void *value)
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

void *vsmap_data(vsmap_t *map, vsmap_handle h)
{
    if (h >= map->capacity)
        return 0;

    uint16_t slot = map->lookup[h];

    if (slot >= map->count)
        return 0;

    return map->pool[slot].value;
}

uint16_t vsmap_valid(vsmap_t *map, vsmap_handle h)
{
    if (h >= map->capacity)
        return 0;

    return map->lookup[h] < map->count;
}

void *vsmap_remove(vsmap_t *map, vsmap_handle h)
{
    if (h >= map->capacity)
        return 0;

    uint16_t slot = map->lookup[h];

    if (slot >= map->count)
        return 0;

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

#endif // VSMAP_IMPLEMENTATION
