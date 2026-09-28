/**
 * vsmap.h — Value-Slot Map
 *
 * A generic, target-agnostic handle table: a dense array of caller-owned pointers (`pool[]`), kept
 * gap-free by swap-and-pop, addressed indirectly through a stable `vsmap_handle_t` so the caller never
 * has to track where an element currently lives.
 *
 * ============================================================================
 * PORTABILITY / REQUIREMENTS -- READ THIS FIRST
 * ============================================================================
 *
 * vsmap.h has ZERO #include lines of its own. What's required of whoever includes it:
 *
 * 1. Fixed-width integer types must already be visible BEFORE this header is included. vsmap.h
 *    deliberately does NOT #include <stdint.h> itself -- the including project must provide `uint8_t`,
 *    `uint16_t` and `uint32_t` (whichever one VSMAP_HANDLE_T ends up being), whether via a plain
 *    `#include <stdint.h>` or whatever equivalent your target already provides them through.
 *
 * 2. The engine functions themselves use only C89-compatible constructs. There is no flexible array
 *    member here, since `pool[]` and `lookup[]` are ordinary, uniformly-typed arrays and the compiler
 *    already handles their alignment on its own; every function body keeps its declarations at the top
 *    of the block. Two things are still worth being precise about: this file is commented with `//`, a
 *    near-universally-supported convention rather than something ISO C89 itself defines -- a strict
 *    `-std=c89` build rejects `//` outright, while `-std=gnu89` (and effectively every real-world compiler)
 *    accepts it; and `inline` isn't in strict C89 either, so every function is declared through VSMAP_INLINE,
 *    which resolves to `static inline` on a C99-or-later compiler and plain `static` otherwise -- predefine
 *    VSMAP_INLINE yourself to override.
 *
 * 3. VSMAP_ALLOC() and VSMAP_BIND() are written as C99 compound literals for convenience -- they expand to
 *    `(vsmap_t){ ... }`, so they work anywhere a vsmap_t expression is legal (a declaration initializer, a
 *    plain reassignment, a function argument). If your compiler is C89-only and lacks compound literals,
 *    nothing about the engine itself depends on them: replace `vsmap_t map = VSMAP_ALLOC(...);` with a
 *    zero-initialized `vsmap_t map = {0};` followed by assigning `map.pool`, `map.lookup`, `map.capacity`
 *    by hand (`.free_head` and `.count` are set by vsmap_init(), so they don't need a manual value here).
 *
 * 4. CAPACITY must satisfy 1 <= CAPACITY <= max(vsmap_handle_t). One handle value (all-bits-set) is
 *    reserved as VSMAP_INVALID_HANDLE, but since issued handles only ever run 0 .. CAPACITY-1, a CAPACITY
 *    equal to the type's own maximum value is still safe -- the largest handle ever issued is then
 *    CAPACITY-1, one below the reserved sentinel, never equal to it. VSMAP_DECLARE() rejects CAPACITY == 0
 *    at compile time for free (the storage array's size expression collapses to -1), but can't check the
 *    upper bound without a second declaration. VSMAP_ALLOC() checks neither bound, since its CAPACITY is
 *    an ordinary runtime value. Exceeding the upper bound silently truncates the stored capacity at
 *    runtime rather than failing to compile.
 *
 * VSMAP_HANDLE_T defaults to uint16_t (up to 65535 live elements). Predefine it as uint8_t on an 8-bit
 * target with a small pool to shrink every handle, every `lookup[]` entry, and `vsmap_item_t.index` from
 * 2 bytes to 1.
 *
 * ============================================================================
 * Guarantees
 * ============================================================================
 *
 * A `vsmap_item_t` slot's address can and does change on every vsmap_remove() that isn't the last live
 * element: vsmap_remove() keeps the pool dense by *copying* the last live item into the vacated slot.
 * What stays stable is the *handle*: `VSMAP_DATA(map, h)` always resolves to wherever that element
 * currently lives, in O(1), for as long as `h` remains valid.
 *
 * Consequently: never hold a raw `vsmap_item_t *` (or a `&VSMAP_DATA(map, h)`) across any call that can
 * mutate the map (vsmap_add(), vsmap_remove(), or a VSMAP_FOREACH that does either) unless it's the entry
 * you are actively, atomically working with. Re-resolve through the handle afterward instead. The `void
 * *value` a slot stores is unaffected either way -- it's the caller's own pointer, opaque to vsmap.h, and
 * only the bookkeeping around it moves.
 *
 * There is no staleness/generation detection: once a handle is freed and reissued by a later vsmap_add(),
 * vsmap_valid() on your old copy of that handle returns true again -- for the new occupant, not the one
 * you originally got it for. Roll your own generation counter (in the caller-owned value, or by widening
 * this file yourself) if you need to tell those apart.
 */

