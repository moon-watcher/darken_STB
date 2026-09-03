/**
 * darken.h — Darken (DARKula ENgine) 2.0 Entity System
 *
 * darken-2.0.0-dev
 *
 * Full documentation: README.Darken.md
 *
 * GNU C note:
 * - This header uses GNU C __attribute__ extension and statement expressions.
 * - 4-byte align boundary because Darken targets GCC and the Motorola 68000.
 * - 16-bit members preference for optimal 68K performance.
 *
 *
 *
 * Entity: Base entity managed by the entity ctx
 *
 * The entity structure serves as a container for user data with lifecycle  management. The flexible array member
 * 'data[]' allows entitys to have variable-sized payloads while maintaining contiguous memory layout.
 *
 * The stride between entitys is pre-calculated during ctx initialization to enable O(1) access to any entity
 * by index.
 *
 * An entity's own memory address (this struct) never moves once allocated by darken_init(). What moves between
 * the ctx's zones is only the *pointer* to it inside darken.pool[]. This is what makes it safe to keep a raw
 * pointer into entity->data even while the entity gets paused/resumed/reordered.
 *
 *
 *
 * Ctx: Entity container and lifecycle ctx. Maintains the pointer array in three logical zones:
 *
 * Array Layout:
 *    [ active entitys ][   free slots    ][ paused entitys ]
 *    0                 size               paused             capacity
 *
 * The entity entitys themselves live in the caller-provided storage block; * ctx->pool contains pointers to
 * those fixed addresses.
 *
 * - Active zone [0, size):
 *     Entity pointers updated every frame by darken_update(). Iterable with DARKEN_FOREACH. Freely created
 *     (DARKEN_SPAWN) and deleted.
 *
 * - Free zone [size, paused):
 *     Pointer slots not currently assigned to an entity. This is where DARKEN_SPAWN() takes its next entity from,
 *     and where an active entity's slot goes right after it's deleted.
 *
 * - Paused zone [paused, capacity):
 *     Entity pointers parked out of the update loop. darken_update() never touches them and DARKEN_FOREACH
 *     never visits them. Crucially, DARKEN_SPAWN() never hands out a slot from this zone, so a paused entity's
 *     slot (and therefore its entity->data pointer) stays valid and untouched until it's explicitly resumed or
 *     deleted. This is what lets keep safely pointing at a paused entity's data.
 *
 * Neither darken_init(), nor DARKEN_SPAWN(), nor deletion (whichever path triggers it) initializes or clears
 * update/destroy/tag/usr. An entity handed out by DARKEN_SPAWN() — whether fresh from darken_init() or recycled
 * after a previous entity in that slot was deleted — may still carry whatever values that slot's previous
 * occupant left behind. Setting these fields to the values your entity actually needs (including clearing
 * any you don't want carried over) is the caller's responsibility on every spawn.
 *
 *
 *
 * Entity state/destroy callback type.
 *
 * Signature: void callback(darken_entity entity, void *data)
 *
 * Called every frame for `update`, and once on delete/reset for `destroy`. Receives the entity handle as the
 * first parameter, and the entity's own payload (entity->data, cast to your payload type) as the second parameter.
 * The second parameter may be omitted in the callback's own declaration (or marked unused) when it isn't needed,
 * since darken_state is declared without a prototype.
 *
 * To transition an entity to a different state, assign directly to entity->update (and/or entity->destroy) from
 * within the callback itself:
 *
 *     void player_walk_state(darken_entity entity, struct player *data) {
 *         data->x++;
 *
 *         if (should_stop(data))
 *             entity->update = player_stop_state;
 *     }
 */

#pragma once

#include <stdint.h>

typedef void (*darken_state)();
typedef struct darken_entity *darken_entity;

typedef struct darken
{
    darken_entity *pool;
    uint8_t *storage;
    uint16_t capacity;
    uint16_t size;
    uint16_t paused;
    uint16_t stride;
} darken;

struct darken_entity
{
    // Private
    uint16_t slot;
    darken *owner;

    // Lifecycle callbacks
    darken_state update;
    darken_state destroy;

    // User-defined fields
    uint32_t tag;
    uint16_t usr;

    // Payload
    uint8_t data[];
};

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

void darken_init(darken *);
void darken_update(darken *);
void darken_reset(darken *);

void darken_entity_pause(darken_entity);
void darken_entity_resume(darken_entity);
void darken_entity_delete(darken_entity);

// Free .pool & .storage
#define DARKEN_POOL_ALLOC(ALLOC, CAPACITY, PAYLOAD)                                 \
    {                                                                               \
        .pool = (darken_entity *)(ALLOC)((CAPACITY) * sizeof(darken_entity)),       \
        .storage = (uint8_t *)(ALLOC)((CAPACITY) * _DARKEN_ENTITY_STRIDE(PAYLOAD)), \
        .capacity = (CAPACITY),                                                     \
        .stride = _DARKEN_ENTITY_STRIDE(PAYLOAD),                                   \
    }

#define DARKEN_POOL_DECLARE(NAME, CAPACITY, PAYLOAD)                                           \
    struct                                                                                     \
    {                                                                                          \
        uint16_t capacity;                                                                     \
        uint16_t stride;                                                                       \
        darken_entity pool[(CAPACITY)];                                                        \
        uint8_t data[(CAPACITY) * _DARKEN_ENTITY_STRIDE(PAYLOAD)] __attribute__((aligned(4))); \
    } NAME = {                                                                                 \
        .capacity = (CAPACITY),                                                                \
        .stride = _DARKEN_ENTITY_STRIDE(PAYLOAD),                                              \
    }

