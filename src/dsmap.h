#pragma once

#include <stdint.h>

/*
 * dsmap - Data Slot Map
 * ============================================================================
 *
 * Stores fixed-size data directly inside a contiguous memory pool and assigns
 * a stable handle to each active element.
 *
 * The handle does NOT identify the physical slot directly.
 * The slot is resolved through the lookup table:
 *
 *     handle -> lookup[handle] -> slot -> pool[slot]
 *
 * This separation allows the pool to stay compact by moving the last active
 * element into the slot of a removed element.
 *
 * The moved element keeps its handle, so its handle remains valid even though
 * its physical slot changes.
 *
 * Example:
 *     int16_t f = dsmap_alloc(&pool);
 *     Entity *entity = dsmap_data(&pool, f);
 *     entity->id = 5;
 *     dsmap_remove(&pool, c);
 *     entity = dsmap_data(&pool, f);
 *
 * "f" still refers to the same element even if that element was moved to a
 * different physical slot by dsmap_remove().
 *
 *
 * INTERNAL STORAGE
 *     pool:       Raw contiguous storage containing the actual elements.
 *     lookup:     Maps a handle to the current physical slot:
 *         lookup[handle] = slot
 *
 *     handles:    Stores the handle associated with each physical slot:
 *         handles[slot] = handle
 *
 *         This is used to verify that lookup[handle] still refers to the
 *         expected element.
 *
 *     free_head:  First handle in the free-handle chain.
 *     free_count: Number of handles currently available for reuse.
 *
 *
 * STATE
 *     capacity: Maximum number of elements.
 *     size:     Size of each element in bytes.
 *     count:    Number of currently active elements.
 *     next:     Next handle to allocate when there are no reusable handles.
 */

typedef struct
{
    char *pool;

    uint16_t *lookup;  // lookup[handle] returns the current physical slot of the element.
    uint16_t *handles; // handles[slot] stores the handle currently assigned to that slot.

    uint16_t capacity;   // Maximum number of elements the pool can contain.
    uint16_t size;       // Size of each element in bytes.
    uint16_t count;      // Number of currently active elements.
    uint16_t next;       // Next handle to be assigned.
    uint16_t free_head;  // First handle available for reuse.
    uint16_t free_count; // Number of handles available for reuse.

    void (*copy)();
} dsmap_t;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

// Creates a dsmap_t using dynamically allocated memory.
//     ALLOC    = memory allocation function.
//     CAPACITY = maximum number of elements.
//     SIZE     = size of each element in bytes.
//     COPY     = function used to copy elements during remove.
//
// Example:
//     dsmap_t pool = DSMAP_ALLOC(MEM_alloc, 100, sizeof(Entity), memcpy);
//     int16_t handle = dsmap_alloc(&pool);
//     Entity *entity = dsmap_data(&pool, handle);
//     ...
//     MEM_free(pool.pool);
//     MEM_free(pool.lookup);
//     MEM_free(pool.handles);
#define DSMAP_ALLOC(ALLOC, CAPACITY, SIZE, COPY)           \
    {                                                      \
        .pool = (ALLOC)((CAPACITY) * (SIZE)),              \
        .lookup = (ALLOC)((CAPACITY) * sizeof(uint16_t)),  \
        .handles = (ALLOC)((CAPACITY) * sizeof(uint16_t)), \
        .capacity = (CAPACITY),                            \
        .size = (SIZE),                                    \
        .count = 0,                                        \
        .next = 0,                                         \
        .free_head = 0,                                    \
        .free_count = 0,                                   \
        .copy = (COPY),                                    \
    }

// Creates a dsmap_t using externally provided static storage.
//     NAME    = data storage.
//     LOOKUP  = handle-to-slot lookup storage.
//     HANDLES = slot-to-handle storage.
//     COPY    = function used to copy elements during remove.
//
// Example:
//     dsmap_t pool = DSMAP_BIND(storage, lookup, handles, memcpy);
#define DSMAP_BIND(NAME, LOOKUP, HANDLES, COPY)       \
    {                                                 \
        .pool = (char *)(NAME),                       \
        .lookup = (LOOKUP),                           \
        .handles = (HANDLES),                         \
        .capacity = sizeof(NAME) / sizeof((NAME)[0]), \
        .size = sizeof((NAME)[0]),                    \
        .count = 0,                                   \
        .next = 0,                                    \
        .free_head = 0,                               \
        .free_count = 0,                              \
        .copy = (COPY),                               \
    }

int16_t dsmap_alloc(dsmap_t *);
void *dsmap_data(dsmap_t *, uint16_t);
int16_t dsmap_remove(dsmap_t *, uint16_t);
void dsmap_clear(dsmap_t *);

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DSMAP_IMPLEMENTATION

int16_t dsmap_alloc(dsmap_t *p)
{
    if (p->count >= p->capacity)
        return -1;

    uint16_t handle;
    uint16_t slot = p->count++;

    if (p->free_count)
    {
        handle = p->free_head;
        p->free_head = p->lookup[handle];
        --p->free_count;
    }
    else
    {
        if (p->next >= p->capacity)
        {
            --p->count;
            return -2;
        }

        handle = p->next++;
    }

    p->handles[slot] = handle;
    p->lookup[handle] = slot;

    return handle;
}

void *dsmap_data(dsmap_t *p, uint16_t handle)
{
    if (handle >= p->next)
        return 0;

    uint16_t slot = p->lookup[handle];

    if (slot >= p->count || p->handles[slot] != handle)
        return 0;

    return p->pool + (slot * p->size);
}

int16_t dsmap_remove(dsmap_t *p, uint16_t handle)
{
    if (handle >= p->next)
        return -1;

    uint16_t slot = p->lookup[handle];

    if (slot >= p->count)
        return -2;

    if (p->handles[slot] != handle)
        return -3;

    --p->count;

    if (slot != p->count)
    {
        void *to = p->pool + (slot * p->size);
        void *from = p->pool + (p->count * p->size);

        p->copy(to, from, p->size);

        p->handles[slot] = p->handles[p->count];
        p->lookup[p->handles[slot]] = slot;
    }

    p->lookup[handle] = p->free_head;
    p->free_head = handle;
    ++p->free_count;

    return p->count;
}

void dsmap_clear(dsmap_t *p)
{
    p->count = 0;
    p->next = 0;
    p->free_head = 0;
    p->free_count = 0;
}

#endif // DSMAP_IMPLEMENTATION
