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
 * the ctx's zones is only the *pointer* to it inside darken.pool[]. This is what makes it safe to keep a
 * raw pointer into entity->data even while the entity gets paused/resumed/reordered.
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
 * Every entity handed out by DARKEN_SPAWN() — whether fresh from darken_init() or recycled after a
 * previous entity in that slot was deleted — always starts with update/destroy == DARKEN_DELETE and
 * tag/usr == 0. Deletion (whichever path triggers it) resets those four fields before the slot goes
 * back to the free zone, so no state ever leaks from one entity's lifetime into the next one reusing
 * its slot.
 *
 *
 *
 * Entity state/destroy callback type.
 *
 * Receives entity->data and returns either another state callback or one of the Darken control values.
 */

#ifndef DARKEN_H
#define DARKEN_H

#include <stdint.h>

typedef void *(*darken_state)();

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

#define DARKEN_DATA(TYPE, VAR, ENTITY) TYPE *VAR = (TYPE *)(ENTITY)->data;
#define DARKEN_DATA_GET_ENTITY(DATA) ((darken_entity)((uint8_t *)(DATA) - (uint32_t)&((darken_entity)0)->data))

// Darken control values
#define DARKEN_DELETE ((void *)0)
#define DARKEN_LOOP ((void *)1)
#define DARKEN_PAUSE ((void *)2)

uint16_t darken_entity_run(darken_entity);
uint16_t darken_entity_update(darken_entity);
uint16_t darken_entity_pause(darken_entity);
uint16_t darken_entity_resume(darken_entity);
uint16_t darken_entity_delete(darken_entity);

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

#define DARKEN_FOREACH(CTX, CODE) _DARKEN_BLOCK(   \
    uint16_t _index = (CTX)->size;                 \
    if (_index) {                                  \
        darken_entity *_pool = (CTX)->pool;        \
        while (_index--)                           \
        {                                          \
            darken_entity _entity = _pool[_index]; \
            (void)_entity;                         \
            CODE;                                  \
        }                                          \
    })

#define DARKEN_SPAWN(CTX) ({                                  \
    darken *_ctx = (CTX);                                     \
    _ctx->size < _ctx->paused ? _ctx->pool[_ctx->size++] : 0; \
})

void darken_init(darken *);
void darken_update(darken *);
void darken_reset(darken *);

/* ============================================================================
 * PRIVATE MACROS
 * ============================================================================ */

/**
 * Align a byte count to a 4-byte boundary.
 *
 * The Motorola 68000 requires word alignment for word/long accesses.
 * Longword alignment keeps entity strides predictable and efficient.
 */
#define _DARKEN_ALIGN4(X) (((X) + 3U) & ~3U)

#define _DARKEN_ENTITY_STRIDE(PAYLOAD) _DARKEN_ALIGN4(sizeof(struct darken_entity) + (PAYLOAD))

#define _DARKEN_BLOCK(CODE) \
    do                      \
    {                       \
        CODE                \
    } while (0)

#define _DARKEN_ASSERT(COND, CODE, RET) \
    if (!(COND))                        \
        return 0;                       \
    CODE;                               \
    return RET;

#define _DARKEN_RUN(ENTITY) \
    ENTITY->update(ENTITY->data);

#define _DARKEN_UPDATE(ENTITY) _DARKEN_BLOCK(    \
    darken_state callback = _DARKEN_RUN(ENTITY); \
                                                 \
    if (!_DARKEN_STATE_IS_LOOP(callback))        \
        ENTITY->update = callback;)

#define _DARKEN_PAUSE(ENTITY) _DARKEN_BLOCK(                                \
    _darken_swap(ENTITY->owner->pool, ENTITY->slot, --ENTITY->owner->size); \
    _darken_swap(ENTITY->owner->pool, ENTITY->slot, --ENTITY->owner->paused);)

#define _DARKEN_RESUME(ENTITY) _DARKEN_BLOCK(           \
    darken *ctx = ENTITY->owner;                        \
                                                        \
    _darken_swap(ctx->pool, ENTITY->slot, ctx->paused); \
    _darken_swap(ctx->pool, ENTITY->slot, ctx->size);   \
                                                        \
    ++ctx->paused;                                      \
    ++ctx->size;)

#define _DARKEN_DESTROY(ENTITY) _DARKEN_BLOCK(    \
    darken *ctx = ENTITY->owner;                  \
                                                  \
    if (_DARKEN_STATE_IS_ACTIVE(ENTITY->destroy)) \
        ENTITY->destroy(ENTITY->data);            \
                                                  \
    _darken_swap(ctx->pool, ENTITY->slot, --ctx->size);)