// Static/global initialization: compile-time constants
#define DARKEN_POOL_INIT(STORAGE)                                                            \
    {                                                                                        \
        .pool = (STORAGE).pool,                                                              \
        .storage = (STORAGE).data,                                                           \
        .capacity = sizeof((STORAGE).pool) / sizeof(darken_entity),                          \
        .stride = sizeof((STORAGE).data) / (sizeof((STORAGE).pool) / sizeof(darken_entity)), \
    }

// Runtime: locals, reassignment, any context
#define DARKEN_POOL_BIND(NAME)       \
    {                                \
        .pool = (NAME).pool,         \
        .storage = (NAME).data,      \
        .capacity = (NAME).capacity, \
        .stride = (NAME).stride,     \
    }

#define DARKEN_SPAWN(CTX) ({                                  \
    darken *_ctx = (CTX);                                     \
    _ctx->size < _ctx->paused ? _ctx->pool[_ctx->size++] : 0; \
})

#define DARKEN_FOREACH(CTX, CODE)                        \
    do                                                   \
    {                                                    \
        uint16_t _index = (CTX)->size;                   \
        if (_index)                                      \
        {                                                \
            darken_entity _entity, *_pool = (CTX)->pool; \
            while (_index--)                             \
            {                                            \
                _entity = _pool[_index];                 \
                CODE;                                    \
            }                                            \
        }                                                \
    } while (0)

#define DARKEN_DATA(TYPE, VAR, ENTITY) TYPE *VAR = (TYPE *)(ENTITY)->data;
#define DARKEN_ENTITY(DATA) ((darken_entity)((uint8_t *)(DATA) - (uint32_t)&((darken_entity)0)->data))

#define DARKEN_ENTITY_IS_ACTIVE(ENTITY) ((ENTITY)->slot < (ENTITY)->owner->size)
#define DARKEN_ENTITY_IS_PAUSED(ENTITY) ((ENTITY)->slot >= (ENTITY)->owner->paused)
#define DARKEN_ENTITY_IS_USE(ENTITY) (DARKEN_ENTITY_IS_ACTIVE(ENTITY) || DARKEN_ENTITY_IS_PAUSED(ENTITY))
#define DARKEN_ENTITY_IS_FREE(ENTITY) (!DARKEN_ENTITY_IS_USE(ENTITY))

/* ============================================================================
 * PRIVATE
 * ============================================================================ */

#define _DARKEN_ALIGN4(X) (((X) + 3U) & ~3U)
#define _DARKEN_ENTITY_STRIDE(PAYLOAD) _DARKEN_ALIGN4(sizeof(struct darken_entity) + (PAYLOAD))

static inline void _darken_swap(darken_entity pool[], uint16_t i, uint16_t j)
{
    if (i == j)
        return;

    darken_entity tmp = pool[i];
    pool[i] = pool[j];
    pool[j] = tmp;
    pool[i]->slot = i;
    pool[j]->slot = j;
}

#ifdef DARKEN_IMPLEMENTATION

void darken_init(darken *ctx)
{
    ctx->size = 0;
    uint16_t capacity = ctx->paused = ctx->capacity;
    uint8_t *storage = ctx->storage;
    uint16_t step = ctx->stride;
    darken_entity entity, *pool = ctx->pool;

    while (capacity--)
    {
        entity = pool[capacity] = (darken_entity)storage;
        entity->owner = ctx;
        entity->slot = capacity;

        storage += step;
    }
}

void darken_update(darken *ctx)
{
    DARKEN_FOREACH(ctx, {
        // if (_entity->update)
            _entity->update(_entity, _entity->data);
    });
}

void darken_reset(darken *ctx)
{
    DARKEN_FOREACH(ctx, {
        if (_entity->destroy)
            _entity->destroy(_entity, _entity->data);
    });

    ctx->size = 0;
    ctx->paused = ctx->capacity;
}

void darken_entity_pause(darken_entity entity)
{
    if (!DARKEN_ENTITY_IS_ACTIVE(entity))
        return;

    _darken_swap(entity->owner->pool, entity->slot, --entity->owner->size);
    _darken_swap(entity->owner->pool, entity->slot, --entity->owner->paused);
}

void darken_entity_resume(darken_entity entity)
{
    if (!DARKEN_ENTITY_IS_PAUSED(entity))
        return;

    _darken_swap(entity->owner->pool, entity->slot, entity->owner->paused++);
    _darken_swap(entity->owner->pool, entity->slot, entity->owner->size++);
}

void darken_entity_delete(darken_entity entity)
{
    if (DARKEN_ENTITY_IS_ACTIVE(entity))
    {
        if (entity->destroy)
            entity->destroy(entity, entity->data);

        _darken_swap(entity->owner->pool, entity->slot, --entity->owner->size);
    }
    else if (DARKEN_ENTITY_IS_PAUSED(entity))
        _darken_swap(entity->owner->pool, entity->slot, entity->owner->paused++);
}

#endif // DARKEN_IMPLEMENTATION
