/**
 * darken.h — Darken (DARKula ENgine) Entity System
 *
 * darken-1.1.0_dev
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
 * 'data[]' allows entities to have variable-sized payloads while maintaining contiguous memory layout.
 *
 * The stride between entities is pre-calculated during ctx initialization to enable O(1) access to any entity
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
 *    [ active entities ][   free slots    ][ paused entities ]
 *    0                 size               paused             capacity
 *
 * The entities themselves live in the caller-provided storage block; ctx->pool contains pointers to
 * those fixed addresses.
 *
 * - Active zone [0, size):
 *     Entity pointers updated every frame by darken_update(). Iterable with DARKEN_FOREACH (note: iterates
 *     in reverse order, from size-1 down to 0). Freely created (DARKEN_SPAWN) and deleted.
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
 * STATE-MACHINE mode is the default. Define DARKEN_DIRECT before including this header to opt into direct
 * mode instead.
 *
 * Each mode has one fixed callback signature — there is no separate configuration macro for the argument
 * list anymore. The signature is chosen per mode to match how that mode is actually used: state-machine
 * callbacks rarely need the entity handle, since the return value drives the lifecycle; direct-mode callbacks
 * almost always need it, since they call darken_entity_pause()/resume()/delete() themselves.
 *
 * 1) STATE-MACHINE mode — default
 * ---------------------------------------------------------
 *     Signature: darken_state callback(void *data)
 *
 *     Only the entity's payload is passed — never the entity handle. darken_update() reads update()'s return
 *     value and drives the lifecycle itself:
 *
 *         DARKEN_CONTINUE: stay active, keep the same update callback
 *         DARKEN_DELETE:   call destroy (if set), then delete the entity
 *         DARKEN_PAUSE:    move the entity straight to the paused zone
 *         (anything else): treated as a new update callback pointer; installed as entity->update for next frame
 *
 *         void *player_walk_state(struct player *data) {
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
 *     `destroy` has the same signature and the same (data)-only argument as `update`, but its return value is
 *     always ignored — darken_reset() and darken_entity_delete() only ever call it for its side effects.
 *
 *     Need the entity handle anyway (e.g. to read/write usr or tag)? Recover it with DARKEN_ENTITY(data).
 *
 *     Comparing a darken_state value against the sentinels with `==`/`>` relies on GNU C's permissive
 *     pointer/integer handling (see the GNU C note above).
 *
 *     IMPORTANT: In STATE-MACHINE mode, callbacks receive ONLY the data pointer. They do NOT receive
 *     the entity handle. If you need the entity handle, use DARKEN_ENTITY(data) to recover it.
 *
 * 2) DIRECT mode — DARKEN_DIRECT defined
 * ---------------------------------------------------------
 *     Signature: void callback(darken_entity entity)
 *                void callback(darken_entity entity, void *data)
 *
 *     Both the entity handle and its payload are passed, in that order. darken_update() just calls
 *     entity->update(entity, entity->data) every frame and ignores any return value; entity->destroy(entity,
 *     entity->data) is called the same way by darken_reset() and darken_entity_delete(). The callback is in
 *     full control of the entity's lifecycle: it changes state by assigning directly to entity->update
 *     (and/or entity->destroy), and it pauses/resumes/deletes itself by calling darken_entity_pause(),
 *     darken_entity_resume() or darken_entity_delete() — all of which take the handle it was just given
 *     directly, no DARKEN_ENTITY(data) lookup needed.
 *
 *         void player_walk_state(darken_entity entity) {
 *             DARKEN_DATA(struct player, data, entity);
 *             data->x++;
 *         }
 *
 *         void player_walk_state(darken_entity entity, struct player *data) {
 *             data->x++;
 *
 *             if (should_stop(data))
 *                 entity->update = player_stop_state;
 *
 *             if (should_die(data))
 *                 darken_entity_delete(entity);
 *         }
 *
 *     A callback that only declares the entity parameter (e.g. `void f(darken_entity entity)`) still works:
 *     darken_state has no prototype, so the callee just reads however many leading arguments it declares and
 *     the rest are pushed and ignored.
 */

