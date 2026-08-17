/**
 * darken.h — Darken (DARKula ENgine) 2.0 Entity System
 *
 * Public functions/types: de_*
 * Public macros:          DE_*
 * Internal functions:     _de_*
 * Internal macros:        _DE_*
 *
 * Full documentation: README.md
 */

#ifndef DARKEN_H
#define DARKEN_H

#include <stdint.h>

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================ */

// State function pointer type
// Takes entity data pointer, returns next state or control code
typedef void *(*de_state)(void *);

// Forward declarations for type safety
typedef struct de_entity *de_entity;  // Entity pointer type
typedef struct de_manager de_manager; // Entity manager type
typedef struct de_system de_system;   // System type

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
 * Memory Layout:
 * [state][destructor][manager][slot][tag][user data...]
 *
 * The stride between entities is pre-calculated during manager initialization
 * to enable O(1) access to any entity by index.
 */
struct de_entity
{
    de_state state;      // Current state function (NULL = inactive)
    de_state destructor; // Optional cleanup function called on deletion
    de_manager *manager; // Owning manager reference
    uint16_t slot;       // Current index in manager's items array
    uint16_t tag;        // User-defined tag for entity identification
    uint8_t data[];      // Flexible array member for entity-specific data
};

/**
 * Manager: Entity container and lifecycle manager
 *
 * Pool layout:
 *   [0 ... size-1]                    → active entities
 *   [size ... pause_index-1]          → free slots (unused)
 *   [pause_index ... capacity-1]      → paused entities
 *
 * - size         : number of active entities
 * - pause_index  : first index of paused area
 * - capacity     : total capacity of the pool
 */
struct de_manager
{
    de_entity *items;     // Array of entity pointers (pre-allocated)
    uint16_t size;        // Number of active entities
    uint16_t capacity;    // Maximum number of entities
    uint16_t pause_index; // Start index of paused area
};

/**
 * System: Generic data pool for component storage
 *
 * Provides a flat array for storing component data. The pool is organized
 * in groups of 'params' elements, allowing systems to process multiple
 * related data items per entity.
 *
 * Pool Layout (params=3):
 * [e0.a][e0.b][e0.c][e1.a][e1.b][e1.c][e2.a][e2.b][e2.c]...
 */
struct de_system
{
    void **pool;       // Data storage array
    uint16_t size;     // Current size in groups (not individual elements)
    uint16_t capacity; // Maximum capacity in groups
    uint16_t params;   // Number of elements per group
};

/* ============================================================================
 * STATE CONSTANTS
 * ============================================================================ */

/**
 * Special state values used for entity control
 * Low values enable fast comparison on 68K (cmp immediate)
 */
#define DE_STATE_DELETE ((void *)0) // Delete entity after update
#define DE_STATE_LOOP ((void *)1)   // Continue with current state
#define DE_STATE_PAUSE ((void *)2)  // Pause entity after update

/**
 * State checking macros
 * Optimized for 68K comparison operations
 */
#define DE_STATE_IS_DELETED(S) ((S) == ((de_state)0)) // Check if delete requested
#define DE_STATE_IS_LOOP(S) ((S) == ((de_state)1))    // Check if loop requested
#define DE_STATE_IS_PAUSED(S) ((S) == ((de_state)2))  // Check if pause requested
#define DE_STATE_IS_ACTIVE(S) ((S) > ((de_state)2))   // Check if active state function

/* ============================================================================
 * ENTITY API
 * ============================================================================ */

/**
 * Entity management functions
 */
void *de_entity_exec(de_entity);      // Execute current state without updating it
void *de_entity_update(de_entity);    // Update entity (call state, handle transitions)
void de_entity_pause(de_entity);      // Move entity to paused section
void de_entity_resume(de_entity);     // Move entity to active section
void de_entity_delete(de_entity);     // Mark entity for deletion (calls destructor)
void de_entity_move_front(de_entity); // Move to front of active section (priority)
void de_entity_move_back(de_entity);  // Move to back of active section (defer)

/* ============================================================================
 * MANAGER CREATION MACROS
 * ============================================================================ */

