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
 * INTERNAL UTILITIES
 * ============================================================================ */

/**
 * Counts the number of data parameters for DE_SYSTEM_ADD.
 * The first argument (system pointer) is not counted.
 * Returns a value from 1 to 5.
 */
#define _DE_ADD_NARGS(...) _DE_ADD_NARGS_I(__VA_ARGS__, 5, 4, 3, 2, 1, 0)
#define _DE_ADD_NARGS_I(_1, _2, _3, _4, _5, _6, N, ...) N

/**
 * Counts the number of data variables for DE_SYSTEM_FOREACH / DE_SYSTEM_ITERATOR.
 * The first argument (system pointer) and the last argument (code block) are not counted.
 * Returns a value from 0 to 5.
 */
#define _DE_FOREACH_NARGS(...) _DE_FOREACH_NARGS_I(__VA_ARGS__, 5, 4, 3, 2, 1, 0, -1)
#define _DE_FOREACH_NARGS_I(_1, _2, _3, _4, _5, _6, _7, N, ...) N

/* Token concatenation */
#define _DE_CONCAT_INNER(A, B) A##B
#define _DE_CONCAT(A, B) _DE_CONCAT_INNER(A, B)

/**
 * 4-byte alignment macro
 * Critical for 68K performance (bus alignment).
 * Note: On platforms with larger alignment requirements,
 *       adjust this to use _Alignof(struct de_entity).
 */
#define _DE_ALIGN4(X) (((X) + 3U) & ~3U)

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================ */

// State function pointer type
// Takes entity data pointer, returns next state or control code.
// WARNING: Storing function pointers in void* is not strictly ISO C,
//          but works on most 68K compilers. Use with caution.
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
 *
 * IMPORTANT: an entity's own memory address (this struct) never moves once
 * allocated by de_manager_init(). What moves between the manager's zones is
 * only the *pointer* to it inside de_manager.items[]. This is what makes it
 * safe for a de_system (or any external code) to keep a raw pointer into
 * entity->data even while the entity gets paused/resumed/reordered.
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
 * Maintains entities in a contiguous array split into three zones:
 *
 * Array Layout:
 * [ active entities ][   free slots   ][ paused entities ]
 * 0             active_count      paused_start        capacity
 *
 * - Active zone [0, active_count):
 *     Entities updated every frame by de_manager_update(). Iterable with
 *     DE_MANAGER_FOREACH. Freely created (de_manager_new) and deleted.
 *
 * - Free zone [active_count, paused_start):
 *     Slots not currently in use by anyone. This is where de_manager_new()
 *     takes its next entity from, and where an active entity's slot goes
 *     right after it's deleted.
 *
 * - Paused zone [paused_start, capacity):
 *     Entities parked out of the update loop. de_manager_update() never
 *     touches them and DE_MANAGER_FOREACH never visits them. Crucially,
 *     de_manager_new() never hands out a slot from this zone, so a paused
 *     entity's slot (and therefore its entity->data pointer) stays valid
 *     and untouched until it's explicitly resumed or deleted. This is what
 *     lets a de_system keep safely pointing at a paused entity's data.
 *
 * All counters are 16-bit for optimal 68K performance.
 */
struct de_manager
{
    de_entity *items;      // Array of entity pointers (pre-allocated)
    uint16_t capacity;     // Maximum number of entities
    uint16_t active_count; // Active zone boundary: active = [0, active_count)
    uint16_t paused_start; // Paused zone start: paused = [paused_start, capacity)
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
    void **end;        // One past the last element in use
    uint16_t capacity; // Maximum capacity in groups (multiplied by params)
    uint16_t size;     // Current size in groups (not individual elements)
    uint16_t params;   // Number of elements per group
};

/* ============================================================================
 * STATE CONSTANTS
 * ============================================================================ */

/**
 * Special state values used for entity control
 */
#define DE_STATE_DELETE ((void *)0) // Delete entity after update
#define DE_STATE_LOOP ((void *)1)   // Continue with current state
#define DE_STATE_PAUSE ((void *)2)  // Pause entity after update