#pragma once

#include <stdint.h>

#ifdef DARKEN_DIRECT
typedef void (*darken_state)();
#else
typedef void *(*darken_state)();
#endif

typedef struct darken_entity *darken_entity;

typedef struct darken
{
    darken_entity *pool; // Pointer array to entities in the ctx's storage block
    uint8_t *storage;    // Pointer to the contiguous memory block where entities are allocated
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

#ifndef DARKEN_DIRECT
// Sentinel return values for update() callbacks in state-machine mode.
// Any other darken_state value returned is treated as the next update callback.
#define DARKEN_CONTINUE ((darken_state)1)
#define DARKEN_DELETE ((darken_state)0)
#define DARKEN_PAUSE ((darken_state)2)
#endif

// Dynamic allocation: use with malloc/calloc or custom allocator
// darken m = DARKEN_POOL_ALLOC(MEM_alloc, 5, sizeof(struct MyComponent));
// darken_init(&m);
// ...
// free(m.pool);
// free(m.storage);
#define DARKEN_POOL_ALLOC(ALLOC, CAPACITY, PAYLOAD)                                 \
    {                                                                               \
        .pool = (darken_entity *)(ALLOC)((CAPACITY) * sizeof(darken_entity)),       \
        .storage = (uint8_t *)(ALLOC)((CAPACITY) * _DARKEN_ENTITY_STRIDE(PAYLOAD)), \
        .capacity = (CAPACITY),                                                     \
        .stride = _DARKEN_ENTITY_STRIDE(PAYLOAD),                                   \
    }

// Static allocation with automatic storage duration (stack or global)
// DARKEN_POOL_DECLARE(storage, 5, sizeof(struct MyComponent));
// darken m = DARKEN_POOL_BIND(storage);
// darken_init(&m);
#define DARKEN_POOL_DECLARE(NAME, CAPACITY, PAYLOAD)                                           \
    struct                                                                                     \
    {                                                                                          \
        uint16_t capacity;                                                                     \
        uint16_t stride;                                                                       \
        darken_entity pool[(CAPACITY)] __attribute__((aligned(4)));                            \
        uint8_t data[(CAPACITY) * _DARKEN_ENTITY_STRIDE(PAYLOAD)] __attribute__((aligned(4))); \
    } NAME = {                                                                                 \
        .capacity = (CAPACITY),                                                                \
        .stride = _DARKEN_ENTITY_STRIDE(PAYLOAD),                                              \
    }

// Static/global initialization: compile-time constants
// Use when the storage is defined at file scope and you want compile-time initialization
#define DARKEN_POOL_INIT(STORAGE)                                                            \
    {                                                                                        \
        .pool = (STORAGE).pool,                                                              \
        .storage = (STORAGE).data,                                                           \
        .capacity = sizeof((STORAGE).pool) / sizeof(darken_entity),                          \
        .stride = sizeof((STORAGE).data) / (sizeof((STORAGE).pool) / sizeof(darken_entity)), \
    }

// Runtime binding: locals, reassignment, any context
// Use when you need to (re)bind a darken context to storage at runtime
#define DARKEN_POOL_BIND(NAME)       \
    {                                \
        .pool = (NAME).pool,         \
        .storage = (NAME).data,      \
        .capacity = (NAME).capacity, \
        .stride = (NAME).stride,     \
    }

// Spawn a new entity from the free zone. Returns the entity or NULL if no free slots.
// The returned entity may contain garbage from a previous occupant — always initialize
// all fields you care about (update, destroy, tag, usr, and data).
#define DARKEN_SPAWN(CTX) ({                                  \
    darken *_ctx = (CTX);                                     \
    _ctx->size < _ctx->paused ? _ctx->pool[_ctx->size++] : 0; \
})

// Iterate over all active entities in REVERSE order (from size-1 down to 0).
// Reverse order allows safe deletion during iteration.
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

// Declare a typed pointer to an entity's data payload
#define DARKEN_DATA(TYPE, VAR, ENTITY) TYPE *VAR = (TYPE *)(ENTITY)->data;

