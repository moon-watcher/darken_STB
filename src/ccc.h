/**
 * ccc2.h — Option 2: Entity-Owned State
 * Same struct/pool layout as ccc2.h, but ccc_state receives the whole
 * entity and there are no sentinel return values. Lifecycle is explicit API calls.
 */

#pragma once

#include <stdint.h>

typedef void (*ccc_state)();

typedef struct ccc_entity *ccc_entity;

typedef struct ccc
{
    ccc_entity *pool;
    uint8_t *storage;
    uint16_t capacity;
    uint16_t size;
    uint16_t paused;
    uint16_t stride;
} ccc;

struct ccc_entity
{
    uint16_t slot;
    ccc *owner;

    ccc_state update;
    ccc_state destroy;

    uint32_t tag;
    uint16_t usr;

    uint8_t data[];
};

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

void ccc_entity_pause(ccc_entity);
void ccc_entity_resume(ccc_entity);
void ccc_entity_delete(ccc_entity);

void ccc_init(ccc *);
void ccc_update(ccc *);
void ccc_reset(ccc *);

#define CCC_DATA(TYPE, VAR, ENTITY) TYPE *VAR = (TYPE *)(ENTITY)->data;

// Free .pool & .storage
#define CCC_POOL_ALLOC(ALLOC, CAPACITY, PAYLOAD)                                 \
    {                                                                            \
        .pool = (ccc_entity *)(ALLOC)((CAPACITY) * sizeof(ccc_entity)),          \
        .storage = (uint8_t *)(ALLOC)((CAPACITY) * _CCC_ENTITY_STRIDE(PAYLOAD)), \
        .capacity = (CAPACITY),                                                  \
        .stride = _CCC_ENTITY_STRIDE(PAYLOAD),                                   \
    }

#define CCC_POOL_DECLARE(NAME, CAPACITY, PAYLOAD)                                           \
    struct                                                                                  \
    {                                                                                       \
        uint16_t capacity;                                                                  \
        uint16_t stride;                                                                    \
        ccc_entity pool[(CAPACITY)];                                                        \
        uint8_t data[(CAPACITY) * _CCC_ENTITY_STRIDE(PAYLOAD)] __attribute__((aligned(4))); \
    } NAME = {                                                                              \
        .capacity = (CAPACITY),                                                             \
        .stride = _CCC_ENTITY_STRIDE(PAYLOAD),                                              \
    }

// Static/global initialization: compile-time constants
#define CCC_POOL_INIT(STORAGE)                                                            \
    {                                                                                     \
        .pool = (STORAGE).pool,                                                           \
        .storage = (STORAGE).data,                                                        \
        .capacity = sizeof((STORAGE).pool) / sizeof(ccc_entity),                          \
        .stride = sizeof((STORAGE).data) / (sizeof((STORAGE).pool) / sizeof(ccc_entity)), \
    }

// Runtime: locals, reassignment, any context
#define CCC_POOL_BIND(NAME)          \
    {                                \
        .pool = (NAME).pool,         \
        .storage = (NAME).data,      \
        .capacity = (NAME).capacity, \
        .stride = (NAME).stride,     \
    }

#define CCC_FOREACH(CTX, CODE)                      \
    do                                              \
    {                                               \
        uint16_t _index = (CTX)->size;              \
        if (_index)                                 \
        {                                           \
            ccc_entity *_pool = (CTX)->pool;        \
            while (_index--)                        \
            {                                       \
                ccc_entity _entity = _pool[_index]; \
                (void)_entity;                      \
                CODE;                               \
            }                                       \
        }                                           \
    } while (0)

#define CCC_SPAWN(CTX) ({                                     \
    ccc *_ctx = (CTX);                                        \
    _ctx->size < _ctx->paused ? _ctx->pool[_ctx->size++] : 0; \
})

/* ============================================================================
 * PRIVATE
 * ============================================================================ */

#define _CCC_ALIGN4(X) (((X) + 3U) & ~3U)
#define _CCC_ENTITY_STRIDE(PAYLOAD) _CCC_ALIGN4(sizeof(struct ccc_entity) + (PAYLOAD))

static inline uint16_t _ccc_swap(ccc_entity pool[], uint16_t i, uint16_t j)
{
    if (i == j)
        return 0;

    ccc_entity tmp = pool[i];
    pool[i] = pool[j];
    pool[j] = tmp;
    pool[i]->slot = i;
    pool[j]->slot = j;

    return 1;
}

#ifdef CCC_IMPLEMENTATION

void ccc_init(ccc *ctx)
{
    ctx->size = 0;
    uint16_t capacity = ctx->paused = ctx->capacity;
    uint8_t *storage = ctx->storage;
    uint16_t step = ctx->stride;
    ccc_entity entity, *pool = ctx->pool;

    while (capacity--)
    {
        entity = pool[capacity] = (ccc_entity)storage;
        entity->owner = ctx;
        entity->slot = capacity;

        storage += step;
    }
}

void ccc_update(ccc *ctx)
{
    CCC_FOREACH(ctx, {
        // if (_entity->update)
        _entity->update(_entity, _entity->data);
    });
}

void ccc_reset(ccc *ctx)
{
    CCC_FOREACH(ctx, {
        if (_entity->destroy)
            _entity->destroy(_entity, _entity->data);
    });

    ctx->size = 0;
    ctx->paused = ctx->capacity;
}

void ccc_entity_pause(ccc_entity entity)
{
    ccc *ctx = entity->owner;
    _ccc_swap(ctx->pool, entity->slot, --ctx->size);
    _ccc_swap(ctx->pool, entity->slot, --ctx->paused);
}

void ccc_entity_resume(ccc_entity entity)
{
    ccc *ctx = entity->owner;
    _ccc_swap(ctx->pool, entity->slot, ctx->paused);
    _ccc_swap(ctx->pool, entity->slot, ctx->size);
    ++ctx->paused;
    ++ctx->size;
}

void ccc_entity_delete(ccc_entity entity)
{
    ccc *ctx = entity->owner;

    if (entity->slot < ctx->size)
    {
        if (entity->destroy)
            entity->destroy(entity, entity->data);

        _ccc_swap(ctx->pool, entity->slot, --ctx->size);
    }
    else
        _ccc_swap(ctx->pool, entity->slot, ctx->paused++);
}

#endif // CCC_IMPLEMENTATION
