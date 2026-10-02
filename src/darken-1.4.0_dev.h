/**
 * darken.h — Darken (DARKula ENgine) Entity System
 *
 * darken-1.4.0_dev
 *
 * Full documentation: README.Darken.md
 *
 * ============================================================================
 * PORTABILITY / REQUIREMENTS -- READ THIS FIRST
 * ============================================================================
 *
 * Darken is a generic, target-agnostic entity manager. Its logic operates only on caller-provided storage,
 * pointers, integer types, and callback functions. It does not depend on a specific platform, operating
 * environment, runtime, or toolchain.
 *
 * Requirements:
 *
 * 1. The fixed-width integer types used by the selected configuration must already be defined before this
 *    header is processed. Darken does not provide those type definitions itself.
 *
 * 2. The implementation must support the language features used by this header, including flexible array
 *    members, compound literals, designated initializers, and static inline functions.
 *
 * 3. Alignment and offset handling is implemented without compiler-specific alignment attributes or built-in
 *    alignment operators. Alignment for static storage is derived from the header layout, while runtime entity
 *    recovery uses the address of the data member as its offset.
 *
 *    The alignment calculation uses a complete header-shaped type without the flexible array member. For
 *    `struct { char c; TYPE t; }`, the difference between the wrapper size and TYPE's size represents the
 *    padding required before TYPE and therefore its alignment.
 *
 *    The entity recovery operation computes the byte offset of data[] from the start of the entity and
 *    subtracts that offset from the payload pointer. It performs address arithmetic only; it does not access
 *    an object through the null pointer that expresses the member address.
 *
 *    Offset calculations use `unsigned long`. No size-specific pointer or integer type is required by the
 *    engine for these operations.
 *
 * 4. CAPACITY must satisfy 1 <= CAPACITY <= max(darken_index_t), and the computed entity stride must fit in
 *    darken_index_t. These are API requirements. DARKEN_DECLARE() rejects CAPACITY == 0 through its array-size
 *    expression, while the remaining range requirements are the caller's responsibility. DARKEN_ALLOC()
 *    receives CAPACITY as a runtime expression and therefore performs no compile-time range validation.
 *    Values that exceed darken_index_t can be truncated when stored in the context.
 *
 * Pointer representation, struct alignment, integer representation, and endianness are determined by the
 * implementation on which the header is compiled. Darken does not impose fixed assumptions for them.
 *
 * STATE-MACHINE mode has one additional representation requirement: its sentinel values are represented
 * through the callback type and compared with `==` and `>`. The target must support that representation and
 * ordering consistently. DIRECT mode does not use callback-pointer sentinels and therefore avoids that
 * requirement.
 *
 *
 *
 * ============================================================================
 * CONFIGURATION -- all opt-in, all optional, all defaulted
 * ============================================================================
 *
 * The defaults provide the complete entity interface without imposing artificial feature limits. The
 * optional configuration macros let the caller trade range, storage usage, and callback behaviour according
 * to the needs of the application.
 *
 *   DARKEN_SMALL
 *       Convenience preset for smaller configurations. It defines only the default types that are not already
 *       defined:
 *           DARKEN_INDEX_T = uint8_t
 *           DARKEN_USR_T   = uint8_t
 *       It does not remove fields or change behaviour. The field-removal options and DARKEN_DIRECT remain
 *       explicit because they change the entity interface or update semantics. Any individual type macro
 *       defined by the caller takes precedence over the preset.
 *
 *   DARKEN_INDEX_T       default uint16_t
 *       Type of every internal index, capacity, size and stride. A smaller integer type reduces storage
 *       requirements when the required range permits it. Choose the smallest type that safely represents all
 *       required values.
 *
 *   DARKEN_USR_T         default uint16_t
 *       Type of the user-defined `usr` field. Choose a smaller type when the required value range permits it.
 *
 *   DARKEN_TAG_T         default uint32_t
 *       Type of the user-defined `tag` field. Choose a smaller type when the required value range permits it,
 *       or remove the field with DARKEN_NO_TAG when it is not needed.
 *
 *   DARKEN_NO_TAG
 *       Removes `tag` from the entity header entirely. Any code that accesses entity->tag then fails to compile.
 *
 *   DARKEN_NO_USR
 *       Removes `usr` from the entity header entirely. Any code that accesses entity->usr then fails to compile.
 *
 *   DARKEN_NO_DESTROY
 *       Removes the `destroy` callback field from the entity header and the corresponding destroy calls from
 *       darken_entity_delete(), darken_update() and darken_reset(). This reduces the per-entity header size
 *       and changes the lifecycle interface: without the field, deletion removes the entity without invoking
 *       a cleanup callback. Any code that assigns entity->destroy then fails to compile.
 *
 *   DARKEN_MIGRATE_WORD_T   default unsigned char
 *       Word size for bulk-copy entity bytes in darken_entity_migrate(). The default is unsigned char.
 *       A wider type can be selected as an optimization, but it must be suitable for raw memory access and
 *       must not require stricter alignment than the entity storage provides.
 *
 *       The default keeps the copy byte-wise. When widened, the selected type is used for the bulk portion
 *       of the copy and a byte loop handles the remaining tail.
 *
 *   DARKEN_DIRECT
 *       Switches the engine from state-machine mode (default) to direct-callback mode. See "Update /
 *       lifecycle control" below. It also selects a mode that does not rely on callback-pointer sentinels.
 *       default mode's function-pointer sentinels are unsafe.
 *
 * ----------------------------------------------------------------------------
 * ALIGNMENT CAVEAT for DARKEN_INDEX_T / DARKEN_USR_T / DARKEN_TAG_T
 * ----------------------------------------------------------------------------
 *
 * Each configurable integer field is placed in the entity header before the flexible array member `data[]`.
 * The layout relies on the payload beginning at an address compatible with the entity's alignment.
 *
 * The selected integer types must not impose an alignment requirement that is stricter than the alignment
 * represented by the entity header. The payload type used with DARKEN_DATA() must also fit that alignment
 * requirement. Configurations that violate these relationships can produce misaligned payload accesses.
 *
 *
 * ============================================================================
 * Entity: Base entity managed by the entity ctx
 * ============================================================================
 *
 * The entity structure serves as a container for user data with lifecycle management. The flexible array
 * member `data[]` allows entities to have variable-sized payloads while maintaining contiguous memory layout.
 *
 * The stride between entities is calculated once during ctx initialization and defines the spacing of entities
 * in the storage block. O(1) access by index comes from pool[] itself being a flat array of pointers; stride
 * does not participate in lookups after initialization.
 * any lookup after init.
 *
 * An entity's own memory address never moves after darken_init(). Reordering changes only the pointer stored
 * in darken.pool[]. A raw pointer into entity->data therefore remains valid while the entity changes position
 * within the pool or moves between active and free positions.
 *
 * The ctx itself also needs a stable address because darken_init() stores its address in every entity's ->owner.
 * The darken_t instance must therefore already be at its final address before darken_init() runs and must remain
 * there for as long as the ctx is used.
 *
 *
 * ============================================================================
 * Ctx: Entity container and lifecycle ctx.
 * ============================================================================
 *
 * Maintains the pointer array in two logical regions:
 *
 * Array Layout:
 *    [ active entities ][   free slots    ]
 *    0                 size               capacity
 *
 * The entities themselves live in the caller-provided storage block; ctx->pool contains pointers to those
 * fixed addresses.
 *
 * - Active region [0, size):
 *     Entity pointers visited by darken_update(). Iterable with DARKEN_FOREACH, which traverses from size-1
 *     down to 0. Entities are created with DARKEN_SPAWN() and removed by deletion.
 *
 * - Free region [size, capacity):
 *     Pointer slots not currently assigned to an active entity. DARKEN_SPAWN() takes its next entity from
 *     this region, and a deleted entity's pool slot is moved into this region.
 *
 * Neither darken_init(), nor DARKEN_SPAWN(), nor deletion initializes or clears update/destroy/tag/usr. An
 * entity handed out by DARKEN_SPAWN() may therefore contain data left in its storage from an earlier use of
 * that slot. Setting these fields to the values your entity actually needs is the caller's responsibility
 * on every spawn.
 *
 * ----------------------------------------------------------------------------
 * WARNING -- update == NULL is a hard crash, not a no-op
 * ----------------------------------------------------------------------------
 *
 * darken_update() calls every active entity's ->update unconditionally. If an entity is spawned without
 * assigning ->update before the next darken_update() call, the engine invokes a NULL function pointer.
 * There is no NULL check in the update path; assigning the callback is therefore the caller's responsibility.
 * Always assign ->update (and ->destroy, and any field you rely on) immediately after every DARKEN_SPAWN().
 *
 *
 * ============================================================================
 * Update / lifecycle control -- two selectable modes
 * ============================================================================
 *
 * STATE-MACHINE mode is the default. Define DARKEN_DIRECT before including this header to select direct
 * callback mode instead.
 *
 * Each mode has one fixed callback signature -- there is no separate configuration macro for the argument
 * list. The signature is chosen per mode to match how that mode is actually used: state-machine callbacks
 * rarely need the entity handle, since the return value drives the lifecycle; direct-mode callbacks almost
 * always need it, since they call darken_entity_delete() themselves.
 *
 * 1) STATE-MACHINE mode -- default
 * ---------------------------------------------------------
 *     Signature: darken_state_t callback(void *data)
 *
 *     Only the entity's payload is passed -- never the entity handle. darken_update() reads update()'s return
 *     value and drives the lifecycle itself:
 *
 *         DARKEN_CONTINUE: stay active, keep the same update callback
 *         DARKEN_DELETE:   call destroy (if set), then delete the entity
 *         (anything else): treated as a new update callback and installed as entity->update for the next frame
 *
 *         void *player_walk_state(struct player *data)
 *         {
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
 *     `destroy` uses the same callback type and the same argument convention as `update`. Its return value is
 *     always ignored; darken_reset() and darken_entity_delete() call it only for its side effects.
 *     (Omitted entirely when DARKEN_NO_DESTROY is defined.)
 *
 *     `destroy` must not modify the ctx's active/free regions. Deleting, spawning, or otherwise reordering
 *     entities from inside a destroy callback will invalidate the pool state used by the current operation.
 *     and the DARKEN_DELETE branch inside darken_update().
 *
 *     Need the entity handle anyway (for example, to read or write usr or tag)? Recover it with
 *     DARKEN_ENTITY(data).
 *
 *     Comparing a darken_state_t value against the sentinels with `==` and `>` relies on the callback pointer
 *     representation supporting the sentinel values and their ordering. DIRECT mode does not use this mechanism.
 *
 * 2) DIRECT mode -- DARKEN_DIRECT defined
 * ---------------------------------------------------------
 *     Signature: void callback(darken_entity_t entity)
 *                void callback(darken_entity_t entity, void *data)
 *
 *     Both the entity handle and its payload are passed, in that order. darken_update() calls
 *     entity->update(entity, entity->data) every frame and ignores any return value; entity->destroy(entity,
 *     entity->data) is called the same way by darken_reset() and darken_entity_delete(). The callback controls
 *     the entity's lifecycle by assigning directly to entity->update and/or entity->destroy, and it deletes
 *     itself by calling darken_entity_delete().
 *
 *
 *         void player_walk_state(darken_entity_t entity) {
 *             DARKEN_DATA(struct player, data, entity);
 *             data->x++;
 *         }
 *
 *         void player_walk_events_state(darken_entity_t entity, struct player *data) {
 *             data->x++;
 *
 *             if (should_stop(data))
 *                 entity->update = player_stop_state;
 *
 *             if (should_die(data))
 *                 darken_entity_delete(entity);
 *         }
 *
// A callback may declare only the entity parameter, or both the entity and payload parameters. The callback
// type is intentionally unprototyped, and the calling convention must support the selected argument form.
// Declaring both parameters explicitly is the portable choice when argument-count mismatches are not supported
// by the target calling convention.
 */