/**
 * Structured storage declaration.
 * Use with DE_MANAGER_ARGS() when calling de_manager_init().
 *
 * Example:
 * DE_MANAGER_STORAGE(m_storage, 8, sizeof(struct MyComponent));
 * de_manager_init(&m, DE_MANAGER_ARGS(m_st));
 */
#define DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)                                         \
    struct                                                                                       \
    {                                                                                            \
        de_entity entities[(CAPACITY)];                                                          \
        uint8_t data[(CAPACITY) * DE_ENTITY_STRIDE((PAYLOAD_SIZE))] __attribute__((aligned(4))); \
        uint16_t capacity;                                                                       \
        uint16_t payload_size;                                                                   \
    } NAME = {                                                                                   \
        .capacity = (CAPACITY),                                                                  \
        .payload_size = (PAYLOAD_SIZE),                                                          \
    }

#define DE_MANAGER_ARGS(NAME) \
    (NAME).entities, (NAME).data, (NAME).capacity, (NAME).payload_size

/* ============================================================================
 * MANAGER API
 * ============================================================================ */

/**
 * Manager management functions
 *
 * de_manager_init: Initializes manager with pre-allocated storage
 * de_manager_new: Creates new entity (returns NULL if full)
 * de_manager_update: Updates all active entities
 * de_manager_reset: Deletes all entities
 */
void de_manager_init(de_manager *, de_entity *, void *, uint16_t, uint16_t);
de_entity de_manager_new(de_manager *);
void de_manager_update(de_manager *);
void de_manager_reset(de_manager *);

/* ============================================================================
 * SYSTEM API
 * ============================================================================ */

void de_system_init(de_system *, void **, uint16_t, uint16_t);
uint16_t de_system_remove(de_system *, void *);

/* ============================================================================
 * ITERATION MACROS
 * ============================================================================ */

/**
 * DE_MANAGER_ITERATE: Iterates over active entities only
 *
 * Within the CODE block:
 * - ENTITY: Available entity pointer
 * - INDEX: Current index in iteration
 *
 * Example:
 * DE_MANAGER_ITERATE(my_manager, {
 *     if (ENTITY->tag == PLAYER_TAG) {
 *         update_player(ENTITY);
 *     }
 * });
 */

/**
 * Internal iteration over active entities
 * Iterates backwards for safe deletion during iteration
 */
#define DE_MANAGER_ITERATE(MANAGER, CODE)               \
    do                                                  \
    {                                                   \
        uint16_t INDEX = (MANAGER)->size;               \
        while (INDEX-- > 0)                             \
        {                                               \
            de_entity ENTITY = (MANAGER)->items[INDEX]; \
            CODE;                                       \
        }                                               \
    } while (0)

/* ============================================================================
 * APPLICATION MACROS (with target collection)
 * ============================================================================ */

/**
 * DE_MANAGER_APPLY: Collect entities matching filter, then apply action
 *
 * Parameters:
 * - M: Manager
 * - F: Filter condition (evaluated per entity)
 * - A: Action to apply to collected entities
 *
 * Useful for operations that might invalidate iteration (like batch deletion)
 *
 * Example:
 * DE_MANAGER_APPLY(my_manager, ENTITY->health <= 0, delete_entity(ENTITY));
 */
#define DE_MANAGER_APPLY(MANAGER, FILTER, ACTION) \
    _DE_MANAGER_APPLY(DE_MANAGER_ITERATE, MANAGER, FILTER, ACTION)

/**
 * Internal application implementation
 * Collects matching entities first, then applies action
 */
#define _DE_MANAGER_APPLY(ITER, MANAGER, FILTER, ACTION) \
    do                                                   \
    {                                                    \
        de_entity _targets[(MANAGER)->size + 1];         \
        uint16_t _count = 0;                             \
        ITER(MANAGER, {                                  \
            if (FILTER)                                  \
                _targets[_count++] = ENTITY;             \
        });                                              \
        while (_count--)                                 \
            ACTION(_targets[_count]);                    \
    } while (0)

