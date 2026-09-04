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
 * The entity structure serves as a container for user data with lifecycle management. The flexible array member
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
 * The entity entitys themselves live in the caller-provided storage block; ctx->pool contains pointers to
 * those fixed addresses.
 *
 * - Active zone [0, size):
 *     Entity pointers updated every frame by darken_update(). Iterable with DARKEN_FOREACH. Freely created
 *     (DARKEN_SPAWN) and deleted.
 *
 * - Free zone [size, paused):
 *     Pointer slots not currently assigned to an entity. This is where DARKEN_SPAWN() takes its next entity
 *     from, and where an active entity's slot goes right after it's deleted.
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
 * Update / lifecycle control — two selectable modes
 * ==================================================
 *
 * Define DARKEN_STATE_MACHINE before including this header to switch modes. It changes the signature of
 * `update`/`destroy` callbacks and what darken_update() does with their result; everything else in the API
 * (spawn/pause/resume/delete, the pool macros, the zone layout) is identical either way.
 *
 * 1) MANUAL mode — default, DARKEN_STATE_MACHINE not defined
 * ---------------------------------------------------------
 *     darken_update() just calls entity->update(...) every frame and ignores any return value;
 *     entity->destroy(...) is called the same way by darken_reset() and darken_entity_delete(). The callback
 *     is in full control of the entity's lifecycle: it changes state by assigning directly to entity->update
 *     (and/or entity->destroy), and it pauses/resumes/deletes itself by calling darken_entity_pause(),
 *     darken_entity_resume() or darken_entity_delete() explicitly.
 *
 *     What gets passed as `...` is controlled by one macro, DARKEN_CALLBACK_ARGS(ENTITY), redefinable before
 *     including this header. It receives the darken_entity handle and must expand to the argument list you want.
 *     Default (matches the original, fixed behavior):
 *
 *         #define DARKEN_CALLBACK_ARGS(ENTITY) (ENTITY)->data   // callback(data)
 *
 *     Other useful redefinitions:
 *
 *         #define DARKEN_CALLBACK_ARGS(ENTITY) (ENTITY)                   // callback(entity)
 *         #define DARKEN_CALLBACK_ARGS(ENTITY) (ENTITY), (ENTITY)->data   // callback(entity, data)
 *         #define DARKEN_CALLBACK_ARGS(ENTITY) (ENTITY)->data, (ENTITY)   // callback(data, entity)
 *
 *     Passing the entity directly means you no longer need DARKEN_ENTITY(data) to recover the handle inside
 *     the callback:
 *
 *         #define DARKEN_CALLBACK_ARGS(ENTITY) (ENTITY), (ENTITY)->data
 *
 *         void player_walk_state(darken_entity entity, struct player *data) {
 *             data->x++;
 *
 *             if (should_stop(data))
 *                 entity->update = player_stop_state;
 *         }
 *
 *     DARKEN_CALLBACK_ARGS has no effect in state-machine mode (see below) — there the signature is fixed so
 *     the return-value protocol stays well-defined.
 *
 * 2) STATE-MACHINE mode — DARKEN_STATE_MACHINE defined
 * ---------------------------------------------------------
 *     Signature: darken_state callback(void *data)
 *
 *     darken_update() reads the callback's return value and drives the lifecycle itself:
 *
 *         DARKEN_CONTINUE          stay active, keep the same update callback
 *         DARKEN_DELETE            call destroy (if set), then delete the entity
 *         DARKEN_PAUSE             move the entity straight to the paused zone
 *         (anything else)       treated as a new update callback pointer;
 *                                installed as entity->update for next frame
 *
 *         darken_state player_walk_state(struct player *data) {
 *             data->x++;
 *
 *             if (should_stop(data))
 *                 return player_stop_state;
 *
 *             if (should_die(data))
 *                 return DARKEN_DELETE;
 *
 *             return DARKEN_CONTINUE;
 *         }
 *
 *     `destroy` keeps the same callback type as `update` in this mode too, but its return value is always
 *     ignored — darken_reset() and darken_entity_delete() only ever call it for its side effects.
 *
 *     Comparing an darken_state value against the sentinels with `==`/`>` relies on GNU C's permissive
 *     pointer/integer handling (see the GNU C note above); it's the same technique the original state-machine
 *     variant of this engine already used.
 */

#pragma once

#include <stdint.h>

// Define DARKEN_STATE_MACHINE (before including this header) to switch update callbacks from "void, self-managed"
// to "returns darken_state, engine-managed". See the big comment above for the full explanation of both modes.
#ifdef DARKEN_STATE_MACHINE
typedef void *(*darken_state)();
#else
typedef void (*darken_state)();
#endif

typedef struct darken_entity *darken_entity;

typedef struct darken
{
    darken_entity *pool; // Pointer array to entitys in the ctx's storage block
    uint8_t *storage;    // Pointer to the contiguous memory block where entitys are allocated
    uint16_t capacity;
    uint16_t size;
    uint16_t paused;
    uint16_t stride;
} darken;