#ifndef DARKEN_H
#define DARKEN_H

// Darken does not provide external declarations. The fixed-width integer types selected by the
// configuration must already be defined before this header is processed. Offset and alignment handling
// are implemented locally by the macros below.

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

// DARKEN_SMALL: single-switch preset for 8-bit targets. Expands only into macros that aren't already
// defined, so any individual override you set yourself wins. It changes ONLY types -- no field is removed,
// no behaviour changes -- so any code that compiles against the defaults still compiles against DARKEN_SMALL,
// just with a smaller per-entity footprint. The field-removal knobs (DARKEN_NO_TAG, DARKEN_NO_USR,
// DARKEN_NO_DESTROY) and the mode switch (DARKEN_DIRECT) stay explicit opt-ins because each one is a
// semantic change that can break existing code.
#ifdef DARKEN_SMALL
#ifndef DARKEN_INDEX_T
#define DARKEN_INDEX_T uint8_t
#endif
#ifndef DARKEN_USR_T
#define DARKEN_USR_T uint8_t
#endif
#endif

// darken_index_t -- internal index/capacity/size/stride type. Default uint16_t.
#ifndef DARKEN_INDEX_T
#define DARKEN_INDEX_T uint16_t
#endif
typedef DARKEN_INDEX_T darken_index_t;