/**
 * State checking macros
 */
#define DE_STATE_IS_DELETED(S) ((S) == ((de_state)0)) // Check if delete requested
#define DE_STATE_IS_LOOP(S) ((S) == ((de_state)1))    // Check if loop requested
#define DE_STATE_IS_PAUSED(S) ((S) == ((de_state)2))  // Check if pause requested
#define DE_STATE_IS_ACTIVE(S) ((S) > ((de_state)2))   // Check if active state function

/* ============================================================================
 * ENTITY API
 * ============================================================================ */

/**
 * Entity stride calculation
 * Ensures proper alignment between consecutive entities
 */
#define DE_ENTITY_STRIDE(PAYLOAD) _DE_ALIGN4(sizeof(struct de_entity) + (PAYLOAD))

/**
 * Entity management functions
 */
void *de_entity_exec(de_entity);      // Execute current state without updating it
void *de_entity_update(de_entity);    // Update entity (call state, handle transitions)
void de_entity_pause(de_entity);      // Park entity in the paused zone
void de_entity_resume(de_entity);     // Bring entity back to the active zone
void de_entity_delete(de_entity);     // Mark entity for deletion (calls destructor); works from either zone
void de_entity_move_front(de_entity); // Move to front of active section (priority)
void de_entity_move_back(de_entity);  // Move to back of active section (defer)

/* ============================================================================
 * MANAGER API
 * ============================================================================ */

/**
 * Structured storage declaration.
 * Use with DE_MANAGER_ARGS() when calling de_manager_init().
 *
 * Example:
 *    DE_MANAGER_STORAGE(m1st, 8, sizeof(struct MyComponent));
 *    de_manager_init(&m1, DE_MANAGER_ARGS(m1st));
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

/**
 * Iterates the active zone only (backward order).
 *
 * Within the CODE block:
 * - INDEX:  Current index in iteration
 * - ITEMS:  Entity list
 * - ENTITY: Available entity pointer
 *
 * Safety rule: deleting/pausing/resuming the entity currently being visited
 * (ENTITY) is safe. Mutating a DIFFERENT entity mid-loop is NOT guaranteed safe.
 *
 * Example:
 *    DE_MANAGER_FOREACH(my_manager, {
 *        if (ENTITY->tag == PLAYER_TAG)
 *            update_player(ENTITY);
 *    });
 */
#define DE_MANAGER_FOREACH(M, CODE)          \
    do                                       \
    {                                        \
        uint16_t INDEX = (M)->active_count;  \
        de_entity *ITEMS = (M)->items;       \
        while (INDEX--)                      \
        {                                    \
            de_entity ENTITY = ITEMS[INDEX]; \
            CODE;                            \
        }                                    \
    } while (0)

/**
 * Manager management functions
 *
 * de_manager_init:   Initializes manager with pre-allocated storage. Starts
 *                    with everything free (size = 0, paused_start = capacity).
 * de_manager_new:    Creates new entity from the free zone (returns NULL if full).
 * de_manager_update: Updates all active entities (paused ones are never touched).
 * de_manager_reset:  Deletes active entities in the manager.
 */
void de_manager_init(de_manager *, de_entity *, void *, uint16_t, uint16_t);
de_entity de_manager_new(de_manager *);
void de_manager_update(de_manager *);
void de_manager_reset(de_manager *);

/* ============================================================================
 * SYSTEM API
 * ============================================================================ */

/**
 * Static storage declaration for a system.
 * Use with DE_SYSTEM_ARGS() when calling de_system_init().
 *
 * Example:
 *    de_system sys;
 *    DE_SYSTEM_STORAGE(sys_storage, 32, 3);
 *    de_system_init(&sys, DE_SYSTEM_ARGS(sys_storage));
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
 * Adds a group of parameters to the system.
 * Supports 1-5 data parameters (system pointer is always first).
 * Returns 1 on success, 0 if system is full.
 *
 * Example:
 *    DE_SYSTEM_ADD(physics_system, entity_ptr, &velocity, &position);
 */