#ifndef VSMAP_H
#define VSMAP_H

// vsmap.h does NOT #include anything -- not <stdint.h>, nothing. See "PORTABILITY / REQUIREMENTS" above.
// Make sure uint8_t/uint16_t/uint32_t are visible before this point.

// VSMAP_INLINE -- resolves to `static inline` on C99+ and to plain `static` on C89. Predefine yourself to
// override.
#ifndef VSMAP_INLINE
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define VSMAP_INLINE static inline
#else
#define VSMAP_INLINE static
#endif
#endif

// Width of every handle, index and count in this map. Default uint16_t; predefine as uint8_t for an
// 8-bit target with a small pool.
#ifndef VSMAP_HANDLE_T
#define VSMAP_HANDLE_T uint16_t
#endif
typedef VSMAP_HANDLE_T vsmap_handle_t;

// All-bits-set for whatever VSMAP_HANDLE_T resolves to -- not a hardcoded 0xFFFF. The inner cast forces
// the 0 (and the following bitwise-NOT, after the usual integer promotions) back down to vsmap_handle_t's
// own width before the outer cast is applied, which is what makes this produce the correct sentinel for
// any unsigned width, not only the one width a literal like 0xFFFFu happens to spell out.
#define VSMAP_INVALID_HANDLE ((vsmap_handle_t) ~(vsmap_handle_t)0)

typedef struct
{
    void *value;          // caller's pointer; opaque to us
    vsmap_handle_t index; // which `lookup` slot this pool entry currently represents
} vsmap_item_t;

typedef struct
{
    vsmap_item_t *pool;       // pool[0, count) — live elements, no gaps
    vsmap_handle_t *lookup;   // handle -> dense position, or (beyond the live range) a free-list link
    vsmap_handle_t capacity;  //
    vsmap_handle_t free_head; // head of the free list, or VSMAP_INVALID_HANDLE if empty
    vsmap_handle_t count;
} vsmap_t;

/* ============================================================================
 * PUBLIC API — all inline, always available, nothing to compile separately
 * ============================================================================ */

// Dynamic allocation.
//
//     vsmap_t map = VSMAP_ALLOC(malloc, 100);
//     vsmap_init(&map);
//     ...
//     VSMAP_FREE(free, &map);
//
// Does not handle allocation failure; validate .pool/.lookup before calling vsmap_init(). ALLOC is
// trusted to hand back memory suitably aligned for vsmap_item_t/vsmap_handle_t -- true for malloc()/
// calloc() on a hosted implementation, not guaranteed for a hand-rolled allocator on a freestanding target.
#define VSMAP_ALLOC(ALLOC, CAPACITY)                                              \
    (vsmap_t)                                                                     \
    {                                                                             \
        .pool = (vsmap_item_t *)(ALLOC)((CAPACITY) * sizeof(vsmap_item_t)),       \
        .lookup = (vsmap_handle_t *)(ALLOC)((CAPACITY) * sizeof(vsmap_handle_t)), \
        .capacity = (vsmap_handle_t)(CAPACITY),                                   \
        .free_head = VSMAP_INVALID_HANDLE,                                        \
        .count = 0,                                                               \
    }

// Frees the pool and lookup blocks previously allocated by VSMAP_ALLOC().
#define VSMAP_FREE(FREE, MAP)  \
    do                         \
    {                          \
        (FREE)((MAP)->pool);   \
        (FREE)((MAP)->lookup); \
    } while (0)