// usr -- user-defined field.
#ifdef DARKEN_NO_USR
#define _DARKEN_USR_DECL
#else
#ifndef DARKEN_USR_T
#define DARKEN_USR_T uint16_t
#endif
#define _DARKEN_USR_DECL DARKEN_USR_T usr;
#endif

// tag -- user-defined identification/categorization field.
#ifdef DARKEN_NO_TAG
#define _DARKEN_TAG_DECL
#else
#ifndef DARKEN_TAG_T
#define DARKEN_TAG_T uint32_t
#endif
#define _DARKEN_TAG_DECL DARKEN_TAG_T tag;
#endif

// destroy -- optional cleanup callback.
#ifdef DARKEN_NO_DESTROY
#define _DARKEN_DESTROY_DECL
#else
#define _DARKEN_DESTROY_DECL darken_state_t destroy;
#endif

// Callback type. Return type differs per mode.
#ifdef DARKEN_DIRECT
typedef void (*darken_state_t)();
#else
typedef void *(*darken_state_t)();
#endif

typedef struct darken_entity_t *darken_entity_t;

typedef struct darken_t
{
    darken_entity_t *pool;  // Pointer array to entities in the ctx's storage block
    unsigned char *storage; // Pointer to the contiguous memory block where entities are allocated
    darken_index_t capacity;
    darken_index_t size;
    darken_index_t stride;
} darken_t;