/* ============================================================================
 * SYSTEM MACROS
 * ============================================================================ */

/**
 * DE_SYSTEM_STORAGE: Declaracion estatica de storage para sistema.
 * Uso con DE_SYSTEM_ARGS() al llamar de_system_init().
 *
 * Ejemplo:
 *   de_system sys;
 *   DE_SYSTEM_STORAGE(sys_storage, 32, 3);
 *   de_system_init(&sys, DE_SYSTEM_ARGS(sys_storage));
 */
#define DE_SYSTEM_STORAGE(NAME, CAPACITY, PARAMS) \
    struct                                        \
    {                                             \
        void *pool[(CAPACITY) * (PARAMS)];        \
        uint16_t capacity;                        \
        uint16_t params;                          \
    } NAME = {                                    \
        .capacity = (CAPACITY),                   \
        .params = (PARAMS),                       \
    }

#define DE_SYSTEM_ARGS(NAME) \
    (NAME).pool, (NAME).capacity, (NAME).params

/**
 * DE_SYSTEM_ADD: Adds a group of parameters to the system
 * Supports 1-5 parameters (variable arguments)
 * Returns 1 on success, 0 if system is full
 *
 * Example:
 * DE_SYSTEM_ADD(physics_system, entity_ptr, &velocity, &position);
 */
#define DE_SYSTEM_ADD(...)                                           \
    _DE_GET_MACRO(__VA_ARGS__, _,                                    \
                  _DE_SYSTEM_ADD5, _DE_SYSTEM_ADD4, _DE_SYSTEM_ADD3, \
                  _DE_SYSTEM_ADD2, _DE_SYSTEM_ADD1, unused)(__VA_ARGS__)

/**
 * DE_SYSTEM_FOREACH: Iterates over system data
 * Supports 0-5 variables per iteration
 *
 * Example:
 * DE_SYSTEM_FOREACH(physics_system, entity, velocity, position, {
 *     update_physics(entity, velocity, position);
 * });
 */
#define DE_SYSTEM_FOREACH(...)                                                      \
    _DE_GET_MACRO(__VA_ARGS__,                                                      \
                  _DE_SYSTEM_FOREACH_5, _DE_SYSTEM_FOREACH_4, _DE_SYSTEM_FOREACH_3, \
                  _DE_SYSTEM_FOREACH_2, _DE_SYSTEM_FOREACH_1, _DE_SYSTEM_FOREACH_0)(__VA_ARGS__)

/**
 * DE_SYSTEM_ITERATOR: Creates an iterator function from a foreach pattern
 * The generated function returns DE_STATE_LOOP for use as entity state
 *
 * Example:
 * DE_SYSTEM_ITERATOR(physics_update, entity, velocity, position, {
 *     entity->x += velocity->dx;
 *     entity->y += velocity->dy;
 * });
 * // Now physics_update can be used as an entity state function
 */
#define DE_SYSTEM_ITERATOR(...)                                                        \
    _DE_GET_MACRO(__VA_ARGS__,                                                         \
                  _DE_SYSTEM_ITERATOR_5, _DE_SYSTEM_ITERATOR_4, _DE_SYSTEM_ITERATOR_3, \
                  _DE_SYSTEM_ITERATOR_2, _DE_SYSTEM_ITERATOR_1, _DE_SYSTEM_ITERATOR_0)(__VA_ARGS__)

/* ============================================================================
 * SYSTEM MACRO IMPLEMENTATIONS
 * ============================================================================ */

/**
 * Internal system add implementation
 */
#define _DE_SYSTEM_ADD(SYS, N, ...)          \
    ({                                       \
        de_system *_s = (SYS);               \
        uint16_t _ok = 0;                    \
        if (_s->size + (N) <= _s->capacity)  \
        {                                    \
            void **_p = &_s->pool[_s->size]; \
            __VA_ARGS__                      \
            _s->size += (N);                 \
            _ok = 1;                         \
        }                                    \
        _ok;                                 \
    })

/* Variadic add implementations for 1-5 parameters */
#define _DE_SYSTEM_ADD1(SYSTEM, A) \
    _DE_SYSTEM_ADD(SYSTEM, 1, _p[0] = (void *)(A);)

