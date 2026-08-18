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
 */

#ifndef DARKEN_H
#define DARKEN_H

#include <stdint.h>

/**
 * Entity state/destructor callback type.
 *
 * Receives entity->data and returns either another state callback or one of
 * the DE_STATE_* control values.
 */
typedef void *(*de_state)(void *);

typedef struct de_entity *de_entity;
typedef struct de_manager *de_manager;
typedef struct de_system de_system;

/* ============================================================================
 * DATA STRUCTURES
 * ============================================================================ */

/**
 * Entity: Base object managed by the entity manager
 *
 * The entity structure serves as a container for user data with lifecycle
 * management. The flexible array member 'data[]' allows entities to have
 * variable-sized payloads while maintaining contiguous memory layout.
 *
 * The stride between entities is pre-calculated during manager initialization
 * to enable O(1) access to any entity by index.
 *
 * An entity's own memory address (this struct) never moves once allocated
 * by de_manager_init(). What moves between the manager's zones is only the
 * *pointer* to it inside de_manager.pool[]. This is what makes it safe for
 * a de_system (or any external code) to keep a raw pointer into entity->data
 * even while the entity gets paused/resumed/reordered.
 */
struct de_entity
{
    de_state state;
    de_state destructor;
    de_manager manager;
    uint16_t slot;
    uint16_t tag;
    uint8_t data[];
};

/**
 * Manager: Entity container and lifecycle manager.
 *
 * Maintains the pointer array in three logical zones:
 *
 * Array Layout:
 *    [ active entities ][   free slots   ][ paused entities ]
 * 0             active_count      paused_start        capacity
 *
 * The entity objects themselves live in the caller-provided storage block;
 * manager->pool contains pointers to those fixed addresses.
 *
 * - Active zone [0, active_count):
 *     Entity pointers updated every frame by de_manager_update(). Iterable with
 *     DE_MANAGER_FOREACH. Freely created (de_manager_new) and deleted.
 *
 * - Free zone [active_count, paused_start):
 *     Pointer slots not currently assigned to an entity. This is where
 *     de_manager_new() takes its next entity from, and where an active entity's
 *     slot goes right after it's deleted.
 *
 * - Paused zone [paused_start, capacity):
 *     Entity pointers parked out of the update loop. de_manager_update() never
 *     touches them and DE_MANAGER_FOREACH never visits them. Crucially,
 *     de_manager_new() never hands out a slot from this zone, so a paused
 *     entity's slot (and therefore its entity->data pointer) stays valid
 *     and untouched until it's explicitly resumed or deleted. This is what
 *     lets a de_system keep safely pointing at a paused entity's data.
 */
struct de_manager
{
    de_entity *pool;
    uint16_t capacity;
    uint16_t active_count;
    uint16_t paused_start;
};

/**
 * System: Flat packed pool of data pointers.
 *
 * The pool is organized in groups of 'params' pointers. Each group can hold
 * the pointers associated with one processed item/entity.
 *
 * Pool Layout (params=2):
 *    [e0.a][e0.b][e1.a][e1.b][e2.a][e2.b]...
 */
struct de_system
{
    void **pool;
    void **end;
    uint16_t capacity;
    uint16_t size;
    uint16_t params;
};

/* ============================================================================
 * STATE CONSTANTS
 * ============================================================================ */

// Special state values used for entity control
#define DE_STATE_DELETE ((void *)0)
#define DE_STATE_LOOP ((void *)1)
#define DE_STATE_PAUSE ((void *)2)

// State checking macros
#define DE_STATE_IS_DELETED(S) ((S) == ((de_state)0))
#define DE_STATE_IS_LOOP(S) ((S) == ((de_state)1))
#define DE_STATE_IS_PAUSED(S) ((S) == ((de_state)2))
#define DE_STATE_IS_ACTIVE(S) ((S) > ((de_state)2))

/* ============================================================================
 * ENTITY API
 * ============================================================================ */

void *de_entity_exec(de_entity);
void *de_entity_update(de_entity);
void de_entity_pause(de_entity);
void de_entity_resume(de_entity);
void de_entity_delete(de_entity);
void de_entity_move_front(de_entity);
void de_entity_move_back(de_entity);

