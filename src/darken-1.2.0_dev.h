

#pragma once

#ifndef DARKEN_MAX_ZONES
#define DARKEN_MAX_ZONES 8
#endif

#ifdef DARKEN_DIRECT
typedef void (*darken_state_t)();
#else
typedef void *(*darken_state_t)();
#endif

typedef struct darken_entity_t *darken_entity_t;

typedef struct darken_t
{
    darken_entity_t *pool;
    uint8_t *storage;
    uint16_t capacity;
    uint16_t stride;
    uint16_t zones;
    uint16_t bounds[DARKEN_MAX_ZONES];
} darken_t;

struct darken_entity_t
{
    uint16_t slot;
    uint16_t usr;
    darken_state_t update;
    darken_state_t destroy;
    uint32_t tag;
    darken_t *owner;
    uint8_t data[];
};

#ifdef DARKEN_DIRECT
#define _DARKEN_ARGS(ENTITY) (ENTITY), (ENTITY)->data
#define _DARKEN_UPDATE _entity->update(_DARKEN_ARGS(_entity))
#else
#define _DARKEN_ARGS(ENTITY) (ENTITY)->data

#define _DARKEN_UPDATE                                             \
    darken_state_t state = _entity->update(_DARKEN_ARGS(_entity)); \
                                                                   \
    if (state == DARKEN_CONTINUE)                                  \
        continue;                                                  \
                                                                   \
    if (state > DARKEN_CONTINUE)                                   \
        _entity->update = state;                                   \
                                                                   \
    else /* DARKEN_DELETE) */                                      \
    {                                                              \
        if (_entity->destroy)                                      \
            _entity->destroy(_DARKEN_ARGS(_entity));               \
                                                                   \
        _darken_move_free(_entity);                                \
    }
#endif

#define _DARKEN_ALIGN(X, A) (((X) + (uintptr_t)(A) - 1) & ~((uintptr_t)(A) - 1))
#define _DARKEN_POOL_ALIGN __alignof__(darken_entity_t)
#define _DARKEN_ENTITY_ALIGN __alignof__(struct darken_entity_t)
#define _DARKEN_ENTITY_STRIDE(PAYLOAD) _DARKEN_ALIGN(sizeof(struct darken_entity_t) + (PAYLOAD), _DARKEN_ENTITY_ALIGN)

static inline void darken_swap(darken_entity_t pool[], uint16_t i, uint16_t j)
{
    if (i == j)
        return;

    darken_entity_t tmp = pool[i];
    pool[i] = pool[j];
    pool[j] = tmp;
    pool[i]->slot = i;
    pool[j]->slot = j;
}

static inline uint16_t _darken_size(darken_t *ctx)
{
    return ctx->bounds[ctx->zones - 1];
}

#define DARKEN_ENTITY_IN_ACTIVE(ENTITY) ((ENTITY)->slot < _darken_size((ENTITY)->owner))
#define DARKEN_ENTITY_IN_FREE(ENTITY) ((ENTITY)->slot >= _darken_size((ENTITY)->owner))

static inline uint16_t _darken_zone_lo(darken_t *ctx, uint16_t zone)
{
    return zone > 0 ? ctx->bounds[zone - 1] : 0;
}

static inline uint16_t _darken_zone_hi(darken_t *ctx, uint16_t zone)
{
    return zone < ctx->zones ? ctx->bounds[zone] : ctx->capacity;
}

static inline uint16_t darken_entity_zone(darken_entity_t entity)
{
    darken_t *ctx = entity->owner;
    uint16_t z = 0;

    if (DARKEN_ENTITY_IN_FREE(entity))
        return (int)ctx->zones;

    while (z < ctx->zones && entity->slot >= ctx->bounds[z])
        z++;

    return z;
}

static inline void _darken_move_zone(darken_entity_t entity, uint16_t target)
{
    if (DARKEN_ENTITY_IN_FREE(entity))
        return;

    uint16_t cur = darken_entity_zone(entity);

    if (cur == target)
        return;

    darken_t *ctx = entity->owner;

    while (cur < target)
        darken_swap(ctx->pool, entity->slot, --ctx->bounds[cur++]);

    while (cur > target)
        darken_swap(ctx->pool, entity->slot, ctx->bounds[--cur]++);
}

static inline void _darken_move_from_free(darken_entity_t entity, uint16_t target)
{
    darken_t *ctx = entity->owner;
    uint16_t cur = ctx->zones;

    if (!DARKEN_ENTITY_IN_FREE(entity))
        return;

    while (cur > target)
        darken_swap(ctx->pool, entity->slot, ctx->bounds[--cur]++);
}

