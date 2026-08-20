/**
 * darken.h — Darken (DARKula ENgine) 2.0 Entity System
 *
 * Public functions/types: de_*
 * Public macros:          DE_*
 * Internal functions:     _de_*
 * Internal macros:        _DE_*
 *
 * Full documentation: README.md
 *
 * GNU C note:
 * - This header uses GNU C extensions (__attribute__ and statement expressions).
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
 * An entity's own memory address (this struct) never moves once allocated by de_manager_init(). What moves between
 * the manager's zones is only the *pointer* to it inside de_manager.pool[]. This is what makes it safe to keep a
 * raw pointer into entity->data even while the entity gets paused/resumed/reordered.
 *
 *
 *
 * Manager: Entity container and lifecycle manager.
 *
 * Maintains the pointer array in three logical zones:
 *
 * Array Layout:
 *    [ active entities ][   free slots   ][ paused entities ]
 * 0             size      paused        capacity
 *
 * The entity objects themselves live in the caller-provided storage block; * manager->pool contains pointers to
 * those fixed addresses.
 *
 * - Active zone [0, size):
 *     Entity pointers updated every frame by de_manager_update(). Iterable with DE_MANAGER_FOREACH. Freely created
 *     (de_manager_new) and deleted.
 *
 * - Free zone [size, paused):
 *     Pointer slots not currently assigned to an entity. This is where de_manager_new() takes its next entity from,
 *     and where an active entity's slot goes right after it's deleted.
 *
 * - Paused zone [paused, capacity):
 *     Entity pointers parked out of the update loop. de_manager_update() never touches them and DE_MANAGER_FOREACH
 *     never visits them. Crucially, de_manager_new() never hands out a slot from this zone, so a paused entity's
 *     slot (and therefore its entity->data pointer) stays valid and untouched until it's explicitly resumed or
 *     deleted. This is what lets keep safely pointing at a paused entity's data.
 *
 *
 *
 * Entity state/destructor callback type.
 *
 * Receives entity->data and returns either another state callback or one of the DE_STATE_* control values.
 */

#ifndef DARKEN_H
#define DARKEN_H

#include <stdint.h>

typedef void *(*de_state)(void *);

typedef struct de_entity *de_entity;
typedef struct de_manager *de_manager;

struct de_entity
{
    de_state state;
    de_state destructor;
    de_manager owner;
    uint16_t slot;
    uint16_t tag;
    uint8_t data[];
};

struct de_manager
{
    de_entity *pool;
    uint16_t capacity;
    uint16_t size;
    uint16_t paused;
};

#define DE_STATE_DELETE ((void *)0)
#define DE_STATE_LOOP ((void *)1)
#define DE_STATE_PAUSE ((void *)2)

#define DE_STATE_IS_DELETED(STATE) ((STATE) == ((de_state)0))
#define DE_STATE_IS_LOOP(STATE) ((STATE) == ((de_state)1))
#define DE_STATE_IS_PAUSED(STATE) ((STATE) == ((de_state)2))
#define DE_STATE_IS_ACTIVE(STATE) ((STATE) > ((de_state)2))

#define DE_ENTITY_IS_ACTIVE(ENTITY) ((ENTITY)->slot < (ENTITY)->owner->size)
#define DE_ENTITY_IS_PAUSED(ENTITY) ((ENTITY)->slot >= (ENTITY)->owner->paused)
#define DE_ENTITY_IS_FREE(ENTITY) (!DE_ENTITY_IS_ACTIVE(ENTITY) && !DE_ENTITY_IS_PAUSED(ENTITY))

void *de_entity_exec(de_entity);
void *de_entity_update(de_entity);
uint16_t de_entity_pause(de_entity);
uint16_t de_entity_resume(de_entity);
uint16_t de_entity_delete(de_entity);
uint16_t de_entity_move_front(de_entity);
uint16_t de_entity_move_back(de_entity);

#define DE_MANAGER_STORAGE _DE_MANAGER_STORAGE
#define DE_MANAGER_ARGS _DE_MANAGER_ARGS
#define DE_MANAGER_FOREACH _DE_MANAGER_FOREACH

void de_manager_init(de_manager, de_entity *, void *, uint16_t, uint16_t);
de_entity de_manager_new(de_manager);
void de_manager_update(de_manager);
void de_manager_reset(de_manager);

/* ============================================================================
 * INTERNAL MACRO IMPLEMENTATIONS
 * ============================================================================ */

/**
 * Align a byte count to a 4-byte boundary.
 *
 * The Motorola 68000 requires word alignment for word/long accesses.
 * Longword alignment keeps entity strides predictable and efficient.
 */
#define _DE_ALIGN4(X) (((X) + 3U) & ~3U)

// Ensures proper alignment between consecutive entities
#define _DE_ENTITY_STRIDE(PAYLOAD) _DE_ALIGN4(sizeof(struct de_entity) + (PAYLOAD))

#define _DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)                                         \
    struct                                                                                        \
    {                                                                                             \
        de_entity pool[(CAPACITY)];                                                               \
        uint8_t data[(CAPACITY) * _DE_ENTITY_STRIDE((PAYLOAD_SIZE))] __attribute__((aligned(4))); \
        uint16_t capacity;                                                                        \
        uint16_t payload_size;                                                                    \
    } NAME = {                                                                                    \
        .capacity = (CAPACITY),                                                                   \
        .payload_size = (PAYLOAD_SIZE),                                                           \
    }

