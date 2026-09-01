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

/* ============================================================================
 * MACROS DE CONVENIENCIA: operar sobre la entidad desde su *data
 * ============================================================================ */

/* Pausar / resumir / borrar la entidad propietaria de este data */
#define bbb_entity_pause_data(DATA)   bbb_entity_pause(BBB_DATA_GET_ENTITY(DATA))
#define bbb_entity_resume_data(DATA)  bbb_entity_resume(BBB_DATA_GET_ENTITY(DATA))
#define bbb_entity_delete_data(DATA)  bbb_entity_delete(BBB_DATA_GET_ENTITY(DATA))

/* Kill rapido desde data (sin comprobaciones, sin llamar a destroy) */
#define bbb_entity_kill_fast_data(DATA) bbb_entity_kill_fast(BBB_DATA_GET_ENTITY(DATA))

/* Cambiar el callback de update de la entidad propietaria */
#define bbb_entity_set_update(DATA, FN) (BBB_DATA_GET_ENTITY(DATA)->update = (FN))

/* Cambiar el callback de destroy de la entidad propietaria */
#define bbb_entity_set_destroy(DATA, FN) (BBB_DATA_GET_ENTITY(DATA)->destroy = (FN))

/* Leer / escribir tag y usr desde data */
#define bbb_entity_tag(DATA)          (BBB_DATA_GET_ENTITY(DATA)->tag)
#define bbb_entity_set_tag(DATA, V)   (BBB_DATA_GET_ENTITY(DATA)->tag = (V))
#define bbb_entity_usr(DATA)          (BBB_DATA_GET_ENTITY(DATA)->usr)
#define bbb_entity_set_usr(DATA, V)   (BBB_DATA_GET_ENTITY(DATA)->usr = (V))
#define bbb_entity_slot(DATA)          (BBB_DATA_GET_ENTITY(DATA)->slot)
#define bbb_entity_set_slot(DATA, V)   (BBB_DATA_GET_ENTITY(DATA)->slot = (V))

/* Alias del propio entity (para pasarlo a otras funciones) */
#define bbb_this(DATA)                (BBB_DATA_GET_ENTITY(DATA))

/* Cambiar update y destroy de golte (transicion de estado completa) */
#define bbb_entity_set_state(DATA, UP, DEST) \
    do { \
        bbb_entity _e = BBB_DATA_GET_ENTITY(DATA); \
        _e->update = (UP); \
        _e->destroy = (DEST); \
    } while (0)

/* Saber si la entidad esta activa o pausada (util para asserts en MD) */
#define bbb_entity_is_active(DATA) \
    (BBB_DATA_GET_ENTITY(DATA)->slot < BBB_DATA_GET_ENTITY(DATA)->owner->size)
#define bbb_entity_is_paused(DATA) \
    (BBB_DATA_GET_ENTITY(DATA)->slot >= BBB_DATA_GET_ENTITY(DATA)->owner->paused)



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

#define bbb_entity_kill_fast(ENTITY)                         \
    do                                                       \
    {                                                        \
        bbb_entity _kfe = (ENTITY);                          \
        bbb *_kfc = _kfe->owner;                             \
        _bbb_swap(_kfc->pool, _kfe->slot, --_kfc->size);   \
    } while (0)

#endif // BBB_IMPLEMENTATION