/* ============================================================================
 * MANAGER API
 * ============================================================================ */

#define DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE) _DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)
#define DE_MANAGER_ARGS(NAME) _DE_MANAGER_ARGS(NAME)
#define DE_MANAGER_FOREACH(M, CODE) _DE_MANAGER_FOREACH(M, CODE)

void de_manager_init(de_manager , de_entity *, void *, uint16_t, uint16_t);
de_entity de_manager_new(de_manager );
void de_manager_update(de_manager );
void de_manager_reset(de_manager );

/* ============================================================================
 * SYSTEM API
 * ============================================================================ */

#define DE_SYSTEM_STORAGE(NAME, CAPACITY, PARAMS) _DE_SYSTEM_STORAGE(NAME, CAPACITY, PARAMS)
#define DE_SYSTEM_ARGS(NAME) _DE_SYSTEM_ARGS(NAME)
#define DE_SYSTEM_ADD(...) _DE_CONCAT(_DE_SYSTEM_ADD_, _DE_ADD_NARGS(__VA_ARGS__))(__VA_ARGS__)
#define DE_SYSTEM_FOREACH(...) _DE_CONCAT(_DE_SYSTEM_FOREACH_, _DE_FOREACH_NARGS(__VA_ARGS__))(__VA_ARGS__)
#define DE_SYSTEM_ITERATOR(...) _DE_CONCAT(_DE_SYSTEM_ITERATOR_, _DE_FOREACH_NARGS(__VA_ARGS__))(__VA_ARGS__)

void de_system_init(de_system *, void **, uint16_t, uint16_t);
uint16_t de_system_remove(de_system *, void *);

/* ============================================================================
 * INTERNAL MACRO IMPLEMENTATIONS
 * ============================================================================ */

// Ensures proper alignment between consecutive entities
#define _DE_ENTITY_STRIDE(PAYLOAD) _DE_ALIGN4(sizeof(struct de_entity) + (PAYLOAD))

/**
 * Align a byte count to a 4-byte boundary.
 *
 * The Motorola 68000 requires word alignment for word/long accesses.
 * Longword alignment keeps entity strides predictable and efficient.
 */
#define _DE_ALIGN4(X) (((X) + 3U) & ~3U)
#define _DE_ADD_NARGS(...) _DE_ADD_NARGS_I(__VA_ARGS__, 5, 4, 3, 2, 1, 0)
#define _DE_ADD_NARGS_I(_1, _2, _3, _4, _5, _6, N, ...) N
#define _DE_FOREACH_NARGS(...) _DE_FOREACH_NARGS_I(__VA_ARGS__, 5, 4, 3, 2, 1, 0, -1)
#define _DE_FOREACH_NARGS_I(_1, _2, _3, _4, _5, _6, _7, N, ...) N
#define _DE_CONCAT_INNER(A, B) A##B
#define _DE_CONCAT(A, B) _DE_CONCAT_INNER(A, B)

#define _DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)                                        \
    struct                                                                                       \
    {                                                                                            \
        de_entity pool[(CAPACITY)];                                                              \
        uint8_t data[(CAPACITY) * _DE_ENTITY_STRIDE((PAYLOAD_SIZE))] __attribute__((aligned(4))); \
        uint16_t capacity;                                                                       \
        uint16_t payload_size;                                                                   \
    } NAME = {                                                                                   \
        .capacity = (CAPACITY),                                                                  \
        .payload_size = (PAYLOAD_SIZE),                                                          \
    }

#define _DE_MANAGER_ARGS(NAME) \
    (NAME).pool, (NAME).data, (NAME).capacity, (NAME).payload_size

#define _DE_MANAGER_FOREACH(M, CODE)        \
    do                                      \
    {                                       \
        uint16_t INDEX = (M)->active_count; \
        de_entity *POOL = (M)->pool;        \
        while (INDEX--)                     \
        {                                   \
            de_entity ENTITY = POOL[INDEX]; \
            CODE;                           \
        }                                   \
    } while (0)

#define _DE_SYSTEM_STORAGE(NAME, CAPACITY, PARAMS) \
    struct                                         \
    {                                              \
        void *pool[(CAPACITY) * (PARAMS)];         \
        uint16_t capacity;                         \
        uint16_t params;                           \
    } NAME = {                                     \
        .capacity = (CAPACITY),                    \
        .params = (PARAMS),                        \
    }

