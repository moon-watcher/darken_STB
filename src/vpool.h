#pragma once

#include <stdint.h>

typedef struct
{
    void *value;
    uint16_t handle;
} VpoolItem;

typedef struct
{
    VpoolItem *pool;
    uint16_t *lookup;
    uint16_t capacity;
    uint16_t count;
    uint16_t next;
} Vpool;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

#define VPOOL_ALLOC(ALLOC, CAPACITY)                      \
    {                                                     \
        .pool = (ALLOC)((CAPACITY) * sizeof(VpoolItem)),  \
        .lookup = (ALLOC)((CAPACITY) * sizeof(uint16_t)), \
        .capacity = (CAPACITY),                           \
        .count = 0,                                       \
        .next = 0,                                        \
    }

#define VPOOL_DECLARE(NAME, CAPACITY)  \
    VpoolItem NAME##_pool[(CAPACITY)]; \
    uint16_t NAME##_lookup[(CAPACITY)]

#define VPOOL_BIND(NAME)                                            \
    {                                                               \
        .pool = (NAME##_pool),                                      \
        .lookup = (NAME##_lookup),                                  \
        .capacity = sizeof(NAME##_pool) / sizeof((NAME##_pool)[0]), \
        .count = 0,                                                 \
        .next = 0,                                                  \
    }

int16_t vpool_add(Vpool *, void *);
void *vpool_data(Vpool *, uint16_t);
int16_t vpool_remove(Vpool *, uint16_t);
void vpool_clear(Vpool *);

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef VPOOL_IMPLEMENTATION

int16_t vpool_add(Vpool *p, void *value)
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

void *vpool_data(Vpool *p, uint16_t handle)
{
    if (handle >= p->next)
        return 0;

    return p->pool[p->lookup[handle]].value;
}

int16_t vpool_remove(Vpool *p, uint16_t handle)
{
    if (handle >= p->next)
        return -1;

    uint16_t slot = p->lookup[handle];

    if (slot != --p->count)
    {
        p->pool[slot] = p->pool[p->count];
        p->lookup[p->pool[slot].handle] = slot;
    }

    return p->count;
}

void vpool_clear(Vpool *p)
{
    p->count = 0;
    p->next = 0;
}

#endif // VPOOL_IMPLEMENTATION