#define DE_SYSTEM_ADD(...) \
    _DE_CONCAT(_DE_SYSTEM_ADD_, _DE_ADD_NARGS(__VA_ARGS__))(__VA_ARGS__)

/**
 * Iterates over system data.
 * Supports 0-5 data variables per iteration (system pointer always first).
 *
 * Example:
 *    DE_SYSTEM_FOREACH(physics_system, entity, velocity, position, {
 *        update_physics(entity, velocity, position);
 *    });
 */
#define DE_SYSTEM_FOREACH(...) \
    _DE_CONCAT(_DE_SYSTEM_FOREACH_, _DE_FOREACH_NARGS(__VA_ARGS__))(__VA_ARGS__)

/**
 * DE_SYSTEM_ITERATOR: Creates an iterator function from a foreach pattern.
 * The generated function returns DE_STATE_LOOP for use as entity state.
 *
 * Example:
 *    DE_SYSTEM_ITERATOR(physics_update, entity, velocity, position, {
 *        entity->x += velocity->dx;
 *        entity->y += velocity->dy;
 *    });
 * // Now physics_update can be used as an entity state function
 */
#define DE_SYSTEM_ITERATOR(...) \
    _DE_CONCAT(_DE_SYSTEM_ITERATOR_, _DE_FOREACH_NARGS(__VA_ARGS__))(__VA_ARGS__)

void de_system_init(de_system *, void **, uint16_t, uint16_t);
uint16_t de_system_remove(de_system *, void *);

/* ============================================================================
 * INTERNAL MACRO IMPLEMENTATIONS
 * ============================================================================ */

/* Internal system add implementation. Checks param count and capacity. */
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

/* Add variants for 1-5 data parameters */
#define _DE_SYSTEM_ADD_1(SYS, A) _DE_SYSTEM_ADD(SYS, 1, _p[0] = (void *)(A);)
#define _DE_SYSTEM_ADD_2(SYS, A, B) _DE_SYSTEM_ADD(SYS, 2, _p[0] = (void *)(A); _p[1] = (void *)(B);)
#define _DE_SYSTEM_ADD_3(SYS, A, B, C) _DE_SYSTEM_ADD(SYS, 3, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C);)
#define _DE_SYSTEM_ADD_4(SYS, A, B, C, D) _DE_SYSTEM_ADD(SYS, 4, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C); _p[3] = (void *)(D);)
#define _DE_SYSTEM_ADD_5(SYS, A, B, C, D, E) _DE_SYSTEM_ADD(SYS, 5, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C); _p[3] = (void *)(D); _p[4] = (void *)(E);)

/* Internal foreach implementation, wrapped in do-while for safety */
#define _DE_SYSTEM_FOREACH(SYSTEM, IT) \
    do                                 \
    {                                  \
        void **items = (SYSTEM)->pool; \
        void **end = (SYSTEM)->end;    \
        while (items < end)            \
        {                              \
            IT;                        \
            items += (SYSTEM)->params; \
        }                              \
    } while (0)

/* Foreach variants for 0-5 data variables */
#define _DE_SYSTEM_FOREACH_0(SYSTEM, IT) _DE_SYSTEM_FOREACH(SYSTEM, { IT; })
#define _DE_SYSTEM_FOREACH_1(SYSTEM, A, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = items[0]; IT; })
#define _DE_SYSTEM_FOREACH_2(SYSTEM, A, B, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = items[0]; B = items[1]; IT; })
#define _DE_SYSTEM_FOREACH_3(SYSTEM, A, B, C, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = items[0]; B = items[1]; C = items[2]; IT; })
#define _DE_SYSTEM_FOREACH_4(SYSTEM, A, B, C, D, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = items[0]; B = items[1]; C = items[2]; D = items[3]; IT; })
#define _DE_SYSTEM_FOREACH_5(SYSTEM, A, B, C, D, E, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = items[0]; B = items[1]; C = items[2]; D = items[3]; E = items[4]; IT; })

