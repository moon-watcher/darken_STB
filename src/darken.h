/**
 * darken.h — Darken (DARKula ENgine) 2.0 Entity System
 *
 * darken-2.0.0-dev
 *
 * Full documentation: README.Darken.md
 *
 * GNU C note:
 * - This header uses GNU C __attribute__ extension.
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
 *     (darken_spawn) and deleted.
 *
 * - Free zone [size, paused):
 *     Pointer slots not currently assigned to an entity. This is where darken_spawn() takes its next entity from,
 *     and where an active entity's slot goes right after it's deleted.
 *
 * - Paused zone [paused, capacity):
 *     Entity pointers parked out of the update loop. darken_update() never touches them and DARKEN_FOREACH
 *     never visits them. Crucially, darken_spawn() never hands out a slot from this zone, so a paused entity's
 *     slot (and therefore its entity->data pointer) stays valid and untouched until it's explicitly resumed or
 *     deleted. This is what lets keep safely pointing at a paused entity's data.
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

typedef void *(*darken_callback)();

typedef struct darken_entity *darken_entity;

typedef struct darken
{
    darken_entity *pool;

    uint16_t capacity;
    uint16_t size;
    uint16_t paused;
} darken;

struct darken_entity
{
    // Private
    uint16_t slot;
    darken *owner;

    // Lifecycle callbacks
    darken_callback update;
    darken_callback destroy;

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

#define BARKEN_STATE_IS_DELETED(STATE) ((STATE) == (darken_callback)0)
#define DARKEN_STATE_IS_LOOP(STATE) ((STATE) == (darken_callback)1)
#define DARKEN_STATE_IS_PAUSED(STATE) ((STATE) == (darken_callback)2)
#define DARKEN_STATE_IS_ACTIVE(STATE) ((STATE) > (darken_callback)2)

#define DARKEN_ENTITY_IN_ACTIVE(ENTITY) ((ENTITY)->slot < (ENTITY)->owner->size)
#define DARKEN_ENTITY_IN_PAUSED(ENTITY) ((ENTITY)->slot >= (ENTITY)->owner->paused)
#define DARKEN_ENTITY_IN_FREE(ENTITY) (!(DARKEN_ENTITY_IN_ACTIVE(ENTITY) || DARKEN_ENTITY_IN_PAUSED(ENTITY)))

uint16_t darken_entity_run(darken_entity);
uint16_t darken_entity_update(darken_entity);
uint16_t darken_entity_pause(darken_entity);
uint16_t darken_entity_resume(darken_entity);
uint16_t darken_entity_delete(darken_entity);

/**
 * Align a byte count to a 4-byte boundary.
 *
 * The Motorola 68000 requires word alignment for word/long accesses.
 * Longword alignment keeps entity strides predictable and efficient.
 */
#define DARKEN_ALIGN4(X) (((X) + 3U) & ~3U)

// Ensures proper alignment between consecutive entitys
#define DARKEN_ENTITY_STRIDE(PAYLOAD) DARKEN_ALIGN4(sizeof(struct darken_entity) + (PAYLOAD))

#define DARKEN_POOL_MEMORY(CAPACITY) ((CAPACITY) * sizeof(darken_entity))
#define DARKEN_STORAGE_MEMORY(CAPACITY, PAYLOAD) ((CAPACITY) * DARKEN_ENTITY_STRIDE(PAYLOAD))

#define DARKEN_DECLARE_STORAGE(NAME, CAPACITY, PAYLOAD)                                         \
    struct                                                                                      \
    {                                                                                           \
        uint16_t capacity;                                                                      \
        uint16_t payload_size;                                                                  \
        darken_entity pool[(CAPACITY)];                                                         \
        uint8_t data[(CAPACITY) * DARKEN_ENTITY_STRIDE((PAYLOAD))] __attribute__((aligned(4))); \
    } NAME = {                                                                                  \
        .capacity = (CAPACITY),                                                                 \
        .payload_size = (PAYLOAD),                                                              \
    }

#define DARKEN_ARGS(NAME) \
    (NAME).pool, (NAME).data, (NAME).capacity, (NAME).payload_size

#define DARKEN_FOREACH(CTX, CODE)                      \
    do                                                 \
    {                                                  \
        uint16_t _index = (CTX)->size;                 \
        if (_index)                                    \
        {                                              \
            darken_entity *_pool = (CTX)->pool;        \
                                                       \
            while (_index--)                           \
            {                                          \
                darken_entity _entity = _pool[_index]; \
                (void)_entity;                         \
                CODE;                                  \
            }                                          \
        }                                              \
    } while (0)