#define _DE_SYSTEM_ARGS(NAME) \
    (NAME).pool, (NAME).capacity, (NAME).params

#define _DE_SYSTEM_ADD(SYS, N, ...)         \
    ({                                      \
        de_system *_s = (SYS);              \
        uint16_t _ok = 0;                   \
        if (_s->size + (N) <= _s->capacity) \
        {                                   \
            void **_p = _s->end;            \
            __VA_ARGS__                     \
            _s->size += (N);                \
            _s->end += (N);                 \
            _ok = 1;                        \
        }                                   \
        _ok;                                \
    })

#define _DE_SYSTEM_ADD_1(SYS, A) _DE_SYSTEM_ADD(SYS, 1, _p[0] = (void *)(A);)
#define _DE_SYSTEM_ADD_2(SYS, A, B) _DE_SYSTEM_ADD(SYS, 2, _p[0] = (void *)(A); _p[1] = (void *)(B);)
#define _DE_SYSTEM_ADD_3(SYS, A, B, C) _DE_SYSTEM_ADD(SYS, 3, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C);)
#define _DE_SYSTEM_ADD_4(SYS, A, B, C, D) _DE_SYSTEM_ADD(SYS, 4, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C); _p[3] = (void *)(D);)
#define _DE_SYSTEM_ADD_5(SYS, A, B, C, D, E) _DE_SYSTEM_ADD(SYS, 5, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C); _p[3] = (void *)(D); _p[4] = (void *)(E);)

#define _DE_SYSTEM_FOREACH(SYSTEM, IT) \
    do                                 \
    {                                  \
        void **pool = (SYSTEM)->pool;  \
        void **end = (SYSTEM)->end;    \
        while (pool < end)             \
        {                              \
            IT;                        \
            pool += (SYSTEM)->params;  \
        }                              \
    } while (0)

#define _DE_SYSTEM_FOREACH_0(SYSTEM, IT) _DE_SYSTEM_FOREACH(SYSTEM, { IT; })
#define _DE_SYSTEM_FOREACH_1(SYSTEM, A, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = pool[0]; IT; })
#define _DE_SYSTEM_FOREACH_2(SYSTEM, A, B, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = pool[0]; B = pool[1]; IT; })
#define _DE_SYSTEM_FOREACH_3(SYSTEM, A, B, C, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = pool[0]; B = pool[1]; C = pool[2]; IT; })
#define _DE_SYSTEM_FOREACH_4(SYSTEM, A, B, C, D, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = pool[0]; B = pool[1]; C = pool[2]; D = pool[3]; IT; })
#define _DE_SYSTEM_FOREACH_5(SYSTEM, A, B, C, D, E, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = pool[0]; B = pool[1]; C = pool[2]; D = pool[3]; E = pool[4]; IT; })

#define _DE_SYSTEM_ITERATOR(NAME, FOREACH, ...) \
    void *NAME(de_system *system)               \
    {                                           \
        FOREACH(system, __VA_ARGS__);           \
        return DE_STATE_LOOP;                   \
    }

#define _DE_SYSTEM_ITERATOR_0(NAME, IT) _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_0, IT)
#define _DE_SYSTEM_ITERATOR_1(NAME, A, IT) _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_1, A, IT)
#define _DE_SYSTEM_ITERATOR_2(NAME, A, B, IT) _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_2, A, B, IT)
#define _DE_SYSTEM_ITERATOR_3(NAME, A, B, C, IT) _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_3, A, B, C, IT)
#define _DE_SYSTEM_ITERATOR_4(NAME, A, B, C, D, IT) _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_4, A, B, C, D, IT)
#define _DE_SYSTEM_ITERATOR_5(NAME, A, B, C, D, E, IT) _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_5, A, B, C, D, E, IT)

#endif // DARKEN_H

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DARKEN_IMPLEMENTATION