/* Internal iterator generator: calls the corresponding foreach macro. */
#define _DE_SYSTEM_ITERATOR(NAME, FOREACH, ...) \
    void *NAME(de_system *system)               \
    {                                           \
        FOREACH(system, __VA_ARGS__);           \
        return DE_STATE_LOOP;                   \
    }

/* Iterator variants for 0-5 data variables */
#define _DE_SYSTEM_ITERATOR_0(NAME, IT) _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_0, IT)
#define _DE_SYSTEM_ITERATOR_1(NAME, A, IT) _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_1, A, IT)
#define _DE_SYSTEM_ITERATOR_2(NAME, A, B, IT) _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_2, A, B, IT)
#define _DE_SYSTEM_ITERATOR_3(NAME, A, B, C, IT) _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_3, A, B, C, IT)
#define _DE_SYSTEM_ITERATOR_4(NAME, A, B, C, D, IT) _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_4, A, B, C, D, IT)
#define _DE_SYSTEM_ITERATOR_5(NAME, A, B, C, D, E, IT) _DE_SYSTEM_ITERATOR(NAME, _DE_SYSTEM_FOREACH_5, A, B, C, D, E, IT)

#endif /* DARKEN_H */

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DARKEN_IMPLEMENTATION

/**
 * Swaps two entities in the manager's array.
 * Both entities must belong to the same manager.
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
 * Preserves special states (DELETE, LOOP, PAUSE) if they are already set.
 */
void *de_entity_update(de_entity $)
{
    de_state s = de_entity_exec($);

    if (!DE_STATE_IS_LOOP(s))
        $->state = s;

    return s;
}

/**
 * Pauses an entity: moves it out of the active zone, across the free zone,
 * into the paused zone. The entity keeps its physical slot in the pool (it
 * is not freed and cannot be handed out by de_manager_new), so entity->data
 * stays valid and any external references to it (e.g. from a de_system)
 * remain safe until it's resumed or explicitly deleted.
 *
 * No-op if the entity is not currently active (already paused, or free).
 */
void de_entity_pause(de_entity $)
{
    de_manager *m = $->manager;
    uint16_t slot = $->slot;

    if (slot >= m->active_count)
        return;

    // Shrink active zone from the right and fill the vacated slot
    --m->active_count;

    if (slot != m->active_count)
    {
        de_entity e = m->items[m->active_count];
        m->items[slot] = e;
        e->slot = slot;
    }

    // Grow paused zone from the left and place entity at the new boundary
    --m->paused_start;
    m->items[m->paused_start] = $;
    $->slot = m->paused_start;
}

/**
 * Resumes an entity: moves it out of the paused zone, across the free zone,
 * back into the active zone. Mirror of de_entity_pause.
 *
 * No-op if the entity is not currently paused.
 */
void de_entity_resume(de_entity $)
{
    de_manager *m = $->manager;
    uint16_t slot = $->slot;

    if (slot < m->paused_start)
        return;

    // Shrink paused zone from the left and fill the vacated slot
    if (slot != m->paused_start)
    {
        de_entity e = m->items[m->paused_start];
        m->items[slot] = e;
        e->slot = slot;
    }

    ++m->paused_start;

    // Grow active zone from the right and place entity at the new boundary
    m->items[m->active_count] = $;
    $->slot = m->active_count;
    ++m->active_count;
}

/**
 * Deletes an entity, whether it's currently active or paused.
 * Calls destructor if present, then reorganizes the array so the freed
 * slot rejoins the free zone (available again for de_manager_new).
 *
 * Algorithm:
 * 1. Early exit if the slot is already free (not active, not paused)
 * 2. Set state to delete
 * 3. Call destructor if present (can override deletion by returning
 *    anything other than DE_STATE_DELETE)
 * 4. If deletion is confirmed: shrink whichever zone (active or paused)
 *    the entity currently belongs to, from the edge adjacent to the free zone
 */
