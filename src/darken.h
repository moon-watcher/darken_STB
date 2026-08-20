/**
 * darken.h — Darken (DARKula ENgine) 2.0 Entity System
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
 * Manager: Entity container and lifecycle manager. Maintains the pointer array in three logical zones:
 *
 * Array Layout:
 *    [ active entities ][   free slots    ][ paused entities ]
 *    |                 |                  |                  |
 *    0                 size               paused             capacity
 *
 * The entity objects themselves live in the caller-provided storage block; * manager->pool.entities contains pointers to
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
    struct
    {
        de_entity *entities;
        uint16_t capacity;
        uint16_t size;
    } pool;

    uint16_t paused;
};

#define DE_STATE_DELETE ((void *)0)
#define DE_STATE_LOOP ((void *)1)
#define DE_STATE_PAUSE ((void *)2)

#define DE_STATE_IS_DELETED(STATE) ((STATE) == ((de_state)0))
#define DE_STATE_IS_LOOP(STATE) ((STATE) == ((de_state)1))
#define DE_STATE_IS_PAUSED(STATE) ((STATE) == ((de_state)2))
#define DE_STATE_IS_ACTIVE(STATE) ((STATE) > ((de_state)2))

#define DE_ENTITY_IS_ACTIVE(ENTITY) ((ENTITY)->slot < (ENTITY)->owner->pool.size)
#define DE_ENTITY_IS_PAUSED(ENTITY) ((ENTITY)->slot >= (ENTITY)->owner->paused)
#define DE_ENTITY_IS_FREE(ENTITY) (!DE_ENTITY_IS_ACTIVE(ENTITY) && !DE_ENTITY_IS_PAUSED(ENTITY))

/* Function prototypes */
void de_entity_exec(de_entity);
void de_entity_update(de_entity);
void de_entity_pause(de_entity);
void de_entity_resume(de_entity);
void de_entity_delete(de_entity);
void de_entity_move_front(de_entity);
void de_entity_move_back(de_entity);

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

#define _DE_ALIGN4(X) (((X) + 3U) & ~3U)
#define _DE_ENTITY_STRIDE(PAYLOAD) _DE_ALIGN4(sizeof(struct de_entity) + (PAYLOAD))

#define _DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)            \
    struct                                                           \
    {                                                                \
        de_entity pool[(CAPACITY)];                                  \
        uint8_t data[(CAPACITY) * _DE_ENTITY_STRIDE((PAYLOAD_SIZE))] \
            __attribute__((aligned(4)));                             \
        uint16_t capacity;                                           \
        uint16_t payload_size;                                       \
    } NAME = {                                                       \
        .capacity = (CAPACITY),                                      \
        .payload_size = (PAYLOAD_SIZE),                              \
    }

#define _DE_MANAGER_ARGS(NAME) \
    (NAME).pool, (NAME).data, (NAME).capacity, (NAME).payload_size

#define _DE_MANAGER_FOREACH(MANAGER, CODE)          \
    do                                              \
    {                                               \
        uint16_t INDEX = (MANAGER)->pool.size;      \
        de_entity *POOL = (MANAGER)->pool.entities; \
        while (INDEX--)                             \
        {                                           \
            de_entity ENTITY = POOL[INDEX];         \
            CODE;                                   \
        }                                           \
    } while (0)

#define _DE_ASSERT(COND, RETVAL) \
    do                           \
    {                            \
        if (!(COND))             \
            return RETVAL;       \
    } while (0)

#endif /* DARKEN_H */

#ifdef DARKEN_IMPLEMENTATION

/* ============================================================================
 * Generic slot swap – replaces old _de_swap
 * ============================================================================ */
static inline void _de_swap_slots(de_manager m, uint16_t i, uint16_t j)
{
    if (i == j)
        return;
    de_entity *pool = m->pool.entities;
    de_entity tmp = pool[i];
    pool[i] = pool[j];
    pool[j] = tmp;

    if (pool[i])
        pool[i]->slot = i;

    if (pool[j])
        pool[j]->slot = j;
}

/* ============================================================================
 * Entity operations
 * ============================================================================ */

inline void de_entity_exec(de_entity $)
{
    _DE_ASSERT(DE_ENTITY_IS_ACTIVE($), );
    _DE_ASSERT(DE_STATE_IS_ACTIVE($->state), );

    $->state($->data);
}

inline void de_entity_update(de_entity $)
{
    _DE_ASSERT(DE_ENTITY_IS_ACTIVE($), );
    _DE_ASSERT(DE_STATE_IS_ACTIVE($->state), );

    void *result = $->state($->data);

    if (!DE_STATE_IS_LOOP(result))
        $->state = result;
}

inline void de_entity_pause(de_entity $)
{
    _DE_ASSERT(DE_ENTITY_IS_ACTIVE($), );

    de_manager m = $->owner;
    uint16_t size = --m->pool.size; // nueva última activa
    uint16_t paused = --m->paused;  // nueva primera pausada

    _de_swap_slots(m, $->slot, size);
    _de_swap_slots(m, $->slot, paused); // ahora $ está en size, se mueve a paused
}

inline void de_entity_resume(de_entity $)
{
    _DE_ASSERT(DE_ENTITY_IS_PAUSED($), );

    de_manager m = $->owner;
    uint16_t size = m->pool.size;
    uint16_t paused = m->paused;

    _de_swap_slots(m, $->slot, paused);
    _de_swap_slots(m, $->slot, size);

    ++m->pool.size;
    ++m->paused;
}

inline void de_entity_delete(de_entity $)
{
    _DE_ASSERT(DE_ENTITY_IS_ACTIVE($), );
    de_manager m = $->owner;
    if (DE_STATE_IS_ACTIVE($->destructor))
        $->destructor($->data);

    uint16_t size = --m->pool.size;
    _de_swap_slots(m, $->slot, size);
}

inline void de_entity_move_front(de_entity $)
{
    _DE_ASSERT(DE_ENTITY_IS_ACTIVE($), );

    _de_swap_slots($->owner, $->slot, $->owner->pool.size - 1);
}

inline void de_entity_move_back(de_entity $)
{
    _DE_ASSERT(DE_ENTITY_IS_ACTIVE($), );
    de_manager m = $->owner;
    _de_swap_slots(m, $->slot, 0);
}

/* ============================================================================
 * Manager lifecycle
 * ============================================================================ */

inline void de_manager_init(de_manager $, de_entity *pool, void *param_storage,
                            uint16_t capacity, uint16_t bytes)
{
    $->pool.entities = pool;
    $->pool.capacity = capacity;
    $->pool.size = 0;
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

inline de_entity de_manager_new(de_manager $)
{
    if ($->pool.size >= $->paused)
        return 0;

    de_entity entity = $->pool.entities[$->pool.size];
    entity->state = DE_STATE_DELETE;
    entity->destructor = 0;
    entity->owner = $;
    entity->slot = $->pool.size++;
    entity->tag = 0;

    return entity;
}

inline void de_manager_update(de_manager $)
{
    uint16_t i = $->pool.size;
    de_entity *pool = $->pool.entities;

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

inline void de_manager_reset(de_manager $)
{
    DE_MANAGER_FOREACH($, de_entity_delete(ENTITY));
    $->pool.size = 0;
    $->paused = $->pool.capacity;
}

#endif /* DARKEN_IMPLEMENTATION */