#define _DE_SYSTEM_ADD2(SYSTEM, A, B) \
    _DE_SYSTEM_ADD(SYSTEM, 2, _p[0] = (void *)(A); _p[1] = (void *)(B);)

#define _DE_SYSTEM_ADD3(SYSTEM, A, B, C) \
    _DE_SYSTEM_ADD(SYSTEM, 3, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C);)

#define _DE_SYSTEM_ADD4(SYSTEM, A, B, C, D) \
    _DE_SYSTEM_ADD(SYSTEM, 4, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C); _p[3] = (void *)(D);)

#define _DE_SYSTEM_ADD5(SYSTEM, A, B, C, D, E) \
    _DE_SYSTEM_ADD(SYSTEM, 5, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C); _p[3] = (void *)(D); _p[4] = (void *)(E);)

/**
 * Internal iterator generator
 */
#define _DE_SYSTEM_ITERATOR(NAME, FOREACH, ...) \
    void *NAME(de_system *system)               \
    {                                           \
        FOREACH(system, __VA_ARGS__);           \
        return (void *)DE_STATE_LOOP;           \
    }

/* Iterator variants for 0-5 variables */
#define _DE_SYSTEM_ITERATOR_0(NAME, IT) \
    _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_0, IT)

#define _DE_SYSTEM_ITERATOR_1(NAME, A, IT) \
    _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_1, A, IT)

#define _DE_SYSTEM_ITERATOR_2(NAME, A, B, IT) \
    _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_2, A, B, IT)

#define _DE_SYSTEM_ITERATOR_3(NAME, A, B, C, IT) \
    _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_3, A, B, C, IT)

#define _DE_SYSTEM_ITERATOR_4(NAME, A, B, C, D, IT) \
    _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_4, A, B, C, D, IT)

#define _DE_SYSTEM_ITERATOR_5(NAME, A, B, C, D, E, IT) \
    _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_5, A, B, C, D, E, IT)

/**
 * Internal foreach implementation
 * Optimized for sequential access
 */
#define _DE_SYSTEM_FOREACH(SYSTEM, IT)                     \
    void **items = (SYSTEM)->pool;                         \
    for (uint16_t i = 0, size = (SYSTEM)->size; i < size;) \
        IT;

/* Foreach variants for 0-5 variables */
#define _DE_SYSTEM_FOREACH_0(SYSTEM, IT) \
    _DE_SYSTEM_FOREACH(SYSTEM, { IT; })

#define _DE_SYSTEM_FOREACH_1(SYSTEM, A, IT) \
    _DE_SYSTEM_FOREACH(SYSTEM, { A = items[i++]; IT; })

#define _DE_SYSTEM_FOREACH_2(SYSTEM, A, B, IT) \
    _DE_SYSTEM_FOREACH(SYSTEM, { A = items[i++]; B = items[i++]; IT; })

#define _DE_SYSTEM_FOREACH_3(SYSTEM, A, B, C, IT) \
    _DE_SYSTEM_FOREACH(SYSTEM, { A = items[i++]; B = items[i++]; C = items[i++]; IT; })

#define _DE_SYSTEM_FOREACH_4(SYSTEM, A, B, C, D, IT) \
    _DE_SYSTEM_FOREACH(SYSTEM, { A = items[i++]; B = items[i++]; C = items[i++]; D = items[i++]; IT; })

#define _DE_SYSTEM_FOREACH_5(SYSTEM, A, B, C, D, E, IT) \
    _DE_SYSTEM_FOREACH(SYSTEM, { A = items[i++]; B = items[i++]; C = items[i++]; D = items[i++]; E = items[i++]; IT; })

/* ============================================================================
 * INTERNAL UTILITIES
 * ============================================================================ */

/**
 * Entity stride calculation
 * Ensures proper alignment between consecutive entities
 */
#define DE_ENTITY_STRIDE(PAYLOAD) _DE_ALIGN4(sizeof(struct de_entity) + (PAYLOAD))