// Static allocation with automatic or static storage duration (stack or global).
//
//     VSMAP_DECLARE(storage, 100);
//     vsmap_t map = VSMAP_BIND(storage);
//     vsmap_init(&map);
//
// Expands to a single declaration, so it can be prefixed with `static` at file scope too. CAPACITY == 0
// is rejected at compile time (negative array size on `pool`).
#define VSMAP_DECLARE(NAME, CAPACITY)                    \
    struct                                               \
    {                                                    \
        vsmap_item_t pool[(CAPACITY) ? (CAPACITY) : -1]; \
        vsmap_handle_t lookup[(CAPACITY)];               \
    } NAME

// Static/global initialization: compile-time constants.
#define VSMAP_BIND(NAME)                                                                \
    (vsmap_t)                                                                           \
    {                                                                                   \
        .pool = (NAME).pool,                                                            \
        .lookup = (NAME).lookup,                                                        \
        .capacity = (vsmap_handle_t)(sizeof((NAME).lookup) / sizeof((NAME).lookup[0])), \
        .free_head = VSMAP_INVALID_HANDLE,                                              \
        .count = 0,                                                                     \
    }

#define VSMAP_DATA(MAP, HANDLE) ((MAP)->pool[(MAP)->lookup[(HANDLE)]])

// Visits every live value, from last to first (safe to vsmap_remove() the current ITEM's handle from
// inside CODE: when an item is removed, the last live item is copied into the vacated slot, and that
// last item has already been visited by the reverse traversal). Bound to `item` (a `vsmap_item_t *`)
// inside CODE; `item->value` is your pointer. Do not hold onto `item` past a CODE that removes or adds
// a *different* entry -- see "Guarantees" above.
#define VSMAP_FOREACH(MAP, CODE)                 \
    do                                           \
    {                                            \
        vsmap_handle_t _index = (MAP)->count;    \
        vsmap_item_t *_pool = (MAP)->pool;       \
                                                 \
        while (_index--)                         \
        {                                        \
            vsmap_item_t *item = &_pool[_index]; \
            CODE;                                \
        }                                        \
    } while (0)

// Must be called once after ALLOC/BIND, before the first vsmap_add(). Also doubles as a full reset: call
// it again any time to drop every element and start over (every handle issued before that call is
// no longer valid). A zero-capacity map remains empty and has no free handles.
VSMAP_INLINE void vsmap_init(vsmap_t *map)
{
    vsmap_handle_t i;

    map->count = 0;
    map->free_head = VSMAP_INVALID_HANDLE;

    if (!map->capacity)
        return;

    map->free_head = 0;

    for (i = 0; i < map->capacity; i++)
        map->lookup[i] = (vsmap_handle_t)(i + 1);

    map->lookup[map->capacity - 1] = VSMAP_INVALID_HANDLE;
}

// Adds `value`, returns its handle, or VSMAP_INVALID_HANDLE if the pool is full.
VSMAP_INLINE vsmap_handle_t vsmap_add(vsmap_t *map, void *value)
{
    if (map->count >= map->capacity || map->free_head == VSMAP_INVALID_HANDLE)
        return VSMAP_INVALID_HANDLE;

    vsmap_handle_t handle = map->free_head;
    map->free_head = map->lookup[handle];

    vsmap_handle_t slot = map->count++;
    map->pool[slot].value = value;
    map->pool[slot].index = handle;
    map->lookup[handle] = slot;

    return handle;
}

VSMAP_INLINE int vsmap_valid(vsmap_t *map, vsmap_handle_t handle)
{
    if (handle >= map->capacity)
        return 0;

    vsmap_handle_t slot = map->lookup[handle];

    return slot < map->count && map->pool[slot].index == handle;
}

// Removes a currently valid handle in O(1). The handle must refer to a live element; unlike
// vsmap_valid(), this function does not validate the handle before using it.
VSMAP_INLINE void vsmap_remove(vsmap_t *map, vsmap_handle_t handle)
{
    vsmap_handle_t slot = map->lookup[handle];
    vsmap_handle_t last = (vsmap_handle_t)(--map->count);

    if (slot != last)
    {
        map->pool[slot] = map->pool[last];
        map->lookup[map->pool[slot].index] = slot;
    }

    map->lookup[handle] = map->free_head;
    map->free_head = handle;
}

#endif // VSMAP_H
