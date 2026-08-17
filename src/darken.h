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
 * GNU C / SGDK note:
 * This header uses GNU C extensions (__attribute__ and statement expressions)
 * because Darken targets GCC/SGDK and the Motorola 68000.
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
 * Align a byte count to a 4-byte boundary.
 *
 * The Motorola 68000 requires word alignment for word/long accesses.
 * Longword alignment keeps entity strides predictable and efficient.
 */
#define _DE_ALIGN4(X) (((X) + 3U) & ~3U)

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================ */

/**
 * Entity state/destructor callback type.
 *
 * Receives entity->data and returns either another state callback or one of
 * the DE_STATE_* control values.
 *
 * GNU C/SGDK note: Darken represents both function pointers and control
 * values through void*. This is an intentional implementation choice for
 * the 68000/SGDK target and is not a strictly portable ISO C interface.
 */
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
 * Manager: Entity container and lifecycle manager.
 *
 * Maintains the pointer array in three logical zones:
 *
 * Array Layout:
 * [ active entities ][   free slots   ][ paused entities ]
 * 0             active_count      paused_start        capacity
 *
 * The entity objects themselves live in the caller-provided storage block;
 * manager->items contains pointers to those fixed addresses.
 *
 * - Active zone [0, active_count):
 *     Entity pointers updated every frame by de_manager_update(). Iterable with
 *     DE_MANAGER_FOREACH. Freely created (de_manager_new) and deleted.
 *
 * - Free zone [active_count, paused_start):
 *     Pointer slots not currently assigned to an entity. This is where
 *     de_manager_new()
 *     takes its next entity from, and where an active entity's slot goes
 *     right after it's deleted.
 *
 * - Paused zone [paused_start, capacity):
 *     Entity pointers parked out of the update loop. de_manager_update() never
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
 * System: Flat packed pool of data pointers.
 *
 * The pool is organized in groups of 'params' pointers. Each group can hold
 * the pointers associated with one processed item/entity.
 *
 * Pool Layout (params=3):
 * [e0.a][e0.b][e0.c][e1.a][e1.b][e1.c][e2.a][e2.b][e2.c]...
 */