static inline void _darken_move_free(darken_entity_t entity)
{
    if (DARKEN_ENTITY_IN_FREE(entity))
        return;

    uint16_t cur = darken_entity_zone(entity);
    darken_t *ctx = entity->owner;

    while (cur < ctx->zones)
        darken_swap(ctx->pool, entity->slot, --ctx->bounds[cur++]);
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

#define DARKEN_CONTINUE ((darken_state_t)1)
#define DARKEN_DELETE ((darken_state_t)0)

#define DARKEN_ALLOC(ALLOC, CAPACITY, PAYLOAD) DARKEN_ALLOC_ZONES((ALLOC), (CAPACITY), 1, (PAYLOAD))

#define DARKEN_ALLOC_ZONES(ALLOC, CAPACITY, ZONES, PAYLOAD)              \
    (darken_t)                                                           \
    {                                                                    \
        .pool = (ALLOC)((CAPACITY) * sizeof(darken_entity_t)),           \
        .storage = (ALLOC)((CAPACITY) * _DARKEN_ENTITY_STRIDE(PAYLOAD)), \
        .capacity = (CAPACITY),                                          \
        .stride = _DARKEN_ENTITY_STRIDE(PAYLOAD),                        \
        .zones = (ZONES),                                                \
    }

#define DARKEN_FREE(FREE, CTX)  \
    do                          \
    {                           \
        (FREE)((CTX)->pool);    \
        (FREE)((CTX)->storage); \
    } while (0)

#define DARKEN_DECLARE(NAME, CAPACITY, PAYLOAD) DARKEN_DECLARE_ZONES(NAME, CAPACITY, 1, PAYLOAD)

#define DARKEN_DECLARE_ZONES(NAME, CAPACITY, ZONES, PAYLOAD)                                                                                                               \
    struct                                                                                                                                                                 \
    {                                                                                                                                                                      \
        uint16_t capacity;                                                                                                                                                 \
        uint16_t stride;                                                                                                                                                   \
        uint16_t zones;                                                                                                                                                    \
        darken_entity_t pool[(CAPACITY) ? (CAPACITY) : -1] __attribute__((aligned(_DARKEN_POOL_ALIGN)));                                                                   \
        uint8_t data[(CAPACITY) * _DARKEN_ENTITY_STRIDE(PAYLOAD)] __attribute__((aligned(_DARKEN_ENTITY_ALIGN)));                                                          \
        uint8_t _darken_zones_marker[(ZONES) > 0 ? (ZONES) : 1];                                                                                                           \
        uint8_t _darken_checks[((ZONES) > 0 && (ZONES) <= DARKEN_MAX_ZONES) && ((CAPACITY) <= (uint16_t)-1) && (_DARKEN_ENTITY_STRIDE(PAYLOAD) <= (uint16_t)-1) ? 1 : -1]; \
    } NAME = {                                                                                                                                                             \
        .capacity = (CAPACITY),                                                                                                                                            \
        .stride = _DARKEN_ENTITY_STRIDE(PAYLOAD),                                                                                                                          \
        .zones = (ZONES),                                                                                                                                                  \
    }

#define DARKEN_INIT(STORAGE)                                                                   \
    (darken_t)                                                                                 \
    {                                                                                          \
        .pool = (STORAGE).pool,                                                                \
        .storage = (STORAGE).data,                                                             \
        .capacity = sizeof((STORAGE).pool) / sizeof(darken_entity_t),                          \
        .stride = sizeof((STORAGE).data) / (sizeof((STORAGE).pool) / sizeof(darken_entity_t)), \
        .zones = sizeof((STORAGE)._darken_zones_marker),                                       \
    }

#define DARKEN_BIND(NAME)            \
    (darken_t)                       \
    {                                \
        .pool = (NAME).pool,         \
        .storage = (NAME).data,      \
        .capacity = (NAME).capacity, \
        .stride = (NAME).stride,     \
        .zones = (NAME).zones,       \
    }

#define DARKEN_SPAWN(CTX) DARKEN_SPAWN_ZONE((CTX), 0)

#define DARKEN_SPAWN_ZONE(CTX, ZONE) ({                               \
    darken_t *_ctx = (CTX);                                           \
    uint16_t_zone = (ZONE);                                           \
    uint16_t _s = _darken_size(_ctx);                                 \
    darken_entity_t _entity _s < _ctx->capacity ? _ctx->pool[_s] : 0; \
                                                                      \
    if (_entity)                                                      \
        _darken_move_from_free(_entity, _zone);                       \
                                                                      \
    _entity;                                                          \
})

