/**
 * Darken Entity System - Optimized for Motorola 68000
 *
 * High-performance entity-component system for Sega Genesis/Mega Drive development.
 *
 * ARCHITECTURE OVERVIEW:
 * ----------------------
 * This library implements a lightweight Entity-Component-System (ECS) architecture
 * optimized specifically for the Motorola 68000 processor. The design focuses on:
 *
 * 1. CACHE-FRIENDLY MEMORY LAYOUT:
 *    - Entities are stored in contiguous memory blocks
 *    - Pre-calculated pointers eliminate runtime address calculations
 *    - Sequential memory access patterns maximize performance
 *
 * 2. O(1) OPERATIONS:
 *    - Entity creation, deletion, and swapping use constant-time operations
 *    - No memory allocation/deallocation at runtime (static allocation)
 *    - Swap-and-pop technique for removal avoids array shifting
 *
 * 3. STATE MACHINE PATTERN:
 *    - Each entity has a function pointer representing its current state
 *    - States return the next state (or special control codes)
 *    - Enables simple, efficient state transitions without switch statements
 *
 * 4. ACTIVE/PAUSED PARTITIONING:
 *    - Entities are partitioned into active and paused sections
 *    - Updates only iterate over active entities
 *    - Pause/resume operations are O(1) swaps
 *
 * CORE CONCEPTS:
 * -------------
 * ENTITY:
 *   A container with lifecycle management (state, destructor, manager reference)
 *   and user-defined data. Entities don't contain logic themselves - they are
 *   managed by the entity manager and processed by systems.
 *
 * MANAGER:
 *   Owns and updates a collection of entities. Maintains the active/paused
 *   partition and handles entity lifecycle (creation, deletion, pausing).
 *   Uses static memory allocation for predictable performance.
 *
 * SYSTEM:
 *   A generic data pool for component storage. Systems process entities
 *   by iterating over their component data. The pool layout is optimized
 *   for sequential access patterns.
 *
 * STATE FUNCTIONS:
 *   Functions that take entity data as parameter and return the next state.
 *   Special return values control entity lifecycle:
 *   - de_state_loop: Continue with current state (no change)
 *   - de_state_pause: Pause the entity
 *   - de_state_delete: Delete the entity
 *   - Custom function pointer: Transition to new state
 *
 * 68K-SPECIFIC OPTIMIZATIONS:
 * ---------------------------
 * - Register-friendly code structure (minimal memory access)
 * - Avoid multiplication in hot paths (pre-calculated strides)
 * - Sequential memory access (leverages 68K's address registers)
 * - Minimal branching (linear code where possible)
 * - 4-byte alignment (critical for 68K bus performance)
 * - Word-sized operations (16-bit native for 68K)
 *
 * USAGE PATTERNS:
 * --------------
 * 1. Define entity data structure
 * 2. Create manager with static allocation
 * 3. Implement state functions for entity behavior
 * 4. Create entities and set their initial states
 * 5. Update manager each frame
 *
 * BENCHMARKS (from BlastEm emulator kdebug):
 * -----------------------------------------
 * - Create+reset 32 entities x2000 reps: 343 frames
 * - Update 32 entities x2000 reps: 105 frames
 * - Create32+applyAll(delete pairs)+reset x2000 reps: 501 frames
 * - Create+reset 128 entities x500 reps: 343 frames
 * - Create+reset 256 entities x250 reps: 342 frames
 * - Update 128 entities x500 reps: 103 frames
 * - Update 256 entities x250 reps: 102 frames
 * - Entity swap x50000: 0 frames (negligible overhead)
 * - Individual update (32 entities x2000 reps): 135 frames
 */

#ifndef DARKEN_H
#define DARKEN_H

#include <genesis.h>

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
 * Maintains entities in a contiguous array with active/paused partitioning.
 * The array layout allows efficient iteration and O(1) entity operations.
 *
 * Array Layout:
 * [paused entities...][active entities...]
 *                    ^pause_index
 *
 * Size and capacity are 16-bit for optimal 68K performance.
 */