void darken_init(darken *, darken_entity[], void *, uint16_t, uint16_t);
darken_entity darken_spawn(darken *);
void darken_update(darken *);
void darken_reset(darken *);

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DARKEN_IMPLEMENTATION

#define _DARKEN_ASSERT(COND, CODE, RET) \
    if (!(COND))                        \
        return 0;                       \
                                        \
    CODE;                               \
                                        \
    return RET;

#define _DARKEN_BLOCK(CODE) \
    do                      \
    {                       \
        CODE                \
    } while (0)

#define _DARKEN_RUN(ENTITY) \
    ENTITY->update(ENTITY->data);

#define _DARKEN_UPDATE(ENTITY) _DARKEN_BLOCK(       \
    darken_callback callback = _DARKEN_RUN(ENTITY); \
                                                    \
    if (!DARKEN_STATE_IS_LOOP(callback))            \
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

#define _DARKEN_DELETE(ENTITY) _DARKEN_BLOCK(    \
    darken *ctx = ENTITY->owner;                 \
                                                 \
    if (DARKEN_STATE_IS_ACTIVE(ENTITY->destroy)) \
        ENTITY->destroy(ENTITY->data);           \
                                                 \
    _darken_swap(ctx->pool, ENTITY->slot, --ctx->size);)

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

/*
DARKEN_DECLARE_STORAGE(enemy_storage, MAX_ENEMIES, sizeof(EnemyData));
darken_init(&manager, DARKEN_ARGS(enemy_storage));

or

darken_entity *pool = malloc(DARKEN_POOL_MEMORY(MAX_ENEMIES));
uint8_t *storage = malloc(DARKEN_STORAGE_MEMORY(MAX_ENEMIES, sizeof(EnemyData)));
darken_init(&manager, pool, storage, MAX_ENEMIES, sizeof(EnemyData));
free pool & storage
*/
void darken_init(darken *ctx, darken_entity pool[], void *param_storage, uint16_t capacity, uint16_t payload_size)
{
    ctx->pool = pool;
    ctx->capacity = capacity;
    ctx->size = 0;
    ctx->paused = capacity;

    uint16_t stride = DARKEN_ENTITY_STRIDE(payload_size);
    uint8_t *storage = param_storage;

    for (uint16_t i = 0; i < capacity; ++i)
    {
        darken_entity entity = (darken_entity)storage;

        pool[i] = entity;
        entity->owner = ctx;
        entity->slot = i;

        storage += stride;
    }
}

darken_entity darken_spawn(darken *ctx)
{
    _DARKEN_ASSERT(ctx->size < ctx->paused, , ctx->pool[ctx->size++];);
}

void darken_update(darken *ctx)
{
    DARKEN_FOREACH(ctx, {
        if (DARKEN_STATE_IS_ACTIVE(_entity->update))
            _DARKEN_UPDATE(_entity);

        else if (DARKEN_STATE_IS_PAUSED(_entity->update))
            _DARKEN_PAUSE(_entity);

        else if (BARKEN_STATE_IS_DELETED(_entity->update))
            _DARKEN_DELETE(_entity);
    });
}

void darken_reset(darken *ctx)
{
    DARKEN_FOREACH(ctx, _DARKEN_DELETE(_entity));

    ctx->size = 0;
    ctx->paused = ctx->capacity;
}

uint16_t darken_entity_run(darken_entity entity)
{
    _DARKEN_ASSERT(DARKEN_STATE_IS_ACTIVE(entity->update), _DARKEN_RUN(entity), 1);
}

uint16_t darken_entity_update(darken_entity entity)
{
    _DARKEN_ASSERT(DARKEN_STATE_IS_ACTIVE(entity->update), _DARKEN_UPDATE(entity), 1);
}

uint16_t darken_entity_pause(darken_entity entity)
{
    _DARKEN_ASSERT(DARKEN_ENTITY_IN_ACTIVE(entity), _DARKEN_PAUSE(entity), 1);
}

uint16_t darken_entity_resume(darken_entity entity)
{
    _DARKEN_ASSERT(DARKEN_ENTITY_IN_PAUSED(entity), _DARKEN_RESUME(entity), 1);
}

uint16_t darken_entity_delete(darken_entity entity)
{
    _DARKEN_ASSERT(!DARKEN_ENTITY_IN_FREE(entity), {
            if (DARKEN_ENTITY_IN_ACTIVE(entity))
                _DARKEN_DELETE(entity);
            else
                _darken_swap(entity->owner->pool, entity->slot, entity->owner->paused++); }, 1);
}

#endif // DARKEN_IMPLEMENTATION

#endif // DARKEN_H
