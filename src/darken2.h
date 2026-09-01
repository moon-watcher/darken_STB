/**
 * bbb2.h — Option 2: Entity-Owned State
 * Same struct/pool layout as bbb2.h, but bbb_state receives the whole
 * entity and there are no sentinel return values. Lifecycle is explicit API calls.
 */

#pragma once

#include <stdint.h>

typedef void (*bbb_state)();

typedef struct bbb_entity *bbb_entity;

typedef struct bbb
{
    bbb_entity *pool;
    uint8_t *storage;
    uint16_t capacity;
    uint16_t size;
    uint16_t paused;
    uint16_t stride;
} bbb;

struct bbb_entity
{
    uint16_t slot;
    bbb *owner;

    bbb_state update;
    bbb_state destroy;

    uint32_t tag;
    uint16_t usr;

    uint8_t data[];
};

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

#define BBB_DATA(TYPE, VAR, ENTITY) TYPE *VAR = (TYPE *)(ENTITY)->data;
#define BBB_DATA_GET_ENTITY(DATA) ((bbb_entity)((uint8_t *)(DATA) - (uint32_t)&((bbb_entity)0)->data))

void bbb_entity_pause(bbb_entity);
void bbb_entity_resume(bbb_entity);
void bbb_entity_delete(bbb_entity);

void bbb_init(bbb *);
void bbb_update(bbb *);
void bbb_reset(bbb *);

// Free .pool & .storage
#define BBB_POOL_ALLOC(ALLOC, CAPACITY, PAYLOAD)                                 \
    {                                                                            \
        .pool = (bbb_entity *)(ALLOC)((CAPACITY) * sizeof(bbb_entity)),          \
        .storage = (uint8_t *)(ALLOC)((CAPACITY) * _BBB_ENTITY_STRIDE(PAYLOAD)), \
        .capacity = (CAPACITY),                                                  \
        .stride = _BBB_ENTITY_STRIDE(PAYLOAD),                                   \
    }

#define BBB_POOL_DECLARE(NAME, CAPACITY, PAYLOAD)                                           \
    struct                                                                                  \
    {                                                                                       \
        uint16_t capacity;                                                                  \
        uint16_t stride;                                                                    \
        bbb_entity pool[(CAPACITY)];                                                        \
        uint8_t data[(CAPACITY) * _BBB_ENTITY_STRIDE(PAYLOAD)] __attribute__((aligned(4))); \
    } NAME = {                                                                              \
        .capacity = (CAPACITY),                                                             \
        .stride = _BBB_ENTITY_STRIDE(PAYLOAD),                                              \
    }

// Static/global initialization: compile-time constants
#define BBB_POOL_INIT(STORAGE)                                                            \
    {                                                                                     \
        .pool = (STORAGE).pool,                                                           \
        .storage = (STORAGE).data,                                                        \
        .capacity = sizeof((STORAGE).pool) / sizeof(bbb_entity),                          \
        .stride = sizeof((STORAGE).data) / (sizeof((STORAGE).pool) / sizeof(bbb_entity)), \
    }

// Runtime: locals, reassignment, any context
#define BBB_POOL_BIND(NAME)          \
    {                                \
        .pool = (NAME).pool,         \
        .storage = (NAME).data,      \
        .capacity = (NAME).capacity, \
        .stride = (NAME).stride,     \
    }

#define BBB_FOREACH(CTX, CODE)                      \
    do                                              \
    {                                               \
        uint16_t _index = (CTX)->size;              \
        if (_index)                                 \
        {                                           \
            bbb_entity *_pool = (CTX)->pool;        \
            while (_index--)                        \
            {                                       \
                bbb_entity _entity = _pool[_index]; \
                (void)_entity;                      \
                CODE;                               \
            }                                       \
        }                                           \
    } while (0)

#define BBB_SPAWN(CTX) ({                                     \
    bbb *_ctx = (CTX);                                        \
    _ctx->size < _ctx->paused ? _ctx->pool[_ctx->size++] : 0; \
})

/* ============================================================================
 * PRIVATE
 * ============================================================================ */

#define _BBB_ALIGN4(X) (((X) + 3U) & ~3U)

#define _BBB_ENTITY_STRIDE(PAYLOAD) _BBB_ALIGN4(sizeof(struct bbb_entity) + (PAYLOAD))

static inline uint16_t _bbb_swap(bbb_entity pool[], uint16_t i, uint16_t j)
{
    if (i == j)
        return 0;

    bbb_entity tmp = pool[i];
    pool[i] = pool[j];
    pool[j] = tmp;
    pool[i]->slot = i;
    pool[j]->slot = j;

    return 1;
}

#ifdef BBB_IMPLEMENTATION

void bbb_init(bbb *ctx)
{
    ctx->size = 0;
    uint16_t capacity = ctx->paused = ctx->capacity;
    uint8_t *storage = ctx->storage;
    uint16_t step = ctx->stride;
    bbb_entity entity, *pool = ctx->pool;

    while (capacity--)
    {
        entity = pool[capacity] = (bbb_entity)storage;
        entity->owner = ctx;
        entity->slot = capacity;

        storage += step;
    }
}

void bbb_update(bbb *ctx)
{
    BBB_FOREACH(ctx, {
        if (_entity->update)
            _entity->update(_entity->data);
    });
}

void bbb_reset(bbb *ctx)
{
    BBB_FOREACH(ctx, {
        if (_entity->destroy)
            _entity->destroy(_entity->data);
    });

    ctx->size = 0;
    ctx->paused = ctx->capacity;
}

void bbb_entity_pause(bbb_entity entity)
{
    bbb *ctx = entity->owner;
    _bbb_swap(ctx->pool, entity->slot, --ctx->size);
    _bbb_swap(ctx->pool, entity->slot, --ctx->paused);
}

void bbb_entity_resume(bbb_entity entity)
{
    bbb *ctx = entity->owner;
    _bbb_swap(ctx->pool, entity->slot, ctx->paused);
    _bbb_swap(ctx->pool, entity->slot, ctx->size);
    ++ctx->paused;
    ++ctx->size;
}

void bbb_entity_delete(bbb_entity entity)
{
    bbb *ctx = entity->owner;

    if (entity->slot < ctx->size)
    {
        if (entity->destroy)
            entity->destroy(entity->data);

        _bbb_swap(ctx->pool, entity->slot, --ctx->size);
    }
    else
        _bbb_swap(ctx->pool, entity->slot, ctx->paused++);
}

#endif // BBB_IMPLEMENTATION