/**
 * 4-byte alignment macro
 * Critical for 68K performance (bus alignment)
 */
#define _DE_ALIGN4(X) (((X) + 3U) & ~3U)

/**
 * Token concatenation for unique identifier generation
 */
#define _DE_CONCAT(A, B) A##B
#define _DE_UNIQUE(A, B) _DE_CONCAT(A, B)

/**
 * Macro argument count selector
 * Supports 1-7 arguments for variadic macro dispatch
 */
#define _DE_GET_MACRO(_1, _2, _3, _4, _5, _6, _7, NAME, ...) NAME

#endif /* DARKEN_H */

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DARKEN_IMPLEMENTATION

/**
 * Executes the entity's current state without updating it.
 * Useful for initialization or manual execution scenarios.
 */
void *de_entity_exec(de_entity $)
{
    de_state s = $->state;

    if (!DE_STATE_IS_ACTIVE(s))
        return 0;

    return s($->data);
}

/**
 * Updates a single entity.
 * Calls the state function and handles state transitions.
 */
void *de_entity_update(de_entity $)
{
    de_state s = $->state;

    if (!DE_STATE_IS_ACTIVE(s))
        return 0;

    s = s($->data);

    // Only write back if state actually changed
    if (!DE_STATE_IS_LOOP(s))
        $->state = s;

    return s;
}

/**
 * Swaps two entities in the manager's array.
 */
static void _de_entity_swap(de_entity a, de_entity b)
{
    de_manager *m = a->manager;
    uint16_t i = a->slot;
    uint16_t j = b->slot;

    // Swap in array
    m->items[i] = b;
    b->slot = i;
    m->items[j] = a;
    a->slot = j;
}

/**
 * Pauses an entity by moving it to the paused section.
 * Requires at least one free slot to perform the move.
 */
void de_entity_pause(de_entity $)
{
    de_manager *m = $->manager;

    // Only active entities can be paused, and we need a free slot
    if ($->slot >= m->size || m->pause_index <= m->size)
        return;

    // 1. Remove from active area: swap with last active, decrement size
    _de_entity_swap($, m->items[m->size - 1]);
    m->size--;

    // 2. Insert into paused area: swap with last free, decrement pause_index
    _de_entity_swap($, m->items[m->pause_index - 1]);
    m->pause_index--;
}

/**
 * Resumes an entity by moving it to the active section.
 * Requires at least one free slot to perform the move.
 */
void de_entity_resume(de_entity $)
{
    de_manager *m = $->manager;

    // Only paused entities can be resumed, and we need a free slot
    if ($->slot < m->pause_index || m->pause_index >= m->capacity)
        return;

    // 1. Remove from paused area: swap with first paused, increment pause_index
    _de_entity_swap($, m->items[m->pause_index]);
    m->pause_index++;

    // 2. Insert into active area: swap with first free, increment size
    _de_entity_swap($, m->items[m->size]);
    m->size++;
}

/**
 * Deletes an entity.
 * Calls destructor if present, then reorganizes array.
 *
 * Algorithm:
 * 1. Early exit if entity is in free area or out of range
 * 2. Set state to delete
 * 3. Call destructor if present (can override deletion)
 * 4. If deletion confirmed, move entity to free area
 */
void de_entity_delete(de_entity $)
{
    de_manager *m = $->manager;

    // Entity must be assigned (active or paused), not in free area
    if ($->slot >= m->capacity || ($->slot >= m->size && $->slot < m->pause_index))
        return;

    $->state = DE_STATE_DELETE;

    // Call destructor if exists
    if ($->destructor)
        $->state = $->destructor($->data);

    // Only delete if destructor allows it
    if (!DE_STATE_IS_DELETED($->state))
        return;

    if ($->slot < m->size) // active
    {
        _de_entity_swap($, m->items[m->size - 1]);
        m->size--;
    }
    else // paused
    {
        _de_entity_swap($, m->items[m->pause_index]);
        m->pause_index++;
    }
    // Deleted entity now resides in free area; no further action needed
}

/**
 * Moves entity to front of active section.
 * Useful for prioritizing entity updates.
 */