void de_entity_delete(de_entity $)
{
    de_manager *m = $->manager;

    // Already free: nothing to do
    if ($->slot >= m->active_count && $->slot < m->paused_start)
        return;

    if ($->destructor)
        $->destructor($->data);

    if ($->slot >= m->paused_start)
    {
        // Was paused: shrink paused zone from the left, slot rejoins the free zone
        if ($->slot != m->paused_start)
            _de_entity_swap($, m->items[m->paused_start]);

        ++m->paused_start;
    }
    else
    {
        // Was active: shrink active zone from the right, slot rejoins the free zone
        if ($->slot != m->active_count - 1)
            _de_entity_swap($, m->items[m->active_count - 1]);

        --m->active_count;
    }
}

/**
 * Moves entity to front of active section.
 * Useful for prioritizing entity updates. Since the manager updates
 * entities backward, the front is the highest active index.
 */
void de_entity_move_front(de_entity $)
{
    de_manager *m = $->manager;

    if ($->slot < m->active_count && $->slot != m->active_count - 1)
        _de_entity_swap($, m->items[m->active_count - 1]);
}

/**
 * Moves entity to back of active section.
 * Useful for deferring entity updates. Since the manager updates
 * entities backward, the back is index 0.
 */
void de_entity_move_back(de_entity $)
{
    de_manager *m = $->manager;

    if ($->slot < m->active_count && $->slot != 0)
        _de_entity_swap($, m->items[0]);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Initializes an entity manager.
 * Everything starts free: size = 0, paused_start = capacity.
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
    $->active_count = 0;
    $->paused_start = capacity;

    uint16_t stride = DE_ENTITY_STRIDE(bytes);
    uint8_t *p = storage;

    for (uint16_t i = 0; i < capacity; ++i)
    {
        de_entity e = (de_entity)p;

        items[i] = e;
        e->manager = $;
        e->slot = i;

        p += stride;
    }
}

/**
 * Creates a new entity from the free zone.
 * Returns NULL if there are no free slots (size == paused_start).
 */
de_entity de_manager_new(de_manager *$)
{
    if ($->active_count >= $->paused_start)
        return 0;

    de_entity e = $->items[$->active_count];
    e->manager = $;
    e->slot = $->active_count++;
    e->state = DE_STATE_DELETE;
    e->destructor = 0;
    e->tag = 0;

    return e;
}

/**
 * Updates all active entities. Paused entities are never touched.
 * Also processes pending pauses and deletions triggered by state functions.
 *
 * Algorithm:
 * 1. Iterate backwards through the active zone [0, active_count)
 * 2. Call state function for each active entity
 * 3. Handle state transitions (pause, delete, change)
 *
 * Note: both de_entity_pause and de_entity_delete shrink the active zone
 * from its right edge (active_count), which is exactly where this backward
 * traversal starts — so an entity relocated mid-loop by either call was
 * always already visited. No live re-read of active_count is needed.
 */
void de_manager_update(de_manager *$)
{
    uint16_t i = $->active_count;
    de_entity *items = $->items;

    while (i--)
    {
        de_entity e = items[i];
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
 * Deletes active entities in the manager.
 */
void de_manager_reset(de_manager *$)
{
    DE_MANAGER_FOREACH($, de_entity_delete(ENTITY));

    $->active_count = 0;
    $->paused_start = $->capacity;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Initializes a data system.
 * Prepares pool for sequential access.
 */
void de_system_init(de_system *$, void **storage, uint16_t capacity_groups, uint16_t params)
{
    $->pool = storage;
    $->end = storage;
    $->size = 0;
    $->capacity = capacity_groups * params;
    $->params = params;
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
    for (uint16_t i = 0; i < $->size; i += $->params)
        if ($->pool[i] == first)
        {
            $->size -= $->params;

            // Compact if not last group
            if (i != $->size)
                for (uint16_t k = 0; k < $->params; ++k)
                    $->pool[i + k] = $->pool[$->size + k];

            $->end -= $->params;

            return 1;
        }

    return 0;
}

#endif /* DARKEN_IMPLEMENTATION */