struct de_system
{
    void **pool;       // Data storage array
    void **end;        // One past the last element in use
    uint16_t capacity; // Total pointer slots in the pool (groups * params)
    uint16_t size;     // Used pointer slots (always a multiple of params)
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
 * Execute the current state without storing its return value into state.
 *
 * Returns the state function result, or DE_STATE_DELETE when the entity has
 * no executable state.
 */
void *de_entity_exec(de_entity);

/**
 * Execute the current active state and store a returned transition.
 *
 * DE_STATE_LOOP leaves the current state unchanged.
 * DE_STATE_PAUSE / DE_STATE_DELETE are stored as pending transitions and are
 * processed by the next de_manager_update().
 *
 * If the entity is not active, its state is left untouched and returned.
 */
void *de_entity_update(de_entity);

/**
 * Swap two entities belonging to the same manager.
 *
 * This only swaps pointers in manager->items[]; entity memory never moves.
 */
void de_entity_swap(de_entity, de_entity);

/**
 * Move an active entity to the end of the active zone so it is visited first
 * by the backwards DE_MANAGER_ITERATE()/de_manager_update() traversal.
 */
void de_entity_pause(de_entity);

/**
 * Resume a paused entity and place it into the active zone.
 */
void de_entity_resume(de_entity);

/**
 * Request deletion of an entity from either the active or paused zone.
 *
 * The destructor, when present, is called before the entity is removed.
 * Its return value is ignored; destructors cannot cancel deletion.
 */
void de_entity_delete(de_entity);

/**
 * Move an active entity to the highest active index.
 *
 * Because Darken updates/iterates backwards, this makes the entity run earlier
 * in the next traversal.
 */
void de_entity_move_front(de_entity);

/**
 * Move an active entity to index 0.
 *
 * Because Darken updates/iterates backwards, this makes the entity run later
 * in the next traversal.
 */
void de_entity_move_back(de_entity);

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
 * de_manager_init:   Initializes a manager using caller-provided storage.
 * de_manager_new:    Creates an entity from the free zone (returns NULL if full).
 * de_manager_update: Updates active entities and processes pending transitions.
 * de_manager_reset:  Deletes all active and paused entities.
 */
void de_manager_init(de_manager *, de_entity *, void *, uint16_t, uint16_t);

/**
 * Create an entity from the free zone.
 *
 * Returns NULL when no free slot exists.
 *
 * Entity data is NOT initialized.
 */
de_entity de_manager_new(de_manager *);

/**
 * Update all active entities.
 *
 * Paused entities are never touched.
 * State transitions returned by an active state are applied as pending
 * transitions and handled on the next manager update.
 */
void de_manager_update(de_manager *);

/**
 * Delete every active and paused entity and restore the manager to its initial
 * empty state. Destructors are called for every entity that is deleted.
 */
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
 *    DE_SYSTEM_STORAGE(storage, 32, 3);
 *    de_system_init(&sys, DE_SYSTEM_ARGS(storage));
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
 * Adds one parameter group to the system.
 * Supports 1-5 data pointers (the system pointer is always the first macro argument).
 * Returns 1 on success, 0 if system is full.
 *
 * Example:
 *    DE_SYSTEM_ADD(physics_system, entity_ptr, &velocity, &position);
 */
#define DE_SYSTEM_ADD(...) \
    _DE_CONCAT(_DE_SYSTEM_ADD_, _DE_ADD_NARGS(__VA_ARGS__))(__VA_ARGS__)

/**
 * Iterates over the system's parameter groups.
 * Supports 0-5 output variables per iteration; the system pointer is always the first macro argument.
 *
 * Example:
 *    DE_SYSTEM_FOREACH(physics_system, entity, velocity, position, {
 *        update_physics(entity, velocity, position);
 *    });
 */
#define DE_SYSTEM_FOREACH(...) \
    _DE_CONCAT(_DE_SYSTEM_FOREACH_, _DE_FOREACH_NARGS(__VA_ARGS__))(__VA_ARGS__)

/**
 * DE_SYSTEM_ITERATOR: Creates a function that runs a system foreach pattern
 * and returns DE_STATE_LOOP. The generated function receives a de_system *;
 * it is not type-compatible with de_state(void *) under strict ISO C.
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
void de_entity_swap(de_entity a, de_entity b)
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
 * Pauses an entity: moves its pointer from the active zone into the paused
 * zone. The entity keeps its physical storage address (it
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
 * Resumes an entity: moves its pointer from the paused zone back into the
 * active zone. Mirror of de_entity_pause.
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
 * 1. Exit if the entity is already in the free zone.
 * 2. Call the destructor, if present.
 * 3. Remove the entity from its zone by swapping with the zone edge adjacent
 *    to the free zone.
 * 4. Grow the free zone by one slot.
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
            de_entity_swap($, m->items[m->paused_start]);

        ++m->paused_start;
    }
    else
    {
        // Was active: shrink active zone from the right, slot rejoins the free zone
        if ($->slot != m->active_count - 1)
            de_entity_swap($, m->items[m->active_count - 1]);

        --m->active_count;
    }
}

/**
 * Moves an active entity to the highest active index.
 * Because the manager updates backwards, this makes the entity run earlier
 * in the next traversal. No-op for paused/free entities.
 */
void de_entity_move_front(de_entity $)
{
    de_manager *m = $->manager;

    if ($->slot < m->active_count && $->slot != m->active_count - 1)
        de_entity_swap($, m->items[m->active_count - 1]);
}

/**
 * Moves an active entity to index 0.
 * Because the manager updates backwards, this makes the entity run later
 * in the next traversal. No-op for paused/free entities.
 */
void de_entity_move_back(de_entity $)
{
    de_manager *m = $->manager;

    if ($->slot < m->active_count && $->slot != 0)
        de_entity_swap($, m->items[0]);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Initializes an entity manager.
 * Everything starts free: active_count = 0, paused_start = capacity.
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
 * State callbacks may return a new state, DE_STATE_PAUSE, or DE_STATE_DELETE;
 * those transitions are stored and applied by the following manager update.
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
 * Deletes all active entities in the manager and restores all
 * manager zones to their initial empty state.
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
 * 'capacity_groups' is the number of parameter groups the pool can hold;
 * 'params' is the number of pointers in each group.
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
 * Removes a group from the system by matching its first pointer.
 * Returns 1 if removed, 0 if not found. The last group is moved into the
 * removed group's position to keep the pool packed.
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