void de_entity_move_front(de_entity $)
{
    de_manager *m = $->manager;

    if ($->slot < m->size && $->slot != m->size - 1)
        _de_entity_swap($, m->items[m->size - 1]);
}

/**
 * Moves entity to back of active section.
 * Useful for deferring entity updates.
 */
void de_entity_move_back(de_entity $)
{
    de_manager *m = $->manager;

    if ($->slot < m->size && $->slot != 0)
        _de_entity_swap($, m->items[0]);
}

/**
 * Initializes an entity manager.
 *
 * Algorithm:
 * 1. Store basic manager data
 * 2. Calculate entity stride (aligned)
 * 3. Pre-calculate all entity pointers
 */
void de_manager_init(de_manager *$, de_entity *items, void *storage, uint16_t capacity, uint16_t bytes)
{
    $->items = items;
    $->capacity = capacity;
    $->size = 0;               // No active entities
    $->pause_index = capacity; // No paused entities; entire pool is free

    uint16_t stride = DE_ENTITY_STRIDE(bytes);

    // Pre-calculate pointers for fast access
    for (uint16_t i = 0; i < capacity; ++i)
        $->items[i] = (de_entity)((uint8_t *)storage + i * stride);
}

/**
 * Creates a new entity in the manager.
 * Returns NULL if manager is full (no free slots).
 */
de_entity de_manager_new(de_manager *$)
{
    // Need at least one free slot between size and pause_index
    if ($->pause_index <= $->size)
        return 0;

    de_entity e = $->items[$->size];
    e->manager = $;
    e->slot = $->size++;
    e->state = DE_STATE_DELETE;
    e->destructor = 0;
    e->tag = 0;

    return e;
}

/**
 * Updates all active entities.
 * Also processes pending pauses and deletions.
 *
 * Algorithm:
 * 1. Capture active count once
 * 2. Iterate backwards through active area
 * 3. Call state function for each active entity
 * 4. Handle state transitions (pause, delete, change)
 *
 * This is the most performance-critical function.
 */
void de_manager_update(de_manager *$)
{
    uint16_t pc = $->size; // capture active count once

    while (pc-- > 0)
    {
        de_entity e = $->items[pc];
        de_state s = e->state;

        if (DE_STATE_IS_ACTIVE(s))
        {
            s = s(e->data);
            if (!DE_STATE_IS_LOOP(s))
                e->state = s;
        }
        else if (DE_STATE_IS_PAUSED(s))
            de_entity_pause(e);
        else if (DE_STATE_IS_DELETED(s))
            de_entity_delete(e);
    }
}

/**
 * Deletes all entities in the manager.
 * Calls destructors for all active entities.
 */
void de_manager_reset(de_manager *$)
{
    // Destruct active entities
    for (uint16_t i = 0; i < $->size; ++i)
    {
        de_entity e = $->items[i];
        e->state = DE_STATE_DELETE;

        if (e->destructor)
            e->state = e->destructor(e->data);
    }

    $->size = 0;
    $->pause_index = $->capacity;
}

/**
 * Initializes a data system.
 * Prepares pool for sequential access.
 */
void de_system_init(de_system *$, void **storage, uint16_t capacity_groups, uint16_t params)
{
    $->pool = storage;
    $->size = 0;
    $->capacity = capacity_groups * params;
    $->params = params ?: 1;
}

/**
 * Removes a group from the system.
 * Returns 1 if removed, 0 if not found.
 *
 * Algorithm:
 * 1. Find group by first element
 * 2. Move last group to removed position
 * 3. Reduce size
 */
uint16_t de_system_remove(de_system *$, void *first)
{
    uint16_t params = $->params;

    for (uint16_t i = 0; i < $->size; i += params)
        if ($->pool[i] == first)
        {
            $->size -= params;

            // Compact if not last group
            if (i != $->size)
                for (uint16_t k = 0; k < params; ++k)
                    $->pool[i + k] = $->pool[$->size + k];

            return 1;
        }

    return 0;
}

#endif /* DARKEN_IMPLEMENTATION */