// _DARKEN_USR_DECL / _DARKEN_DESTROY_DECL / _DARKEN_TAG_DECL each include their own trailing `;`
// when the field they guard is present, and expand to nothing when the corresponding DARKEN_NO_* option is
// defined. None of the three is followed by a `;` at either use site below.
struct darken_entity_t
{
    darken_index_t slot;   // Private: Index in the ctx's pool array
    _DARKEN_USR_DECL       // User-defined field for custom data
    darken_state_t update; // User-defined update callback
    _DARKEN_DESTROY_DECL   // User-defined destroy callback
    _DARKEN_TAG_DECL       // User-defined tag for identification or categorization
    darken_t *owner;       // Private: Pointer to the owning ctx
    unsigned char data[];  // Payload
};

/* ============================================================================
 * PRIVATE
 * ============================================================================ */

// Mirrors every fixed member of struct darken_entity_t, but has no flexible array member of its own.
// It is used only as a complete header-shaped type whose size and alignment can be queried. Nothing is stored
// through this type. Its member list must remain identical to the fixed members of darken_entity_t, including
// the fields controlled by the DARKEN_NO_* options. If a fixed member is added to darken_entity_t, add the
// same member here.
struct _darken_hdr_shape_t
{
    darken_index_t slot;
    _DARKEN_USR_DECL
    darken_state_t update;
    _DARKEN_DESTROY_DECL
    _DARKEN_TAG_DECL
    darken_t *owner;
};

// Computes the byte offset of MEMBER from the start of TYPE using its member-address expression.
// It is used only where a runtime offset is required. The result is not used for compile-time array sizing.
#define _DARKEN_OFFSETOF(TYPE, MEMBER) ((unsigned long)&((TYPE *)0)->MEMBER)

// Computes an alignment value from sizeof() alone. For `struct { char c; TYPE t; }`, the difference
// between the wrapper size and TYPE's size represents the padding required before TYPE and therefore its
// alignment. TYPE must be a complete type and must not contain a flexible array member. The result is used
// where DARKEN_DECLARE() requires an array-size value known during declaration.
#define _DARKEN_ALIGNOF(TYPE) (sizeof(struct { char _darken_c; TYPE _darken_m; }) - sizeof(TYPE))

// The alignment requirement of struct darken_entity_t, obtained from the fixed header shape above.
#define _DARKEN_ENTITY_ALIGN _DARKEN_ALIGNOF(struct _darken_hdr_shape_t)

// Single call-site helper for invoking update()/destroy(), used everywhere the engine calls into user code.
// The argument list is fixed per mode (see the big comment above), so there is nothing to configure here.
#ifdef DARKEN_DIRECT
#define _DARKEN_ARGS(ENTITY) (ENTITY), (ENTITY)->data
#else
#define _DARKEN_ARGS(ENTITY) (ENTITY)->data
#endif

