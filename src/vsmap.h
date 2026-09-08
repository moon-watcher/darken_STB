#pragma once

#include <stdint.h>

/*
 * SMap - Slot Map
 *
 * Stores pointers associated with stable handles.
 *
 * The handle does NOT directly identify the physical slot of an element.
 * It is used as an index into "lookup" to obtain the element's current slot.
 *
 *    handle -> lookup[handle] -> slot -> pool[slot].value
 *
 * This allows the pool to remain compact by using swap-remove when elements
 * are deleted, while keeping the handles of the remaining elements stable.
 *
 * Example:
 *     int16_t f = vsmap_add(&pool, &entity);
 *     Entity *entity = vsmap_data(&pool, f);
 *     vsmap_remove(&pool, c);
 *     entity = vsmap_data(&pool, f);
 *
 * Even if the element associated with "f" has moved to a different physical
 * slot, vsmap_data() will still return the same element.
 *
 * IMPORTANT:
 *     count    = number of active elements.
 *     capacity = maximum number of elements.
 *     next     = next handle to be assigned.
 *
 * The physical slot is found through:
 *     lookup[handle]
 */

typedef struct
{
    // value  = pointer to the stored object.
    // handle = handle associated with this element.
    //
    // The handle is also stored in the element so we can verify that
    // lookup[handle] still points to the correct element.
    struct SMItem
    {
        void *value;
        uint16_t handle;
    } *pool;

    uint16_t *lookup;  // lookup[handle] returns the physical slot of the element.
    uint16_t capacity; // Maximum number of elements the pool can contain.
    uint16_t count;    // Number of currently active elements.
    uint16_t next;     // Next handle to be assigned.
} vsmap_t;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

// Creates an vsmap_t using dynamically allocated memory.
//     ALLOC    = memory allocation function.
//     CAPACITY = maximum number of elements.
//
// Example:
//     vsmap_t pool = vsmap_ALLOC(MEM_alloc, 100);
//     ...
//     MEM_free(pool.pool);
//     MEM_free(pool.lookup);
#define vsmap_ALLOC(ALLOC, CAPACITY)                          \
    {                                                        \
        .pool = (ALLOC)((CAPACITY) * sizeof(struct SMItem)), \
        .lookup = (ALLOC)((CAPACITY) * sizeof(uint16_t)),    \
        .capacity = (CAPACITY),                              \
        .count = 0,                                          \
        .next = 0,                                           \
    }

//  Statically allocates the storage required by an vsmap_t.
//      NAME     = storage name.
//      CAPACITY = maximum number of elements.
//
//  Example:
//      vsmap_DECLARE(storage, 100);
//      vsmap_t pool = vsmap_BIND(storage);
#define vsmap_DECLARE(NAME, CAPACITY)       \
    struct SMItem NAME##_pool[(CAPACITY)]; \
    uint16_t NAME##_lookup[(CAPACITY)]

//  Binds an vsmap_t to storage declared with vsmap_DECLARE().
//
//  Example:
//      vsmap_DECLARE(storage, 100);
//      vsmap_t pool = vsmap_BIND(storage);
#define vsmap_BIND(NAME)                                             \
    {                                                               \
        .pool = (NAME##_pool),                                      \
        .lookup = (NAME##_lookup),                                  \
        .capacity = sizeof(NAME##_pool) / sizeof((NAME##_pool)[0]), \
        .count = 0,                                                 \
        .next = 0,                                                  \
    }

int16_t vsmap_add(vsmap_t *, void *);
void *vsmap_data(vsmap_t *, uint16_t);
int16_t vsmap_remove(vsmap_t *, uint16_t);
void vsmap_reset(vsmap_t *);

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef VSMAP_IMPLEMENTATION

int16_t vsmap_add(vsmap_t *p, void *value)
{
    if (p->count >= p->capacity)
        return -1;

    uint16_t handle = p->next++;
    uint16_t slot = p->count++;

    p->pool[slot].value = value;
    p->pool[slot].handle = handle;
    p->lookup[handle] = slot;

    return handle;
}

void *vsmap_data(vsmap_t *p, uint16_t handle)
{
    if (handle >= p->next)
        return 0;

    uint16_t slot = p->lookup[handle];

    if (slot >= p->count || p->pool[slot].handle != handle)
        return 0;

    return p->pool[slot].value;
}

int16_t vsmap_remove(vsmap_t *p, uint16_t handle)
{
    if (handle >= p->next)
        return -1;

    uint16_t slot = p->lookup[handle];

    if (slot >= p->count)
        return -2;

    if (p->pool[slot].handle != handle)
        return -3;

    if (slot != --p->count)
    {
        p->pool[slot] = p->pool[p->count];
        p->lookup[p->pool[slot].handle] = slot;
    }

    return p->count;
}

void vsmap_reset(vsmap_t *p)
{
    p->count = 0;
    p->next = 0;
}

#endif // VSMAP_IMPLEMENTATION
