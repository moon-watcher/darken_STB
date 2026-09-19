/**
 * darken.h — Darken (DARKula ENgine) Entity System
 *
 * darken-1.2.0_dev
 */

#pragma once

#ifndef DARKEN_MAX_ZONES
#define DARKEN_MAX_ZONES 8
#endif

_Static_assert(DARKEN_MAX_ZONES > 0 && DARKEN_MAX_ZONES <= 255, "darken.h: DARKEN_MAX_ZONES fuera de rango");

#ifndef DARKEN_DEFAULT_ZONE
#define DARKEN_DEFAULT_ZONE 0
#endif

_Static_assert(DARKEN_DEFAULT_ZONE >= 0 && DARKEN_DEFAULT_ZONE < DARKEN_MAX_ZONES, "darken.h: DARKEN_DEFAULT_ZONE fuera de rango");

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
    uint16_t bounds[DARKEN_MAX_ZONES]; // bounds[zones-1] == "size": frontera entre zonas de usuario y libre
} darken_t;

struct darken_entity_t
{
    uint16_t slot;
    uint16_t usr;
    darken_state_t update;
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
    if (state == DARKEN_DELETE)                                    \
    {                                                              \
        _darken_move_free(_entity);                                \
        continue;                                                  \
    }                                                              \
                                                                   \
    _entity->update = state;                                       \
    continue;
#endif

#define _DARKEN_ALIGN(X, A) (((X) + (uintptr_t)(A) - 1) & ~((uintptr_t)(A) - 1))
#define _DARKEN_POOL_ALIGN __alignof__(darken_entity_t)
#define _DARKEN_ENTITY_ALIGN __alignof__(struct darken_entity_t)
#define _DARKEN_ENTITY_STRIDE(PAYLOAD) _DARKEN_ALIGN(sizeof(struct darken_entity_t) + (PAYLOAD), _DARKEN_ENTITY_ALIGN)

#define DARKEN_CONTINUE ((darken_state_t)1)
#define DARKEN_DELETE ((darken_state_t)0)

#define DARKEN_ALLOC(ALLOC, CAPACITY, ZONES, PAYLOAD)                    \
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

#define DARKEN_DECLARE(NAME, CAPACITY, ZONES, PAYLOAD)                                                            \
    _Static_assert((ZONES) > 0 && (ZONES) <= DARKEN_MAX_ZONES, "darken.h: ZONES fuera de rango");                 \
    _Static_assert((CAPACITY) <= (uint16_t)-1, "darken.h: CAPACITY debe caber en uint16_t");                      \
    _Static_assert(_DARKEN_ENTITY_STRIDE(PAYLOAD) <= (uint16_t)-1, "darken.h: el stride debe caber en uint16_t"); \
    struct                                                                                                        \
    {                                                                                                             \
        uint16_t capacity;                                                                                        \
        uint16_t stride;                                                                                          \
        uint16_t zones;                                                                                           \
        darken_entity_t pool[(CAPACITY) ? (CAPACITY) : -1] __attribute__((aligned(_DARKEN_POOL_ALIGN)));          \
        uint8_t data[(CAPACITY) * _DARKEN_ENTITY_STRIDE(PAYLOAD)] __attribute__((aligned(_DARKEN_ENTITY_ALIGN))); \
    } NAME = {                                                                                                    \
        .capacity = (CAPACITY),                                                                                   \
        .stride = _DARKEN_ENTITY_STRIDE(PAYLOAD),                                                                 \
        .zones = (ZONES),                                                                                         \
    }

#define DARKEN_INIT(STORAGE, ZONES)                                                            \
    (darken_t)                                                                                 \
    {                                                                                          \
        .pool = (STORAGE).pool,                                                                \
        .storage = (STORAGE).data,                                                             \
        .capacity = sizeof((STORAGE).pool) / sizeof(darken_entity_t),                          \
        .stride = sizeof((STORAGE).data) / (sizeof((STORAGE).pool) / sizeof(darken_entity_t)), \
        .zones = (ZONES),                                                                      \
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

// Unica fuente de verdad para "size": nunca se guarda por separado, siempre se
// deriva de bounds[zones-1] (la frontera entre zonas de usuario y libre), asi
// que no puede desincronizarse de la realidad de bounds[].
static inline uint16_t _darken_size(darken_t *ctx)
{
    return ctx->bounds[ctx->zones - 1];
}

#define DARKEN_ENTITY_IN_ACTIVE(ENTITY) ((ENTITY)->slot < _darken_size((ENTITY)->owner))
#define DARKEN_ENTITY_IN_FREE(ENTITY) ((ENTITY)->slot >= _darken_size((ENTITY)->owner))

static inline uint16_t _darken_zone_lo(darken_t *ctx, int zone)
{
    return zone > 0 ? ctx->bounds[zone - 1] : 0;
}

static inline uint16_t _darken_zone_hi(darken_t *ctx, int zone)
{
    return zone < ctx->zones ? ctx->bounds[zone] : ctx->capacity;
}

static inline int darken_entity_zone(darken_entity_t entity)
{
    darken_t *ctx = entity->owner;
    int z = 0;

    if (DARKEN_ENTITY_IN_FREE(entity))
        return (int)ctx->zones;

    while (z < ctx->zones && entity->slot >= ctx->bounds[z])
        z++;

    return z;
}

static inline void _darken_move_zone(darken_entity_t entity, int target)
{
    darken_t *ctx = entity->owner;
    int cur;

    if (DARKEN_ENTITY_IN_FREE(entity))
        return;

    cur = darken_entity_zone(entity);

    if (cur == target)
        return;

    while (cur < target)
        darken_swap(ctx->pool, entity->slot, --ctx->bounds[cur++]);

    while (cur > target)
        darken_swap(ctx->pool, entity->slot, ctx->bounds[--cur]++);
}

static inline void _darken_move_from_free(darken_entity_t entity, int target)
{
    darken_t *ctx = entity->owner;
    int cur = ctx->zones;

    if (!DARKEN_ENTITY_IN_FREE(entity))
        return;

    while (cur > target)
        darken_swap(ctx->pool, entity->slot, ctx->bounds[--cur]++);
}

static inline void _darken_move_free(darken_entity_t entity)
{
    darken_t *ctx = entity->owner;
    int cur;

    if (DARKEN_ENTITY_IN_FREE(entity))
        return;

    cur = darken_entity_zone(entity);

    while (cur < ctx->zones)
        darken_swap(ctx->pool, entity->slot, --ctx->bounds[cur++]);
}

static inline void darken_entity_set_zone(darken_entity_t entity, int zone)
{
    _darken_move_zone(entity, zone);
}

static inline uint16_t darken_count_zone(darken_t *ctx, int zone)
{
    return (uint16_t)(_darken_zone_hi(ctx, zone) - _darken_zone_lo(ctx, zone));
}

#define DARKEN_SPAWN_ZONE(CTX, ZONE)                                        \
    ({                                                                      \
        darken_t *_ctx = (CTX);                                             \
        int _zone = (ZONE);                                                 \
        uint16_t _s = _darken_size(_ctx);                                   \
        darken_entity_t _entity = _s < _ctx->capacity ? _ctx->pool[_s] : 0; \
                                                                            \
        if (_entity)                                                        \
            _darken_move_from_free(_entity, _zone);                         \
                                                                            \
        _entity;                                                            \
    })

#define DARKEN_SPAWN(CTX) \
    DARKEN_SPAWN_ZONE((CTX), DARKEN_DEFAULT_ZONE)

#define DARKEN_FOREACH(CTX, CODE) \
    DARKEN_FOREACH_ZONE((CTX), DARKEN_DEFAULT_ZONE, CODE)

#define DARKEN_FOREACH_ZONE(CTX, ZONE, CODE)             \
    do                                                   \
    {                                                    \
        darken_t *_ctx = (CTX);                          \
        int _zone = (ZONE);                              \
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

#define DARKEN_DATA(TYPE, VAR, ENTITY) TYPE *VAR = (TYPE *)(ENTITY)->data;

#define DARKEN_ENTITY(DATA) ((darken_entity_t)((uint8_t *)(DATA) - (uintptr_t)&((darken_entity_t)0)->data))

#define DARKEN_ENTITY_IN_ZONE(ENTITY, ZONE) \
    (DARKEN_ENTITY_IN_ACTIVE(ENTITY) && (ENTITY)->slot >= _darken_zone_lo((ENTITY)->owner, (ZONE)) && (ENTITY)->slot < _darken_zone_hi((ENTITY)->owner, (ZONE)))

#define DARKEN_COUNT_ACTIVE(CTX) (_darken_size(CTX))
#define DARKEN_COUNT_FREE(CTX) ((uint16_t)((CTX)->capacity - _darken_size(CTX)))
#define DARKEN_COUNT_ZONE(CTX, ZONE) darken_count_zone((CTX), (ZONE))

static inline void darken_entity_delete(darken_entity_t entity)
{
    if (DARKEN_ENTITY_IN_FREE(entity))
        return;

    _darken_move_free(entity);
}

static inline void darken_init(darken_t *ctx)
{
    for (int z = 0; z < ctx->zones; z++)
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

static inline void darken_update_zone(darken_t *ctx, int zone)
{
    DARKEN_FOREACH_ZONE(ctx, zone, _DARKEN_UPDATE);
}

static inline void darken_reset(darken_t *ctx)
{
    for (int z = 0; z < ctx->zones; z++)
        ctx->bounds[z] = 0;
}