// Conditionally invokes destroy(), or expands to an expression that consumes ENTITY when
// DARKEN_NO_DESTROY is defined. Keeping this logic in one macro avoids placing conditional preprocessing
// inside DARKEN_FOREACH() arguments. The disabled branch evaluates ENTITY so the loop variable remains
// referenced when destroy support is disabled.
#ifdef DARKEN_NO_DESTROY
#define _DARKEN_MAYBE_DESTROY(ENTITY) ((void)(ENTITY))
#else
#define _DARKEN_MAYBE_DESTROY(ENTITY) \
    if ((ENTITY)->destroy)            \
        (ENTITY)->destroy(_DARKEN_ARGS(ENTITY));
#endif

// Rounds (header + payload) up to the next multiple of the entity's required alignment using unsigned long
// arithmetic. The alignment value is expected to be a power of two.
#define _DARKEN_ENTITY_STRIDE(PAYLOAD) (((sizeof(struct darken_entity_t) + (PAYLOAD)) + (unsigned long)_DARKEN_ENTITY_ALIGN - 1) & ~((unsigned long)_DARKEN_ENTITY_ALIGN - 1))

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

#ifndef DARKEN_DIRECT
// Sentinel return values for update() callbacks in state-machine mode.
// Any value greater than DARKEN_CONTINUE is treated as the next update callback.
#define DARKEN_CONTINUE ((darken_state_t)1)
#define DARKEN_DELETE ((darken_state_t)0)
#endif

// Word size for bulk-copy an entity's bytes in darken_entity_migrate(). The default is unsigned char.
// A wider type can be selected as an optimization, but the selected type must be valid for raw memory access
// and must not require stricter alignment than the entity storage provides.
// The engine does not depend on a particular word size for correctness.
#ifndef DARKEN_MIGRATE_WORD_T
#define DARKEN_MIGRATE_WORD_T unsigned char
#endif

// Dynamic allocation: use with a caller-provided allocation function
//     darken_t m = DARKEN_ALLOC(malloc, 5, sizeof(struct MyComponent));
//     if (!m.pool || !m.storage) return;
//     darken_init(&m);
//     ...
//     DARKEN_FREE(free, &m);
//
// DARKEN_ALLOC() does not handle allocation failure or partial allocation cleanup.
// CAPACITY must satisfy 1 <= CAPACITY <= max(darken_index_t), and the computed stride must fit in
// darken_index_t. Unlike DARKEN_DECLARE(), DARKEN_ALLOC() receives CAPACITY as a runtime expression and
// performs no compile-time range validation. Values that exceed darken_index_t can be truncated when stored
// in the context.
//
// ALLOC must return memory aligned for the entity storage. A custom allocator must provide at least
// _DARKEN_ENTITY_ALIGN alignment for the returned pool and storage blocks.
#define DARKEN_ALLOC(ALLOC, CAPACITY, PAYLOAD)                           \
    (darken_t)                                                           \
    {                                                                    \
        .pool = (ALLOC)((CAPACITY) * sizeof(darken_entity_t)),           \
        .storage = (ALLOC)((CAPACITY) * _DARKEN_ENTITY_STRIDE(PAYLOAD)), \
        .capacity = (darken_index_t)(CAPACITY),                          \
        .stride = (darken_index_t)_DARKEN_ENTITY_STRIDE(PAYLOAD),        \
    }

// Frees the pool and storage blocks previously allocated by DARKEN_ALLOC().
// DARKEN_FREE() only releases memory; it does not call destroy() or reset the ctx.
#define DARKEN_FREE(FREE, CTX)  \
    do                          \
    {                           \
        (FREE)((CTX)->pool);    \
        (FREE)((CTX)->storage); \
    } while (0)