#define _DE_MANAGER_ARGS(NAME) \
    (NAME).pool, (NAME).data, (NAME).capacity, (NAME).payload_size

#define _DE_MANAGER_FOREACH(MANAGER, CODE)  \
    do                                      \
    {                                       \
        uint16_t INDEX = (MANAGER)->size;   \
        de_entity *POOL = (MANAGER)->pool;  \
                                            \
        while (INDEX--)                     \
        {                                   \
            de_entity ENTITY = POOL[INDEX]; \
            CODE;                           \
        }                                   \
    } while (0)

#endif // DARKEN_H

#ifdef DARKEN_IMPLEMENTATION

static void _de_entity_swap(de_entity a, de_entity b)
{
    de_manager manager = a->owner;
    uint16_t i = a->slot;
    uint16_t j = b->slot;

    manager->pool[i] = b;
    b->slot = i;
    manager->pool[j] = a;
    a->slot = j;
}

void *de_entity_exec(de_entity $)
{
    de_state state = $->state;

    if (!DE_STATE_IS_ACTIVE(state))
        return 0;

    return state($->data);
}

void *de_entity_update(de_entity $)
{
    de_state state = de_entity_exec($);

    if (!DE_STATE_IS_LOOP(state))
        $->state = state;

    return state;
}

uint16_t de_entity_pause(de_entity $)
{
    if (!DE_ENTITY_IS_ACTIVE($))
        return 0;

    de_manager manager = $->owner;
    de_entity *pool = manager->pool;
    uint16_t slot = $->slot;
    uint16_t size = --manager->size;     // Shrink active zone from the right and fill the vacated slot
    uint16_t paused = --manager->paused; // Grow paused zone from the left and place entity at the new boundary

    if (slot != size)
    {
        de_entity entity = pool[size];
        pool[slot] = entity;
        entity->slot = slot;
    }

    pool[paused] = $;
    $->slot = paused;

    return 1;
}

uint16_t de_entity_resume(de_entity $)
{
    if (!DE_ENTITY_IS_PAUSED($))
        return 0;

    de_manager manager = $->owner;
    de_entity *pool = manager->pool;
    uint16_t slot = $->slot;
    uint16_t size = manager->size;
    uint16_t paused = manager->paused;

    // Shrink paused zone from the left and fill the vacated slot
    if (slot != paused)
    {
        de_entity entity = pool[paused];
        pool[slot] = entity;
        entity->slot = slot;
    }

    // Grow active zone from the right and place entity at the new boundary
    pool[size] = $;
    $->slot = size;

    ++manager->size;
    ++manager->paused;

    return 1;
}

uint16_t de_entity_delete(de_entity $)
{
    if (DE_ENTITY_IS_FREE($))
        return 0;

    if (DE_STATE_IS_ACTIVE($->destructor))
        $->destructor($->data);

    de_manager manager = $->owner;
    uint16_t slot = DE_ENTITY_IS_PAUSED($) ? manager->paused++ : --manager->size;

    if ($->slot != slot)
        _de_entity_swap($, manager->pool[slot]);

    return 1;
}

uint16_t de_entity_move_front(de_entity $)
{
    de_manager manager = $->owner;

    if (manager->size == 0)
        return 0;

    uint16_t size = manager->size - 1;

    if (DE_ENTITY_IS_ACTIVE($) && $->slot != size)
    {
        _de_entity_swap($, manager->pool[size]);
        return 1;
    }

    return 0;
}

uint16_t de_entity_move_back(de_entity $)
{
    if (DE_ENTITY_IS_ACTIVE($) && $->slot != 0)
    {
        _de_entity_swap($, $->owner->pool[0]);
        return 1;
    }

    return 0;
}

//

void de_manager_init(de_manager $, de_entity *pool, void *param_storage, uint16_t capacity, uint16_t bytes)
{
    $->pool = pool;
    $->capacity = capacity;
    $->size = 0;
    $->paused = capacity;

    uint16_t stride = _DE_ENTITY_STRIDE(bytes);
    uint8_t *storage = param_storage;

    for (uint16_t i = 0; i < capacity; ++i)
    {
        de_entity entity = (de_entity)storage;

        pool[i] = entity;
        entity->owner = $;
        entity->slot = i;

        storage += stride;
    }
}

de_entity de_manager_new(de_manager $)
{
    if ($->size >= $->paused)
        return 0;

    de_entity entity = $->pool[$->size];
    entity->owner = $;
    entity->slot = $->size++;
    entity->state = DE_STATE_DELETE;
    entity->destructor = 0;
    entity->tag = 0;

    return entity;
}

void de_manager_update(de_manager $)
{
    uint16_t i = $->size;
    de_entity *pool = $->pool;

    while (i--)
    {
        de_entity entity = pool[i];
        de_state state = entity->state;

        if (DE_STATE_IS_ACTIVE(state))
        {
            state = state(entity->data);

            if (!DE_STATE_IS_LOOP(state))
                entity->state = state;
        }

        else if (DE_STATE_IS_PAUSED(state))
            de_entity_pause(entity);

        else if (DE_STATE_IS_DELETED(state))
            de_entity_delete(entity);
    }
}

void de_manager_reset(de_manager $)
{
    DE_MANAGER_FOREACH($, de_entity_delete(ENTITY));

    $->size = 0;
    $->paused = $->capacity;
}

#endif // DARKEN_IMPLEMENTATION