#define DARKEN_FOREACH(CTX, CODE)                        \
    do                                                   \
    {                                                    \
        darken_t *_ctx = (CTX);                          \
        uint16_t _index = _darken_size(_ctx);            \
        if (_index)                                      \
        {                                                \
            darken_entity_t *_pool = _ctx->pool;         \
            while (_index--)                             \
            {                                            \
                darken_entity_t _entity = _pool[_index]; \
                CODE;                                    \
            }                                            \
        }                                                \
    } while (0)

#define DARKEN_FOREACH_ZONE(CTX, ZONE, CODE)             \
    do                                                   \
    {                                                    \
        darken_t *_ctx = (CTX);                          \
        uint16_t _zone = (ZONE);                         \
        uint16_t _lo = _darken_zone_lo(_ctx, _zone);     \
        uint16_t _hi = _darken_zone_hi(_ctx, _zone);     \
        uint16_t _index = _hi;                           \
                                                         \
        if (_index > _lo)                                \
        {                                                \
            darken_entity_t *_pool = _ctx->pool;         \
            while (_index-- > _lo)                       \
            {                                            \
                darken_entity_t _entity = _pool[_index]; \
                CODE;                                    \
            }                                            \
        }                                                \
    } while (0)

/* --- DATA / ENTITY --------------------------------------------------------- */

#define DARKEN_DATA(TYPE, VAR, ENTITY) TYPE *VAR = (TYPE *)(ENTITY)->data;
#define DARKEN_ENTITY(DATA) ((darken_entity_t)((uint8_t *)(DATA) - (uintptr_t)&((darken_entity_t)0)->data))

/* --- ZONE QUERIES ---------------------------------------------------------- */

#define DARKEN_ENTITY_IN_ZONE(ENTITY, ZONE)                        \
    (DARKEN_ENTITY_IN_ACTIVE(ENTITY) &&                            \
     (ENTITY)->slot >= _darken_zone_lo((ENTITY)->owner, (ZONE)) && \
     (ENTITY)->slot < _darken_zone_hi((ENTITY)->owner, (ZONE)))

#define DARKEN_COUNT_ACTIVE(CTX) (_darken_size(CTX))
#define DARKEN_COUNT_FREE(CTX) ((uint16_t)((CTX)->capacity - _darken_size(CTX)))
#define DARKEN_COUNT_ZONE(CTX, ZONE) darken_count_zone((CTX), (ZONE))

static inline uint16_t darken_count_zone(darken_t *ctx, uint16_t zone)
{
    return (uint16_t)(_darken_zone_hi(ctx, zone) - _darken_zone_lo(ctx, zone));
}

/* --- LIFECYCLE ------------------------------------------------------------- */

static inline void darken_entity_delete(darken_entity_t entity)
{
    if (DARKEN_ENTITY_IN_FREE(entity))
        return;

    if (entity->destroy)
        entity->destroy(_DARKEN_ARGS(entity));

    _darken_move_free(entity);
}

static inline void darken_entity_set_zone(darken_entity_t entity, uint16_t zone)
{
    _darken_move_zone(entity, zone);
}

static inline void darken_init(darken_t *ctx)
{
    for (uint16_t z = 0; z < ctx->zones; z++)
        ctx->bounds[z] = 0;

    uint16_t i = ctx->capacity;
    uint8_t *storage = ctx->storage;

    while (i--)
    {
        ctx->pool[i] = (darken_entity_t)storage;
        ctx->pool[i]->owner = ctx;
        ctx->pool[i]->slot = i;

        storage += ctx->stride;
    }
}

static inline void darken_update(darken_t *ctx)
{
    DARKEN_FOREACH(ctx, _DARKEN_UPDATE);
}

static inline void darken_update_zone(darken_t *ctx, uint16_t zone)
{
    DARKEN_FOREACH_ZONE(ctx, zone, _DARKEN_UPDATE);
}

static inline void darken_reset(darken_t *ctx)
{
    DARKEN_FOREACH(ctx, {
        if (_entity->destroy)
            _entity->destroy(_DARKEN_ARGS(_entity));
    });

    for (uint16_t z = 0; z < ctx->zones; z++)
        ctx->bounds[z] = 0;
}

static inline void darken_reset_zone(darken_t *ctx, uint16_t zone)
{
    uint16_t lo = _darken_zone_lo(ctx, zone);

    while (_darken_zone_hi(ctx, zone) > lo)
    {
        darken_entity_t _entity = ctx->pool[_darken_zone_hi(ctx, zone) - 1];

        if (_entity->destroy)
            _entity->destroy(_DARKEN_ARGS(_entity));

        _darken_move_free(_entity);
    }
}