// Static storage declaration (stack or global)
//     DARKEN_DECLARE(storage, 5, sizeof(struct MyComponent));
//     static DARKEN_DECLARE(storage, 5, sizeof(struct MyComponent));
//     darken_t m = DARKEN_BIND(storage);
//     darken_init(&m);
//
// CAPACITY must satisfy 1 <= CAPACITY <= max(darken_index_t), and the computed stride must fit in
// darken_index_t. These are API requirements; the caller is responsible for keeping both values in range.
// A CAPACITY or stride that exceeds darken_index_t can be truncated when stored in the context.
//
// The payload type used with DARKEN_DATA() must not require stricter alignment than struct darken_entity_t.
//
// The byte block sits inside a union alongside a _darken_hdr_shape_t member so the block itself receives at
// least _DARKEN_ENTITY_ALIGN alignment. The first byte of the block is aligned, and each following entity is
// placed at a stride that is a multiple of that alignment.
//
// DARKEN_DECLARE() expands to a single declaration and can therefore be used in the supported declaration
// contexts without auxiliary declarations or helper statements.
// one-declaration-only contexts.
//
// The raw storage block declared by DARKEN_DECLARE() is available as `.block.bytes`. Use DARKEN_BIND() or
// DARKEN_INIT() to build the corresponding darken_t context.
#define DARKEN_DECLARE(NAME, CAPACITY, PAYLOAD)                               \
    struct                                                                    \
    {                                                                         \
        darken_index_t capacity;                                              \
        darken_index_t stride;                                                \
        darken_entity_t pool[(CAPACITY) ? (CAPACITY) : -1];                   \
        union                                                                 \
        {                                                                     \
            struct _darken_hdr_shape_t _darken_align;                         \
            unsigned char bytes[(CAPACITY) * _DARKEN_ENTITY_STRIDE(PAYLOAD)]; \
        } block;                                                              \
    } NAME = {                                                                \
        .capacity = (darken_index_t)(CAPACITY),                               \
        .stride = (darken_index_t)_DARKEN_ENTITY_STRIDE(PAYLOAD),             \
    }

// Static/global initialization. DARKEN_INIT() builds a darken_t value from the storage declaration and is
// intended for direct initialization of a context.
#define DARKEN_INIT(STORAGE)                                                                                            \
    (darken_t)                                                                                                          \
    {                                                                                                                   \
        .pool = (STORAGE).pool,                                                                                         \
        .storage = (STORAGE).block.bytes,                                                                               \
        .capacity = (darken_index_t)(sizeof((STORAGE).pool) / sizeof(darken_entity_t)),                                 \
        .stride = (darken_index_t)(sizeof((STORAGE).block.bytes) / (sizeof((STORAGE).pool) / sizeof(darken_entity_t))), \
    }

// Runtime binding: locals, reassignment, any context.
#define DARKEN_BIND(NAME)              \
    (darken_t)                         \
    {                                  \
        .pool = (NAME).pool,           \
        .storage = (NAME).block.bytes, \
        .capacity = (NAME).capacity,   \
        .stride = (NAME).stride,       \
    }

// The returned entity may contain data left in its storage from an earlier use of that slot. Always initialize
// every field you rely on (update, destroy, tag, usr, and data). In particular, assign ->update before the next
// darken_update() call, because the engine invokes it unconditionally.
#define DARKEN_SPAWN(CTX) ((CTX)->size < (CTX)->capacity ? (CTX)->pool[(CTX)->size++] : 0)

// The index-based form keeps the loop state in an integer index and accesses the pool through its pointer array.
// It avoids a second traversal pointer and keeps the iteration logic independent of pointer representation.
//
// Deleting the currently visited entity from inside CODE is safe and cheap: it swaps with the last active
// slot, which the loop has already passed.
//
// Deleting a *different* entity from inside CODE is subtler. If the deleted entity's slot is greater than
// the current loop index, the swap moves a slot the loop already visited into the now-empty position, and
// that entity is NOT revisited this frame -- correct. If the deleted entity's slot is LOWER than the
// current loop index, the entity that was at size-1 gets moved into a position the loop hasn't reached
// yet, and will be visited a second time this same frame. That is not a bug, but it can double-invoke an
// update callback within one frame for the moved entity. Callbacks that are idempotent per frame are
// unaffected; callbacks that advance internal timers will see that entity advance twice.
#define DARKEN_FOREACH(CTX, CODE)                    \
    do                                               \
    {                                                \
        darken_index_t _index = (CTX)->size;         \
        darken_entity_t *_pool = (CTX)->pool;        \
                                                     \
        while (_index--)                             \
        {                                            \
            darken_entity_t _entity = _pool[_index]; \
            CODE;                                    \
        }                                            \
    } while (0)

// Declare a typed pointer to an entity's data payload
#define DARKEN_DATA(TYPE, VAR, ENTITY) TYPE *VAR = (TYPE *)(ENTITY)->data;