static void _de_entity_swap(de_entity a, de_entity b)
{
    de_manager manager = a->manager;
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

void de_entity_pause(de_entity $)
{
    de_manager manager = $->manager;
    uint16_t slot = $->slot;

    if (slot >= manager->active_count)
        return;

    // Shrink active zone from the right and fill the vacated slot
    --manager->active_count;

    if (slot != manager->active_count)
    {
        de_entity entity = manager->pool[manager->active_count];
        manager->pool[slot] = entity;
        entity->slot = slot;
    }

    // Grow paused zone from the left and place entity at the new boundary
    --manager->paused_start;
    manager->pool[manager->paused_start] = $;
    $->slot = manager->paused_start;
}

void de_entity_resume(de_entity $)
{
    de_manager manager = $->manager;
    uint16_t slot = $->slot;

    if (slot < manager->paused_start)
        return;

    // Shrink paused zone from the left and fill the vacated slot
    if (slot != manager->paused_start)
    {
        de_entity entity = manager->pool[manager->paused_start];
        manager->pool[slot] = entity;
        entity->slot = slot;
    }

    ++manager->paused_start;

    // Grow active zone from the right and place entity at the new boundary
    manager->pool[manager->active_count] = $;
    $->slot = manager->active_count;
    ++manager->active_count;
}

void de_entity_delete(de_entity $)
{
    de_manager manager = $->manager;
    uint16_t slot = $->slot;

    // Already free: nothing to do
    if (slot >= manager->active_count && slot < manager->paused_start)
        return;

    if ($->destructor)
        $->destructor($->data);

    if (slot >= manager->paused_start)
    {
        // Was paused: shrink paused zone from the left, slot rejoins the free zone
        if (slot != manager->paused_start)
            _de_entity_swap($, manager->pool[manager->paused_start]);

        ++manager->paused_start;
    }
    else
    {
        // Was active: shrink active zone from the right, slot rejoins the free zone
        if (slot != manager->active_count - 1)
            _de_entity_swap($, manager->pool[manager->active_count - 1]);

        --manager->active_count;
    }
}

void de_entity_move_front(de_entity $)
{
    de_manager manager = $->manager;
    uint16_t slot = $->slot;

    if (slot < manager->active_count && slot != manager->active_count - 1)
        _de_entity_swap($, manager->pool[manager->active_count - 1]);
}

void de_entity_move_back(de_entity $)
{
    de_manager manager = $->manager;
    uint16_t slot = $->slot;

    if (slot < manager->active_count && slot != 0)
        _de_entity_swap($, manager->pool[0]);
}

//

void de_manager_init(de_manager $, de_entity *pool, void *param_storage, uint16_t capacity, uint16_t bytes)
{
    $->pool = pool;
    $->capacity = capacity;
    $->active_count = 0;
    $->paused_start = capacity;

    uint16_t stride = _DE_ENTITY_STRIDE(bytes);
    uint8_t *storage = param_storage;

    for (uint16_t i = 0; i < capacity; ++i)
    {
        de_entity entity = (de_entity)storage;

        pool[i] = entity;
        entity->manager = $;
        entity->slot = i;

        storage += stride;
    }
}

de_entity de_manager_new(de_manager $)
{
    if ($->active_count >= $->paused_start)
        return 0;

    de_entity entity = $->pool[$->active_count];
    entity->manager = $;
    entity->slot = $->active_count++;
    entity->state = DE_STATE_DELETE;
    entity->destructor = 0;
    entity->tag = 0;

    return entity;
}

void de_manager_update(de_manager $)
{
    uint16_t i = $->active_count;
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

    $->active_count = 0;
    $->paused_start = $->capacity;
}

//

void de_system_init(de_system *$, void **storage, uint16_t capacity_groups, uint16_t params)
{
    $->pool = storage;
    $->end = storage;
    $->size = 0;
    $->capacity = capacity_groups * params;
    $->params = params;
}

uint16_t de_system_remove(de_system *$, void *first)
{
    uint16_t params = $->params;

    for (uint16_t i = 0; i < $->size; i += params)
        if ($->pool[i] == first)
        {
            $->size -= params;

            // Compact if not last group
            if (i != $->size)
                for (uint16_t j = 0; j < params; ++j)
                    $->pool[i + j] = $->pool[$->size + j];

            $->end -= params;

            return 1;
        }

    return 0;
}

#endif // DARKEN_IMPLEMENTATION
