#pragma once

#include <stdint.h>

typedef struct
{
    char *pool;
    uint16_t *lookup;
    uint16_t *handles;
    uint16_t capacity;
    uint16_t size;
    uint16_t count;
    uint16_t next;
    uint16_t free_head;
    uint16_t free_count;
    void (*copy)();
} dpool;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

// Dynamic allocation:
//     dpool pool = DPOOL_ALLOC(MEM_alloc, 100, sizeof(Entity), memcpy);
//     int16_t handle = dpool_alloc(&pool);
//     Entity *entity = dpool_data(&pool, handle);
//     dpool_remove(&pool, handle);
//     MEM_free(pool.pool);
//     MEM_free(pool.lookup);
//     MEM_free(pool.handles);
#define DPOOL_ALLOC(ALLOC, CAPACITY, SIZE, COPY)      \
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

// Static allocation:
//     Entity storage[100];
//     uint16_t lookup[100];
//     uint16_t handles[100];
//     dpool pool = DPOOL_BIND(storage, lookup, handles, memcpy);
#define DPOOL_BIND(NAME, LOOKUP, HANDLES, COPY)  \
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

int16_t dpool_alloc(dpool *);
void *dpool_data(dpool *, uint16_t);
int16_t dpool_remove(dpool *, uint16_t);
void dpool_clear(dpool *);

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DPOOL_IMPLEMENTATION

int16_t dpool_alloc(dpool *p)
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
            return -1;
        }

        handle = p->next++;
    }

    p->handles[slot] = handle;
    p->lookup[handle] = slot;

    return handle;
}

void *dpool_data(dpool *p, uint16_t handle)
{
    if (handle >= p->next)
        return 0;

    uint16_t slot = p->lookup[handle];

    if (slot >= p->count)
        return 0;

    if (p->handles[slot] != handle)
        return 0;

    return p->pool + (slot * p->size);
}

int16_t dpool_remove(dpool *p, uint16_t handle)
{
    if (handle >= p->next)
        return -1;

    uint16_t slot = p->lookup[handle];

    if (slot >= p->count)
        return -1;

    if (p->handles[slot] != handle)
        return -1;

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

void dpool_clear(dpool *p)
{
    p->count = 0;
    p->next = 0;
    p->free_head = 0;
    p->free_count = 0;
}

#endif // DPOOL_IMPLEMENTATION