// Recover the entity handle from a pointer to its data payload (mostly useful in STATE-MACHINE mode, where
// callbacks only receive data).
// DATA must point to the beginning of an entity's data[] payload.
// Uses _DARKEN_OFFSETOF() to obtain the payload offset without requiring an additional dependency.
#define DARKEN_ENTITY(DATA) ((darken_entity_t)((unsigned char *)(DATA) - _DARKEN_OFFSETOF(struct darken_entity_t, data)))

// Activity-state tests.
// Note: ENTITY is evaluated multiple times per test. Do not pass expressions with side effects.
#define DARKEN_ENTITY_IS_ACTIVE(ENTITY) ((ENTITY)->slot < (ENTITY)->owner->size)
#define DARKEN_ENTITY_IS_FREE(ENTITY) (!DARKEN_ENTITY_IS_ACTIVE(ENTITY))

// Active/free counts.
#define DARKEN_COUNT_ACTIVE(CTX) ((CTX)->size)
#define DARKEN_COUNT_FREE(CTX) ((darken_index_t)((CTX)->capacity - (CTX)->size))

/* ============================================================================
 * FUNCTIONS
 * ============================================================================ */

// Swap two entities in their owners' pool arrays. The entities may belong to the same ctx or to different
// ctx's. Each entity is exchanged with the pool slot it currently occupies in its own owner, then both owner
// pointers and slot indices are exchanged with the pool entries. This keeps ->owner and ->slot consistent
// when the two entities cross ctx boundaries.
//
// Takes the entities themselves rather than (ctx, i, j), so the ctx and slot are obtained from the entities
// being moved.
static inline void darken_entity_swap(darken_entity_t e1, darken_entity_t e2)
{
    if (e1 == e2)
        return;

    darken_t *ctx1 = e1->owner;
    darken_t *ctx2 = e2->owner;
    darken_index_t i = e1->slot;
    darken_index_t j = e2->slot;

    ctx1->pool[i] = e2;
    ctx2->pool[j] = e1;
    e1->slot = j;
    e2->slot = i;
    e1->owner = ctx2;
    e2->owner = ctx1;
}

// Delete an active entity, calling destroy() first if one is set and DARKEN_NO_DESTROY is not defined.
// destroy() must not modify the ctx's active/free regions by deleting, spawning, or reordering entities while
// it runs.
static inline void darken_entity_delete(darken_entity_t entity)
{
    if (DARKEN_ENTITY_IS_FREE(entity))
        return;

    _DARKEN_MAYBE_DESTROY(entity);
    darken_entity_swap(entity, entity->owner->pool[--entity->owner->size]);
}

// Migrate `entity` from its current ctx into `dst`, WITHOUT calling destroy().
// Returns the entity as it now lives in `dst`, or 0 if the transfer cannot be performed.
//
// `src` and `dst` may use different strides. Exactly min(src->stride, dst->stride) bytes are copied.
// If dst is smaller, excess payload data is truncated; if dst is larger, the remaining payload is unchanged.
// The common header fields are preserved, except slot and owner, which are updated for `dst`.
//
// If `entity` is active, it is removed from `src` after the copy. If it is free, its source slot remains free.
// This is not a no-op when entity is free: dst still gains a new active entity, built from the bytes currently
// present in the source storage. Passing a free entity by mistake therefore consumes a slot in dst.
// silently spend a slot in dst with garbage data, not return early.
//
// The copy runs in DARKEN_MIGRATE_WORD_T-sized chunks with a byte tail for the remainder. Division and
// modulo against sizeof(DARKEN_MIGRATE_WORD_T) keep the copy correct for the selected word size.
// The default word type is unsigned char.
//
// The copied blocks are aligned because the storage base is aligned to _DARKEN_ENTITY_ALIGN and each entity
// begins at a stride that is a multiple of that alignment. If DARKEN_MIGRATE_WORD_T is widened, its alignment
// requirement must not exceed _DARKEN_ENTITY_ALIGN.
static inline darken_entity_t darken_entity_migrate(darken_entity_t entity, darken_t *dst)
{
    darken_t *src = entity->owner;

    if (src == dst || dst->size >= dst->capacity)
        return 0;

    darken_index_t count = src->stride < dst->stride ? src->stride : dst->stride;
    darken_index_t words = (darken_index_t)(count / sizeof(DARKEN_MIGRATE_WORD_T));
    darken_index_t dst_slot = dst->size++;
    darken_entity_t moved = dst->pool[dst_slot];
    DARKEN_MIGRATE_WORD_T *d = (DARKEN_MIGRATE_WORD_T *)moved;
    DARKEN_MIGRATE_WORD_T *s = (DARKEN_MIGRATE_WORD_T *)entity;

    while (words--)
        *d++ = *s++;

    unsigned char *sb = (unsigned char *)s;
    unsigned char *db = (unsigned char *)d;

    count = (darken_index_t)(count % sizeof(DARKEN_MIGRATE_WORD_T));
    while (count--)
        *db++ = *sb++;

    moved->slot = dst_slot;
    moved->owner = dst;

    if (DARKEN_ENTITY_IS_ACTIVE(entity))
        darken_entity_swap(entity, src->pool[--src->size]);

    return moved;
}