// Recover the entity handle from a pointer to its data payload
// (Mostly useful in STATE-MACHINE mode where callbacks only receive data)
#define DARKEN_ENTITY(DATA) ((darken_entity)((uint8_t *)(DATA) - (uint32_t)&((darken_entity)0)->data))

// Zone membership tests
#define DARKEN_ENTITY_IN_ACTIVE(ENTITY) ((ENTITY)->slot < (ENTITY)->owner->size)
#define DARKEN_ENTITY_IN_FREE(ENTITY) (!DARKEN_ENTITY_IN_ACTIVE(ENTITY) && !DARKEN_ENTITY_IN_PAUSED(ENTITY))
#define DARKEN_ENTITY_IN_PAUSED(ENTITY) ((ENTITY)->slot >= (ENTITY)->owner->paused)

// Zone sizes, so callers don't have to do the size/paused/capacity math by hand.
#define DARKEN_COUNT_ACTIVE(CTX) ((CTX)->size)
#define DARKEN_COUNT_FREE(CTX) ((uint16_t)((CTX)->paused - (CTX)->size))
#define DARKEN_COUNT_PAUSED(CTX) ((uint16_t)((CTX)->capacity - (CTX)->paused))

/* ============================================================================
 * PRIVATE
 * ============================================================================ */

// Single call-site helper for invoking update()/destroy(), used everywhere the engine calls into user code.
// The argument list is fixed per mode (see the big comment above), so there is nothing to configure here.
#ifdef DARKEN_DIRECT
#define _DARKEN_CALL(FN, ENTITY) (FN)((ENTITY), (ENTITY)->data)
#else
#define _DARKEN_CALL(FN, ENTITY) (FN)((ENTITY)->data)
#endif

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

//
// USAGE EXAMPLES:
//
// DYNAMIC:
// darken m = DARKEN_POOL_ALLOC(MEM_alloc, 5, sizeof(struct MyComponent));
// darken_init(&m);
// ...
// free(m.pool);
// free(m.storage);
//
// STATIC (Runtime binding):
// Runtime: locals, reassignment, any context
// DARKEN_POOL_DECLARE(storage, 5, sizeof(struct MyComponent));
// darken m = DARKEN_POOL_BIND(storage);
// darken_init(&m);
//
// STATIC (Compile-time initialization):
// Static/global initialization: compile-time constants
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
#ifdef DARKEN_DIRECT
    DARKEN_FOREACH(ctx, {
        _DARKEN_CALL(_entity->update, _entity);
    });
#else
    DARKEN_FOREACH(ctx, {
        darken_state state = _DARKEN_CALL(_entity->update, _entity);

        if (state == DARKEN_CONTINUE)
            continue;

        if (state > DARKEN_PAUSE)
        {
            _entity->update = state;
            continue;
        }

        if (state == DARKEN_DELETE)
        {
            if (_entity->destroy)
                _DARKEN_CALL(_entity->destroy, _entity);

            _darken_swap(ctx->pool, _entity->slot, --ctx->size);
            continue;
        }

        // DARKEN_PAUSE
        _darken_swap(ctx->pool, _entity->slot, --ctx->size);
        _darken_swap(ctx->pool, _entity->slot, --ctx->paused);
    });
#endif
}

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
    if (!DARKEN_ENTITY_IN_PAUSED(entity))
        return;

    _darken_swap(entity->owner->pool, entity->slot, entity->owner->paused++);
    _darken_swap(entity->owner->pool, entity->slot, entity->owner->size++);
}

// Note: darken_entity_delete() only calls destroy() if the entity is active.
// If the entity is paused, it's moved to the free zone without calling destroy().
void darken_entity_delete(darken_entity entity)
{
    if (DARKEN_ENTITY_IN_ACTIVE(entity))
    {
        if (entity->destroy)
            _DARKEN_CALL(entity->destroy, entity);

        _darken_swap(entity->owner->pool, entity->slot, --entity->owner->size);
    }
    else if (DARKEN_ENTITY_IN_PAUSED(entity))
        _darken_swap(entity->owner->pool, entity->slot, entity->owner->paused++);
}

#endif // DARKEN_IMPLEMENTATION
