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
 * Entity: Base object managed by the entity manager
 *
 * The entity structure serves as a container for user data with lifecycle  management. The flexible array member
 * 'data[]' allows entities to have variable-sized payloads while maintaining contiguous memory layout.
 *
 * The stride between entities is pre-calculated during manager initialization to enable O(1) access to any entity
 * by index.
 *
 * An entity's own memory address (this struct) never moves once allocated by darken_init(). What moves between
 * the manager's zones is only the *pointer* to it inside darken.pool[]. This is what makes it safe to keep a
 * raw pointer into entity->data even while the entity gets paused/resumed/reordered.
 *
 *
 *
 * Manager: Entity container and lifecycle manager. Maintains the pointer array in three logical zones:
 *
 * Array Layout:
 *    [ active entities ][   free slots    ][ paused entities ]
 *    0                 size               paused             capacity
 *
 * The entity objects themselves live in the caller-provided storage block; * manager->pool contains pointers to
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
 * Entity state/destructor callback type.
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
    // Pool
    darken_entity *pool;
    uint16_t capacity;
    uint16_t size;

    // Dedicated members
    uint16_t paused;
} darken;

struct darken_entity
{
    // Private
    darken *owner;
    uint16_t slot;

    // Public
    darken_state state;
    darken_state destructor;
    uint32_t tag; // available to the user
    uint16_t usr; // available to the user
    uint8_t data[];
};

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

#define DARKEN_DATA _DARKEN_DATA

// Darken control values
#define DARKEN_DELETE _DARKEN_DELETE
#define DARKEN_LOOP _DARKEN_LOOP
#define DARKEN_PAUSE _DARKEN_PAUSE

#define DARKEN_STATE_IS_DELETED _DARKEN_STATE_IS_DELETED
#define DARKEN_STATE_IS_LOOP _DARKEN_STATE_IS_LOOP
#define DARKEN_STATE_IS_PAUSED _DARKEN_STATE_IS_PAUSED
#define DARKEN_STATE_IS_ACTIVE _DARKEN_STATE_IS_ACTIVE

#define DARKEN_ENTITY_IN_ACTIVE _DARKEN_ENTITY_IN_ACTIVE
#define DARKEN_ENTITY_IN_PAUSED _DARKEN_ENTITY_IN_PAUSED
#define DARKEN_ENTITY_IN_USED _DARKEN_ENTITY_IN_USED
#define DARKEN_ENTITY_IN_FREE _DARKEN_ENTITY_IN_FREE

uint16_t darken_entity_run(darken_entity);
uint16_t darken_entity_update(darken_entity);
uint16_t darken_entity_pause(darken_entity);
uint16_t darken_entity_resume(darken_entity);
uint16_t darken_entity_delete(darken_entity);

#define DARKEN_STORAGE _DARKEN_STORAGE
#define DARKEN_ARGS _DARKEN_ARGS
#define DARKEN_FOREACH _DARKEN_FOREACH

void darken_init(darken *, darken_entity *, void *, uint16_t, uint16_t);
darken_entity darken_spawn(darken *);
void darken_update(darken *);
void darken_reset(darken *);

/* ============================================================================
 * INTERNAL MACRO IMPLEMENTATIONS
 * ============================================================================ */

#define _DARKEN_BLOCK(CODE) \
    do                      \
    {                       \
        CODE                \
    } while (0)

#define _DARKEN_ASSERT(COND) _DARKEN_BLOCK(if (!(COND)) return 0;)

#define _DARKEN_DATA(TYPE, VAR, ENTITY) TYPE *VAR = (TYPE *)(ENTITY)->data;

#define _DARKEN_DELETE ((void *)0)
#define _DARKEN_LOOP ((void *)1)
#define _DARKEN_PAUSE ((void *)2)

#define _DARKEN_STATE_IS_DELETED(STATE) ((STATE) == (darken_state)0)
#define _DARKEN_STATE_IS_LOOP(STATE) ((STATE) == (darken_state)1)
#define _DARKEN_STATE_IS_PAUSED(STATE) ((STATE) == (darken_state)2)
#define _DARKEN_STATE_IS_ACTIVE(STATE) ((STATE) > (darken_state)2)

#define _DARKEN_ENTITY_IN_ACTIVE(ENTITY) ((ENTITY)->slot < (ENTITY)->owner->size)
#define _DARKEN_ENTITY_IN_PAUSED(ENTITY) ((ENTITY)->slot >= (ENTITY)->owner->paused)
#define _DARKEN_ENTITY_IN_USED(ENTITY) (_DARKEN_ENTITY_IN_ACTIVE(ENTITY) || _DARKEN_ENTITY_IN_PAUSED(ENTITY))
#define _DARKEN_ENTITY_IN_FREE(ENTITY) (!_DARKEN_ENTITY_IN_USED(ENTITY))

/**
 * Align a byte count to a 4-byte boundary.
 *
 * The Motorola 68000 requires word alignment for word/long accesses.
 * Longword alignment keeps entity strides predictable and efficient.
 */
#define _DARKEN_ALIGN4(X) (((X) + 3U) & ~3U)

// Ensures proper alignment between consecutive entities
#define _DARKEN_ENTITY_STRIDE(PAYLOAD) _DARKEN_ALIGN4(sizeof(struct darken_entity) + (PAYLOAD))