//
// USAGE EXAMPLES:
//
// DYNAMIC:
//     darken_t m = DARKEN_ALLOC(malloc, 5, sizeof(struct MyComponent));
//     if (!m.pool || !m.storage) return;
//     darken_init(&m);
//     ...
//     darken_reset(&m);
//     DARKEN_FREE(free, &m);
//
// STATIC (Runtime binding):
//     DARKEN_DECLARE(storage, 5, sizeof(struct MyComponent));
//     darken_t m = DARKEN_BIND(storage);
//     darken_init(&m);
//
// STATIC (file-scope, internal linkage):
//     static DARKEN_DECLARE(storage, 5, sizeof(struct MyComponent));
//     static darken_t world = DARKEN_INIT(storage);
//
//     void init_world() {
//         darken_init(&world);
//         ...
//     }
//
// SMALL CONFIGURATION:
//     #define DARKEN_SMALL
//     #include "darken.h"
//     static DARKEN_DECLARE(storage, 32, sizeof(struct bullet));
//     static darken_t world = DARKEN_INIT(storage);
//
//     DARKEN_SMALL reduces the default width of selected fields without removing them. Additional
//     DARKEN_NO_* options can remove individual fields when their interface is not needed.
//
// darken_init() must only be called on unused or uninitialized storage, or after darken_reset() when the
// current population is intentionally being discarded. It does not call destroy() for entities already managed
// by the ctx.
//
// Walks the capacity-sized storage block once, assigning each pool slot a permanent entity address. The loop
// counts down (capacity-1 to 0). The iteration direction is not significant and does not imply that slot i
// corresponds to storage offset i*stride. Only ->slot and ->owner, not array position, are guaranteed to track
// an entity afterward.
static inline void darken_init(darken_t *ctx)
{
    ctx->size = 0;
    darken_index_t i = ctx->capacity;
    unsigned char *storage = ctx->storage;

    while (i--)
    {
        ctx->pool[i] = (darken_entity_t)storage;
        ctx->pool[i]->owner = ctx;
        ctx->pool[i]->slot = i;

        storage += ctx->stride;
    }
}

static inline void darken_update(darken_t *ctx)
{
#ifdef DARKEN_DIRECT
    DARKEN_FOREACH(ctx, _entity->update(_DARKEN_ARGS(_entity)));
#else
    DARKEN_FOREACH(ctx, {
        darken_state_t state = _entity->update(_DARKEN_ARGS(_entity));

        if (state == DARKEN_CONTINUE)
            continue;

        if (state > DARKEN_CONTINUE)
        {
            _entity->update = state;
            continue;
        }

        _DARKEN_MAYBE_DESTROY(_entity);
        darken_entity_swap(_entity, ctx->pool[--ctx->size]);
    });
#endif
}

// Calls destroy() on every currently active entity, then drops the whole pool back to the free region
// (size = 0).
//
// This does not re-run darken_init(), so ->slot, ->owner, and the pool mapping to storage remain unchanged.
// Entities spawned after the reset reuse the current free-region position and may contain data left in their
// storage. Initialize every field required by each newly spawned entity.
//
// destroy() callbacks used by darken_reset() must not modify the ctx's active/free regions by deleting, spawning,
// or otherwise reordering entities during the reset iteration. This is the same restriction documented for
// destroy() above.
//
// An optimizing implementation may remove the traversal when DARKEN_NO_DESTROY makes its loop body
// side-effect free. This is an optimization rather than a behavioural requirement, so code must not depend on it.
static inline void darken_reset(darken_t *ctx)
{
    DARKEN_FOREACH(ctx, { _DARKEN_MAYBE_DESTROY(_entity); });
    ctx->size = 0;
}

#endif // DARKEN_H