struct de_manager
{
    de_entity *items;     // Array of entity pointers (pre-allocated)
    uint16_t size;        // Current number of entities
    uint16_t capacity;    // Maximum number of entities
    uint16_t pause_index; // Partition index: paused < index <= active
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
#define de_state_delete ((de_state)0) // Delete entity after update
#define de_state_loop ((de_state)1)   // Continue with current state
#define de_state_pause ((de_state)2)  // Pause entity after update

/**
 * State checking macros
 * Optimized for 68K comparison operations
 */
#define de_state_is_deleted(S) ((S) == de_state_delete) // Check if delete requested
#define de_state_is_loop(S) ((S) == de_state_loop)      // Check if loop requested
#define de_state_is_paused(S) ((S) == de_state_pause)   // Check if pause requested
#define de_state_is_active(S) ((S) > de_state_pause)    // Check if active state function

/* ============================================================================
 * ENTITY API
 * ============================================================================ */

/**
 * Entity management functions
 * All operations are O(1) unless otherwise noted
 */
void de_entity_exec(de_entity);       // Execute current state without updating it
void de_entity_update(de_entity);     // Update entity (call state, handle transitions)
void de_entity_pause(de_entity);      // Move entity to paused section
void de_entity_resume(de_entity);     // Move entity to active section
void de_entity_delete(de_entity);     // Mark entity for deletion (calls destructor)
void de_entity_move_front(de_entity); // Move to front of active section (priority)
void de_entity_move_back(de_entity);  // Move to back of active section (defer)

/* ============================================================================
 * MANAGER CREATION MACROS
 * ============================================================================ */

/**
 * Creates a manager with static memory allocation
 *
 * Parameters:
 * - MGR: Manager variable name
 * - CAPACITY: Maximum number of entities
 * - PAYLOAD: Size of entity data in bytes
 *
 * The macro generates:
 * 1. Array of entity pointers
 * 2. Contiguous storage for entity data
 * 3. Initialization call
 *
 * Example:
 * de_manager_create(my_manager, 32, sizeof(MyEntityData));
 */
#define de_manager_create(MGR, CAPACITY, PAYLOAD)                                                            \
    de_entity _DE_UNIQUE(_i_, __LINE__)[(CAPACITY)];                                                         \
    uint8_t _DE_UNIQUE(_s_, __LINE__)[(CAPACITY) * DE_ENTITY_STRIDE((PAYLOAD))] __attribute__((aligned(4))); \
    de_manager_init((MGR), _DE_UNIQUE(_i_, __LINE__), _DE_UNIQUE(_s_, __LINE__), (CAPACITY), (PAYLOAD))

/**
 * Structured storage declaration.
 * Use with DE_MANAGER_ARGS() when calling de_manager_init().
 *
 * Example:
 * DE_MANAGER_STORAGE(m1st, 8, sizeof(struct MyComponent));
 * de_manager_init(&m1, DE_MANAGER_ARGS(m1st));
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
 * de_manager_pause: Pauses all entities (O(1))
 * de_manager_resume: Resumes all entities (O(1))
 * de_manager_reset: Deletes all entities
 */
void de_manager_init(de_manager *, de_entity *, void *, uint16_t, uint16_t);
de_entity de_manager_new(de_manager *);
void de_manager_update(de_manager *);
void de_manager_pause(de_manager *);
void de_manager_resume(de_manager *);
void de_manager_reset(de_manager *);

/* ============================================================================
 * ITERATION MACROS
 * ============================================================================ */

/**
 * Iteration limit functions (internal use)
 */
#define _de_manager_limit_active(M) ((M)->pause_index) // Only active entities
#define _de_manager_limit_all(M) (0)                   // All entities

/**
 * de_manager_iterate: Iterates over active entities only
 * de_manager_iterateAll: Iterates over all entities (including paused)
 *
 * Within the CODE block:
 * - ENTITY: Available entity pointer
 * - INDEX: Current index in iteration
 *
 * Example:
 * de_manager_iterate(my_manager, {
 *     if (ENTITY->tag == PLAYER_TAG) {
 *         update_player(ENTITY);
 *     }
 * });
 */
#define de_manager_iterate(M, CODE) _de_manager_iterate(_de_manager_limit_active, M, CODE)
#define de_manager_iterateAll(M, CODE) _de_manager_iterate(_de_manager_limit_all, M, CODE)

/**
 * Internal iteration implementation
 * Iterates backwards for safe deletion during iteration
 */
#define _de_manager_iterate(LIMIT_FN, M, CODE)    \
    do                                            \
    {                                             \
        uint16_t INDEX = (M)->size;               \
        uint16_t _limit = LIMIT_FN(M);            \
        while (INDEX-- > _limit)                  \
        {                                         \
            de_entity ENTITY = (M)->items[INDEX]; \
            CODE;                                 \
        }                                         \
    } while (0)

/* ============================================================================
 * APPLICATION MACROS (with target collection)
 * ============================================================================ */

/**
 * de_manager_apply: Collect entities matching filter, then apply action
 * de_manager_applyAll: Same but includes paused entities
 *
 * Parameters:
 * - M: Manager
 * - F: Filter condition (evaluated per entity)
 * - A: Action to apply to collected entities
 *
 * Useful for operations that might invalidate iteration (like batch deletion)
 *
 * Example:
 * de_manager_apply(my_manager, ENTITY->health <= 0, delete_entity(ENTITY));
 */
#define de_manager_apply(M, F, A) _de_manager_apply(de_manager_iterate, M, F, A)
#define de_manager_applyAll(M, F, A) _de_manager_apply(de_manager_iterateAll, M, F, A)

/**
 * Internal application implementation
 * Collects matching entities first, then applies action
 */
#define _de_manager_apply(ITER, M, FILTER, ACTION) \
    do                                             \
    {                                              \
        de_entity _targets[(M)->size + 1];         \
        uint16_t _count = 0;                       \
        ITER(M, { if (FILTER) _targets[_count++] = ENTITY; });                              \
        while (_count--)                           \
            ACTION(_targets[_count]);              \
    } while (0)

/* ============================================================================
 * SYSTEM MACROS
 * ============================================================================ */

/**
 * de_system_create: Creates a system with static allocation
 *
 * Parameters:
 * - SYS: System variable name
 * - CAPACITY: Maximum number of groups
 * - PARAMS: Number of parameters per group
 *
 * Example:
 * de_system_create(physics_system, 32, 3); // 32 entities, 3 params each
 */
#define de_system_create(SYS, CAPACITY, PARAMS)              \
    void *_DE_UNIQUE(_sp_, __LINE__)[(CAPACITY) * (PARAMS)]; \
    de_system_init((SYS), _DE_UNIQUE(_sp_, __LINE__), (CAPACITY), (PARAMS))

/**
 * de_system_add: Adds a group of parameters to the system
 * Supports 1-5 parameters (variable arguments)
 * Returns 1 on success, 0 if system is full
 *
 * Example:
 * de_system_add(physics_system, entity_ptr, &velocity, &position);
 */
#define de_system_add(...)                                           \
    _DE_GET_MACRO(__VA_ARGS__, _,                                    \
                  _de_system_add5, _de_system_add4, _de_system_add3, \
                  _de_system_add2, _de_system_add1, unused)(__VA_ARGS__)

/**
 * de_system_foreach: Iterates over system data
 * Supports 0-5 variables per iteration
 *
 * Example:
 * de_system_foreach(physics_system, entity, velocity, position, {
 *     update_physics(entity, velocity, position);
 * });
 */
#define de_system_foreach(...)                                                      \
    _DE_GET_MACRO(__VA_ARGS__,                                                      \
                  _de_system_foreach_5, _de_system_foreach_4, _de_system_foreach_3, \
                  _de_system_foreach_2, _de_system_foreach_1, _de_system_foreach_0)(__VA_ARGS__)

/**
 * de_system_iterator: Creates an iterator function from a foreach pattern
 * The generated function returns de_state_loop for use as entity state
 *
 * Example:
 * de_system_iterator(physics_update, entity, velocity, position, {
 *     entity->x += velocity->dx;
 *     entity->y += velocity->dy;
 * });
 * // Now physics_update can be used as an entity state function
 */
#define de_system_iterator(...)                                                        \
    _DE_GET_MACRO(__VA_ARGS__,                                                         \
                  _de_system_iterator_5, _de_system_iterator_4, _de_system_iterator_3, \
                  _de_system_iterator_2, _de_system_iterator_1, _de_system_iterator_0)(__VA_ARGS__)

/* ============================================================================
 * SYSTEM MACRO IMPLEMENTATIONS
 * ============================================================================ */

/**
 * Internal system add implementation
 */
#define _de_system_add(SYS, N, ...)          \
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
#define _de_system_add1(SYS, A) _de_system_add(SYS, 1, _p[0] = (void *)(A);)
#define _de_system_add2(SYS, A, B) _de_system_add(SYS, 2, _p[0] = (void *)(A); _p[1] = (void *)(B);)
#define _de_system_add3(SYS, A, B, C) _de_system_add(SYS, 3, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C);)
#define _de_system_add4(SYS, A, B, C, D) _de_system_add(SYS, 4, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C); _p[3] = (void *)(D);)
#define _de_system_add5(SYS, A, B, C, D, E) _de_system_add(SYS, 5, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C); _p[3] = (void *)(D); _p[4] = (void *)(E);)

/**
 * Internal iterator generator
 */
#define _de_system_iterator(NAME, FOREACH, ...) \
    void *NAME(de_system *system)               \
    {                                           \
        FOREACH(system, __VA_ARGS__);           \
        return (void *)de_state_loop;           \
    }

/* Iterator variants for 0-5 variables */
#define _de_system_iterator_0(NAME, IT) _de_system_iterator(NAME, _de_system_foreach_0, IT)
#define _de_system_iterator_1(NAME, A, IT) _de_system_iterator(NAME, _de_system_foreach_1, A, IT)
#define _de_system_iterator_2(NAME, A, B, IT) _de_system_iterator(NAME, _de_system_foreach_2, A, B, IT)
#define _de_system_iterator_3(NAME, A, B, C, IT) _de_system_iterator(NAME, _de_system_foreach_3, A, B, C, IT)
#define _de_system_iterator_4(NAME, A, B, C, D, IT) _de_system_iterator(NAME, _de_system_foreach_4, A, B, C, D, IT)
#define _de_system_iterator_5(NAME, A, B, C, D, E, IT) _de_system_iterator(NAME, _de_system_foreach_5, A, B, C, D, E, IT)

/**
 * Internal foreach implementation
 * Optimized for sequential access
 */
#define _de_system_foreach(SYSTEM, IT)                     \
    void **items = (SYSTEM)->pool;                         \
    for (uint16_t i = 0, size = (SYSTEM)->size; i < size;) \
        IT;

/* Foreach variants for 0-5 variables */
#define _de_system_foreach_0(SYSTEM, IT) _de_system_foreach(SYSTEM, { IT; })
#define _de_system_foreach_1(SYSTEM, A, IT) _de_system_foreach(SYSTEM, { A = items[i++]; IT; })
#define _de_system_foreach_2(SYSTEM, A, B, IT) _de_system_foreach(SYSTEM, { A = items[i++]; B = items[i++]; IT; })
#define _de_system_foreach_3(SYSTEM, A, B, C, IT) _de_system_foreach(SYSTEM, { A = items[i++]; B = items[i++]; C = items[i++]; IT; })
#define _de_system_foreach_4(SYSTEM, A, B, C, D, IT) _de_system_foreach(SYSTEM, { A = items[i++]; B = items[i++]; C = items[i++]; D = items[i++]; IT; })
#define _de_system_foreach_5(SYSTEM, A, B, C, D, E, IT) _de_system_foreach(SYSTEM, { A = items[i++]; B = items[i++]; C = items[i++]; D = items[i++]; E = items[i++]; IT; })

/* ============================================================================
 * INTERNAL UTILITIES
 * ============================================================================ */

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
 * Entity stride calculation
 * Ensures proper alignment between consecutive entities
 */
#define DE_ENTITY_STRIDE(PAYLOAD) _DE_ALIGN4(sizeof(struct de_entity) + (PAYLOAD))

/**
 * Macro argument count selector
 * Supports 1-7 arguments for variadic macro dispatch
 */
#define _DE_GET_MACRO(_1, _2, _3, _4, _5, _6, _7, NAME, ...) NAME

/**
 * Public system API
 */
void de_system_init(de_system *, void **, uint16_t, uint16_t);
uint16_t de_system_remove(de_system *, void *);

#endif /* DARKEN_H */

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DARKEN_IMPLEMENTATION

/**
 * Executes the entity's current state without updating it.
 * Useful for initialization or manual execution scenarios.
 *
 * Performance: O(1) - Direct function pointer call
 */
void de_entity_exec(de_entity $)
{
    if (de_state_is_active($->state))
        $->state($->data);
}

/**
 * Updates a single entity.
 * Calls the state function and handles state transitions.
 *
 * Performance: O(1) - Direct function pointer call with state check
 * 68K Optimization: Uses register variables for state to minimize memory access
 */
void de_entity_update(de_entity $)
{
    de_state s = $->state;

    if (de_state_is_active(s))
    {
        s = s($->data);

        // Only write back if state actually changed
        if (!de_state_is_loop(s))
            $->state = s;
    }
}

/**
 * Swaps two entities in the manager's array.
 * Internal function critical for O(1) entity operations.
 *
 * Performance: O(1) - Pointer and index swaps
 * 68K Optimization: Minimizes memory access by using local registers
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
 * Uses swap-and-increment technique for O(1) operation.
 *
 * Performance: O(1) - Single swap operation
 */
void de_entity_pause(de_entity $)
{
    de_manager *m = $->manager;

    // Only if in active section
    if ($->slot >= m->pause_index && $->slot < m->size)
        _de_entity_swap($, m->items[m->pause_index++]);
}

/**
 * Resumes an entity by moving it to the active section.
 * Uses swap-and-decrement technique for O(1) operation.
 *
 * Performance: O(1) - Single swap operation
 */
void de_entity_resume(de_entity $)
{
    de_manager *m = $->manager;

    // Only if in paused section
    if ($->slot < m->pause_index)
        _de_entity_swap($, m->items[--m->pause_index]);
}

/**
 * Deletes an entity.
 * Calls destructor if present, then reorganizes array.
 *
 * Algorithm:
 * 1. Early exit if already deleted (slot >= size)
 * 2. Set state to delete
 * 3. Call destructor if present (can override deletion)
 * 4. If deletion confirmed, swap with last entity and reduce size
 *
 * Performance: O(1) - Single swap operation after destructor
 * 68K Optimization: Early checks avoid unnecessary work
 */
void de_entity_delete(de_entity $)
{
    de_manager *m = $->manager;

    // Early exit for already deleted entities
    if ($->slot >= m->size)
        return;

    $->state = de_state_delete;

    // Call destructor if exists
    if ($->destructor)
        $->state = $->destructor($->data);

    // Only delete if destructor allows it
    if (de_state_is_deleted($->state))
    {
        // Move to active section first if paused
        if ($->slot < m->pause_index)
            _de_entity_swap($, m->items[--m->pause_index]);

        --m->size;

        // Swap with last entity to maintain array density
        if ($->slot != m->size)
            _de_entity_swap($, m->items[m->size]);
    }
}

/**
 * Moves entity to front of active section.
 * Useful for prioritizing entity updates.
 *
 * Performance: O(1) - Single swap operation
 */
void de_entity_move_front(de_entity $)
{
    de_manager *m = $->manager;

    if ($->slot >= m->pause_index && $->slot < m->size)
        _de_entity_swap($, m->items[m->size - 1]);
}

/**
 * Moves entity to back of active section.
 * Useful for deferring entity updates.
 *
 * Performance: O(1) - Single swap operation
 */
void de_entity_move_back(de_entity $)
{
    de_manager *m = $->manager;

    if ($->slot >= m->pause_index && $->slot < m->size)
        _de_entity_swap($, m->items[m->pause_index]);
}

/**
 * Initializes an entity manager.
 * Pre-calculates entity pointers for O(1) access.
 *
 * Algorithm:
 * 1. Store basic manager data
 * 2. Calculate entity stride (aligned)
 * 3. Pre-calculate all entity pointers
 *
 * Performance: O(n) where n = capacity (one-time initialization)
 * 68K Optimization: Sequential pointer calculation for cache efficiency
 */
void de_manager_init(de_manager *$, de_entity *items, void *storage, uint16_t capacity, uint16_t bytes)
{
    $->items = items;
    $->capacity = capacity;
    $->size = 0;
    $->pause_index = 0;

    uint16_t stride = DE_ENTITY_STRIDE(bytes);

    // Pre-calculate pointers for fast access
    for (uint16_t i = 0; i < capacity; ++i)
        $->items[i] = (de_entity)((uint8_t *)storage + i * stride);
}

/**
 * Creates a new entity in the manager.
 * Returns NULL if manager is full.
 *
 * Performance: O(1) - Direct array access
 * 68K Optimization: Minimal operations for entity setup
 */
de_entity de_manager_new(de_manager *$)
{
    // Capacity check
    if ($->size >= $->capacity)
        return 0;

    de_entity e = $->items[$->size];
    e->manager = $;
    e->slot = $->size++;
    e->state = de_state_delete;
    e->destructor = 0;
    e->tag = 0;

    return e;
}

/**
 * Updates all active entities.
 * Also processes pending pauses and deletions.
 *
 * Algorithm:
 * 1. Iterate backwards through active section
 * 2. Call state function for each active entity
 * 3. Handle state transitions (pause, delete, change)
 *
 * Performance: O(n) where n = number of active entities
 * 68K Optimization: Backward iteration allows safe deletion during update
 */
void de_manager_update(de_manager *$)
{
    uint16_t i = $->size;

    while (i-- > $->pause_index)
    {
        de_entity e = $->items[i];
        de_state s = e->state;

        if (de_state_is_active(s))
        {
            s = s(e->data);

            if (!de_state_is_loop(s))
                e->state = s;
        }

        else if (de_state_is_paused(s))
            de_entity_pause(e);

        else if (de_state_is_deleted(s))
            de_entity_delete(e);
    }
}

/**
 * Pauses all entities.
 *
 * Performance: O(1) - Single index update
 */
void de_manager_pause(de_manager *$)
{
    $->pause_index = $->size;
}

/**
 * Resumes all entities.
 *
 * Performance: O(1) - Single index update
 */
void de_manager_resume(de_manager *$)
{
    $->pause_index = 0;
}

/**
 * Deletes all entities in the manager.
 *
 * Performance: O(n) where n = number of entities
 * 68K Optimization: Deletes from end to avoid array reorganization
 */
void de_manager_reset(de_manager *$)
{
    $->pause_index = 0;

    uint16_t remaining = $->size;
    while (remaining--)
        de_entity_delete($->items[$->size - 1]);
}

/**
 * Initializes a data system.
 * Prepares pool for sequential access.
 *
 * Performance: O(1) - Direct field assignments
 */
void de_system_init(de_system *$, void **storage, uint16_t capacity_groups, uint16_t params)
{
    $->pool = storage;
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
 *
 * Performance: O(n) worst case (search), O(params) for compaction
 * 68K Optimization: Uses first element as identifier for fast comparison
 */
uint16_t de_system_remove(de_system *$, void *first)
{
    uint16_t params = $->params;

    if (!params)
        return 0;

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