struct darken_entity
{
    uint16_t slot;        // Private: Index in the ctx's pool array
    uint16_t usr;         // User-defined field for custom data
    darken_state update;  // User-defined update callback
    darken_state destroy; // User-defined destroy callback
    uint32_t tag;         // User-defined tag for identification or categorization
    darken *owner;        // Private: Pointer to the owning ctx
    uint8_t data[];       // Payload
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

#ifdef DARKEN_STATE_MACHINE
// Sentinel return values for update() callbacks in state-machine mode.
// Any other darken_state value returned is treated as the next update callback.
#define DARKEN_CONTINUE ((darken_state)1)
#define DARKEN_DELETE ((darken_state)0)
#define DARKEN_PAUSE ((darken_state)2)
#endif

// Argument list passed to update()/destroy() in MANUAL mode (ignored in DARKEN_STATE_MACHINE mode, where the
// signature is always fixed to (data)).
// Redefine this before including the header to change it; ENTITY is the darken_entity handle. Default matches
// the original, non-configurable behavior: callback(data).
#ifndef DARKEN_CALLBACK_ARGS
#define DARKEN_CALLBACK_ARGS(ENTITY) (ENTITY)->data
#endif

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

#define DARKEN_ENTITY_IN_ACTIVE(ENTITY) ((ENTITY)->slot < (ENTITY)->owner->size)
#define DARKEN_ENTITY_IN_PAUSE(ENTITY) ((ENTITY)->slot >= (ENTITY)->owner->paused)
#define DARKEN_ENTITY_IN_FREE(ENTITY) (!DARKEN_ENTITY_IN_ACTIVE(ENTITY) && !DARKEN_ENTITY_IN_PAUSE(ENTITY))

// Zone sizes, so callers don't have to do the size/paused/capacity math by hand.
#define DARKEN_COUNT_ACTIVE(CTX) ((CTX)->size)
#define DARKEN_COUNT_FREE(CTX) ((uint16_t)((CTX)->paused - (CTX)->size))
#define DARKEN_COUNT_PAUSED(CTX) ((uint16_t)((CTX)->capacity - (CTX)->paused))

/* ============================================================================
 * PRIVATE
 * ============================================================================ */

#define _DARKEN_ALIGN4(X) (((X) + 3U) & ~3U)
#define _DARKEN_ENTITY_STRIDE(PAYLOAD) _DARKEN_ALIGN4(sizeof(struct darken_entity) + (PAYLOAD))

// Single call-site helper for invoking update()/destroy(), used everywhere the engine calls into user code.
// Fixed to (data) in DARKEN_STATE_MACHINE mode; otherwise expands via DARKEN_CALLBACK_ARGS.
// Since darken_state has no prototype, the callback only needs to declare whichever leading arguments it
// actually reads.
#ifdef DARKEN_STATE_MACHINE
#define _DARKEN_CALL(FN, ENTITY) (FN)((ENTITY)->data)
#else
#define _DARKEN_CALL(FN, ENTITY) (FN)(DARKEN_CALLBACK_ARGS(ENTITY))
#endif

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

#ifdef DARKEN_STATE_MACHINE

void darken_update(darken *ctx)
{
    DARKEN_FOREACH(ctx, {
        darken_state state = _entity->update(_entity->data);

        if (state == DARKEN_CONTINUE)
            continue;

        if (state > DARKEN_PAUSE)
        {
            _entity->update = state;
        }
        else if (state == DARKEN_DELETE)
        {
            if (_entity->destroy)
                _entity->destroy(_entity->data);

            _darken_swap(ctx->pool, _entity->slot, --ctx->size);
        }
        else if (state == DARKEN_PAUSE)
        {
            _darken_swap(ctx->pool, _entity->slot, --ctx->size);
            _darken_swap(ctx->pool, _entity->slot, --ctx->paused);
        }
    });
}

#else // !DARKEN_STATE_MACHINE

void darken_update(darken *ctx)
{
    DARKEN_FOREACH(ctx, {
        // if (_entity->update)
            _DARKEN_CALL(_entity->update, _entity);
    });
}

#endif // DARKEN_STATE_MACHINE

void darken_reset(darken *ctx)
{
    DARKEN_FOREACH(ctx, {
        if (_entity->destroy)
            _DARKEN_CALL(_entity->destroy, _entity);
    });

    ctx->size = 0;
    ctx->paused = ctx->capacity;
}

void darken_entity_pause(darken_entity entity)
{
    if (!DARKEN_ENTITY_IN_ACTIVE(entity))
        return;

    _darken_swap(entity->owner->pool, entity->slot, --entity->owner->size);
    _darken_swap(entity->owner->pool, entity->slot, --entity->owner->paused);
}

void darken_entity_resume(darken_entity entity)
{
    if (!DARKEN_ENTITY_IN_PAUSE(entity))
        return;

    _darken_swap(entity->owner->pool, entity->slot, entity->owner->paused++);
    _darken_swap(entity->owner->pool, entity->slot, entity->owner->size++);
}

void darken_entity_delete(darken_entity entity)
{
    if (DARKEN_ENTITY_IN_ACTIVE(entity))
    {
        if (entity->destroy)
            _DARKEN_CALL(entity->destroy, entity);

        _darken_swap(entity->owner->pool, entity->slot, --entity->owner->size);
    }
    else if (DARKEN_ENTITY_IN_PAUSE(entity))
        _darken_swap(entity->owner->pool, entity->slot, entity->owner->paused++);
}

#endif // DARKEN_IMPLEMENTATION
