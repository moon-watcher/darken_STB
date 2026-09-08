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
    uint16_t next_handle;
    void *(*copy)();
} dpool;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

// Dynamic allocation:
//     dpool pool = DPOOL_POOL_ALLOC(MEM_alloc, 100, sizeof(Entity), memcpy);
//     int16_t handle = dpool_alloc(&pool);
//     Entity *entity = dpool_data(&pool, handle);
//     dpool_remove(&pool, handle);
//     MEM_free(pool.pool);
//     MEM_free(pool.lookup);
//     MEM_free(pool.handles);
#define DPOOL_POOL_ALLOC(ALLOC, CAPACITY, SIZE, COPY)      \
    {                                                      \
        .pool = (ALLOC)((CAPACITY) * (SIZE)),              \
        .lookup = (ALLOC)((CAPACITY) * sizeof(uint16_t)),  \
        .handles = (ALLOC)((CAPACITY) * sizeof(uint16_t)), \
        .capacity = (CAPACITY),                            \
        .size = (SIZE),                                    \
        .count = 0,                                        \
        .next_handle = 0,                                  \
        .copy = (COPY),                                    \
    }

// Static allocation:
//     Entity storage[4];
//     DPOOL_POOL_BIND(storage, memcpy);
//     dpool pool;
//     DPOOL_POOL_INIT(pool, storage, memcpy);
#define DPOOL_POOL_BIND(NAME, COPY)                           \
    uint16_t NAME##_lookup[sizeof(NAME) / sizeof((NAME)[0])]; \
    uint16_t NAME##_handles[sizeof(NAME) / sizeof((NAME)[0])]

#define DPOOL_POOL_INIT(POOL, NAME, COPY)             \
    (POOL) = (dpool)                                  \
    {                                                 \
        .pool = (char *)(NAME),                       \
        .lookup = (NAME##_lookup),                    \
        .handles = (NAME##_handles),                  \
        .capacity = sizeof(NAME) / sizeof((NAME)[0]), \
        .size = sizeof((NAME)[0]),                    \
        .count = 0,                                   \
        .next_handle = 0,                             \
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

    uint16_t handle = p->next_handle++;
    uint16_t slot = p->count++;

    p->handles[slot] = handle;
    p->lookup[handle] = slot;

    return handle;
}

void *dpool_data(dpool *p, uint16_t handle)
{
    if (handle >= p->next_handle)
        return NULL;

    uint16_t slot = p->lookup[handle];

    if (slot >= p->count)
        return NULL;

    if (p->handles[slot] != handle)
        return NULL;

    return p->pool + (slot * p->size);
}

int16_t dpool_remove(dpool *p, uint16_t handle)
{
    if (handle >= p->next_handle)
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

    return p->count;
}

void dpool_clear(dpool *p)
{
    p->count = 0;
    p->next_handle = 0;
}

#endif // DPOOL_IMPLEMENTATION