#define _DARKEN_ENTITY_UPDATE(ENTITY) _DARKEN_BLOCK( \
    void *result = ENTITY->state(ENTITY->data);      \
                                                     \
    if (!_DARKEN_STATE_IS_LOOP(result))              \
        ENTITY->state = result;)

#define _DARKEN_ENTITY_PAUSE(ENTITY) _DARKEN_BLOCK(                                \
    _darken_entity_swap(ENTITY->owner->pool, ENTITY->slot, --ENTITY->owner->size); \
    _darken_entity_swap(ENTITY->owner->pool, ENTITY->slot, --ENTITY->owner->paused);)

#define _DARKEN_ENTITY_DELETE(ENTITY) _DARKEN_BLOCK( \
    darken *manager = ENTITY->owner;                 \
                                                     \
    if (_DARKEN_STATE_IS_ACTIVE(ENTITY->destructor)) \
        ENTITY->destructor(ENTITY->data);            \
                                                     \
    _darken_entity_swap(manager->pool, ENTITY->slot, --manager->size);)

#define _DARKEN_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)                                                 \
    struct                                                                                            \
    {                                                                                                 \
        darken_entity pool[(CAPACITY)];                                                               \
        uint8_t data[(CAPACITY) * _DARKEN_ENTITY_STRIDE((PAYLOAD_SIZE))] __attribute__((aligned(4))); \
        uint16_t capacity;                                                                            \
        uint16_t payload_size;                                                                        \
    } NAME = {                                                                                        \
        .capacity = (CAPACITY),                                                                       \
        .payload_size = (PAYLOAD_SIZE),                                                               \
    }

#define _DARKEN_ARGS(NAME) \
    (NAME).pool, (NAME).data, (NAME).capacity, (NAME).payload_size

#define _DARKEN_FOREACH(MANAGER, CODE) _DARKEN_BLOCK(               \
    uint16_t INDEX = (MANAGER)->size;                               \
    darken_entity *POOL = (MANAGER)->pool;                          \
                                                                    \
    while (INDEX--) {                                               \
        darken_entity ENTITY __attribute__((unused)) = POOL[INDEX]; \
        CODE;                                                       \
    })

#endif // DARKEN_H

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DARKEN_IMPLEMENTATION

static inline uint16_t _darken_entity_swap(darken_entity *pool, uint16_t i, uint16_t j)
{
    _DARKEN_ASSERT(i != j);

    darken_entity tmp = pool[i];
    pool[i] = pool[j];
    pool[j] = tmp;
    pool[i]->slot = i;
    pool[j]->slot = j;

    return 1;
}

//

void darken_init(darken *$, darken_entity *pool, void *param_storage, uint16_t capacity, uint16_t bytes)
{
    $->pool = pool;
    $->capacity = capacity;
    $->size = 0;
    $->paused = capacity;

    uint16_t stride = _DARKEN_ENTITY_STRIDE(bytes);
    uint8_t *storage = param_storage;

    for (uint16_t i = 0; i < capacity; ++i)
    {
        darken_entity entity = (darken_entity)storage;

        pool[i] = entity;
        entity->owner = $;
        entity->slot = i;

        storage += stride;
    }
}

darken_entity darken_spawn(darken *$)
{
    _DARKEN_ASSERT($->size < $->paused);

    return $->pool[$->size++];
}

void darken_update(darken *$)
{
    uint16_t i = $->size;
    darken_entity *pool = $->pool;

    while (i--)
    {
        darken_entity entity = pool[i];
        darken_state state = entity->state;

        if (_DARKEN_STATE_IS_ACTIVE(state))
            _DARKEN_ENTITY_UPDATE(entity);

        else if (_DARKEN_STATE_IS_PAUSED(state))
            _DARKEN_ENTITY_PAUSE(entity);

        else if (_DARKEN_STATE_IS_DELETED(state))
            _DARKEN_ENTITY_DELETE(entity);
    }
}

void darken_reset(darken *$)
{
    DARKEN_FOREACH($, _DARKEN_ENTITY_DELETE(ENTITY));

    $->size = 0;
    $->paused = $->capacity;
}

uint16_t darken_entity_run(darken_entity $)
{
    _DARKEN_ASSERT(_DARKEN_STATE_IS_ACTIVE($->state));
    $->state($->data);

    return 1;
}

uint16_t darken_entity_update(darken_entity $)
{
    _DARKEN_ASSERT(_DARKEN_STATE_IS_ACTIVE($->state));
    _DARKEN_ENTITY_UPDATE($);

    return 1;
}

uint16_t darken_entity_pause(darken_entity $)
{
    _DARKEN_ASSERT(_DARKEN_ENTITY_IN_ACTIVE($));
    _DARKEN_ENTITY_PAUSE($);

    return 1;
}

uint16_t darken_entity_resume(darken_entity $)
{
    _DARKEN_ASSERT(_DARKEN_ENTITY_IN_PAUSED($));

    darken *manager = $->owner;

    _darken_entity_swap(manager->pool, $->slot, manager->paused);
    _darken_entity_swap(manager->pool, $->slot, manager->size);

    ++manager->paused;
    ++manager->size;

    return 1;
}

uint16_t darken_entity_delete(darken_entity $)
{
    _DARKEN_ASSERT(_DARKEN_ENTITY_IN_USED($));

    if (_DARKEN_ENTITY_IN_ACTIVE($))
        _DARKEN_ENTITY_DELETE($);

    else
        _darken_entity_swap($->owner->pool, $->slot, $->owner->paused++);

    return 1;
}

#endif // DARKEN_IMPLEMENTATION