#define _DARKEN_DELETE(ENTITY) _DARKEN_BLOCK( \
    if (_DARKEN_ENTITY_IN_ACTIVE(ENTITY))     \
        _DARKEN_DESTROY(ENTITY);              \
    else _darken_swap(ENTITY->owner->pool, ENTITY->slot, ENTITY->owner->paused++););

// Probably will be public
#define _DARKEN_STATE_IS_DELETED(STATE) ((STATE) == (darken_state)0)
#define _DARKEN_STATE_IS_LOOP(STATE) ((STATE) == (darken_state)1)
#define _DARKEN_STATE_IS_PAUSED(STATE) ((STATE) == (darken_state)2)
#define _DARKEN_STATE_IS_ACTIVE(STATE) ((STATE) > (darken_state)2)

#define _DARKEN_ENTITY_IN_ACTIVE(ENTITY) ((ENTITY)->slot < (ENTITY)->owner->size)
#define _DARKEN_ENTITY_IN_PAUSED(ENTITY) ((ENTITY)->slot >= (ENTITY)->owner->paused)
#define _DARKEN_ENTITY_IN_USE(ENTITY) (_DARKEN_ENTITY_IN_ACTIVE(ENTITY) || _DARKEN_ENTITY_IN_PAUSED(ENTITY))
#define _DARKEN_ENTITY_IN_FREE(ENTITY) (!_DARKEN_ENTITY_IN_USE(ENTITY))
//

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DARKEN_IMPLEMENTATION

static inline uint16_t _darken_swap(darken_entity pool[], uint16_t i, uint16_t j)
{
    _DARKEN_ASSERT(i != j, {
            darken_entity tmp = pool[i];
            pool[i] = pool[j];
            pool[j] = tmp;
            pool[i]->slot = i;
            pool[j]->slot = j; }, 1);
}

//
// DYNAMIC
// darken m = DARKEN_POOL_ALLOC(MEM_alloc, 5, sizeof(struct MyComponent));
// darken_init(&m);
// ...
// free(m.pool);
// free(m.storage);

// STATIC:  Runtime: locals, reassignment, any context
// DARKEN_POOL_DECLARE(storage, 5, sizeof(struct MyComponent));
// darken m = DARKEN_POOL_BIND(storage);
// darken_init(&m);

// STATIC:  Static/global initialization: compile-time constants
// DARKEN_POOL_DECLARE(storage, 5, sizeof(struct MyComponent));
// darken m = DARKEN_POOL_INIT(storage);
//
// void init_test_manager() {
//     darken_init(&m);
//     ...
// }

void darken_init(darken *ctx)
{
    ctx->size = 0;
    ctx->paused = ctx->capacity;

    uint8_t *p = ctx->storage;
    uint16_t step = ctx->stride;
    darken_entity *pool = ctx->pool;
    uint16_t n = ctx->capacity;

    while (n--)
    {
        darken_entity e = (darken_entity)p;

        pool[n] = e;
        e->owner = ctx;
        e->slot = n;

        p += step;
    }
}

void darken_update(darken *ctx)
{
    DARKEN_FOREACH(ctx, {
        if (_DARKEN_STATE_IS_ACTIVE(_entity->update))
            _DARKEN_UPDATE(_entity);

        else if (_DARKEN_STATE_IS_PAUSED(_entity->update))
            _DARKEN_PAUSE(_entity);

        else if (_DARKEN_STATE_IS_DELETED(_entity->update))
            _DARKEN_DESTROY(_entity);
    });
}

void darken_reset(darken *ctx)
{
    DARKEN_FOREACH(ctx, _DARKEN_DESTROY(_entity));

    ctx->size = 0;
    ctx->paused = ctx->capacity;
}

uint16_t darken_entity_run(darken_entity entity) { _DARKEN_ASSERT(_DARKEN_STATE_IS_ACTIVE(entity->update), _DARKEN_RUN(entity), 1); }
uint16_t darken_entity_update(darken_entity entity) { _DARKEN_ASSERT(_DARKEN_STATE_IS_ACTIVE(entity->update), _DARKEN_UPDATE(entity), 1); }
uint16_t darken_entity_pause(darken_entity entity) { _DARKEN_ASSERT(_DARKEN_ENTITY_IN_ACTIVE(entity), _DARKEN_PAUSE(entity), 1); }
uint16_t darken_entity_resume(darken_entity entity) { _DARKEN_ASSERT(_DARKEN_ENTITY_IN_PAUSED(entity), _DARKEN_RESUME(entity), 1); }
uint16_t darken_entity_delete(darken_entity entity) { _DARKEN_ASSERT(_DARKEN_ENTITY_IN_USE(entity), _DARKEN_DELETE(entity), 1); }

#endif // DARKEN_IMPLEMENTATION

#endif // DARKEN_H
