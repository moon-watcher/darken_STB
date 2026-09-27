/**
 * dsmap.h — Dense Slot Map
 *
 * A generic, target-agnostic handle table over a fixed, typed storage block: `dsmap_alloc()` hands out a
 * `dsmap_handle_t` bound to one specific, permanent element address for the lifetime of the map;
 * `dsmap_remove()` keeps a *second*, purely internal pointer cache densely packed for O(1) iteration,
 * without ever moving or copying the element data itself. Third sibling of darken.h and vsmap.h --
 * DSMAP_FOREACH's reverse-iteration-free, swap-with-last bookkeeping and the free-handle-recycling idea
 * are the same family of trick, applied a third way. See "How this differs from darken.h and vsmap.h"
 * below for exactly how.
 *
 * ============================================================================
 * PORTABILITY / REQUIREMENTS -- READ THIS FIRST
 * ============================================================================
 *
 * dsmap.h has ZERO #include lines of its own. What's required of whoever includes it:
 *
 * 1. Fixed-width integer types must already be visible BEFORE this header is included. dsmap.h
 *    deliberately does NOT #include <stdint.h> itself -- the including project must provide `uint8_t`,
 *    `uint16_t` and `uint32_t` (whichever one DSMAP_HANDLE_T ends up being), whether via a plain
 *    `#include <stdint.h>` or whatever equivalent your target already provides them through.
 *
 * 2. Standard C89 for the engine itself. Like vsmap.h and unlike darken.h, dsmap.h needs no C99
 *    *language* extension at all -- `pool` is a plain, uniformly-typed array of the caller's element type
 *    (via DSMAP_DECLARE) or a raw block sized to `capacity * sizeof(element)` (via DSMAP_ALLOC), and
 *    since sizeof(TYPE) is always a multiple of TYPE's own alignment for any complete type, every element
 *    at `pool + i * size` lands correctly aligned automatically -- no header fields are interleaved with
 *    the payload the way darken.h's entities interleave bookkeeping and data, so there's no union/
 *    alignment trick to build here at all. Every function body keeps its declarations at the top of the
 *    block, and no `for (TYPE i = 0; ...)` C99-style loop declarations remain. Two things are still worth
 *    being precise about, exactly as for its siblings: this file is commented with `//`, a near-
 *    universally-supported convention rather than something ISO C89 itself defines; and `inline` isn't in
 *    strict C89 either, so every function is declared through DSMAP_INLINE, which resolves to `static
 *    inline` on a C99-or-later compiler and plain `static` otherwise -- predefine DSMAP_INLINE yourself to
 *    override.
 *
 * 3. DSMAP_ALLOC() and DSMAP_BIND() are written as C99 compound literals for convenience -- they expand
 *    to `(dsmap_t){ ... }`, so they work anywhere a dsmap_t expression is legal (a declaration initializer,
 *    a plain reassignment, a function argument). If your compiler is C89-only and lacks compound literals,
 *    nothing about the engine itself depends on them: replace `dsmap_t map = DSMAP_ALLOC(...);` with a
 *    zero-initialized `dsmap_t map = {0};` followed by assigning `map.pool`, `map.ptrs`, `map.addrs`,
 *    `map.lookup`, `map.handles`, `map.capacity`, `map.size` by hand (`.count` is set by dsmap_init(), so
 *    it doesn't need a manual value here).
 *
 * 4. CAPACITY must satisfy 1 <= CAPACITY <= max(dsmap_handle_t). One handle value (all-bits-set) is
 *    reserved as DSMAP_INVALID_HANDLE, but since issued handles only ever take values 0 .. CAPACITY-1, a
 *    CAPACITY equal to the type's own maximum value is still safe -- verified for both the default
 *    uint16_t and an overridden uint8_t width. DSMAP_DECLARE() rejects CAPACITY == 0 at compile time for
 *    free (the `pool` array's size expression collapses to -1), but can't check the upper bound without a
 *    second declaration. DSMAP_ALLOC() checks neither bound, since its CAPACITY is an ordinary runtime
 *    value. Exceeding the upper bound silently truncates the stored capacity at runtime.
 *
 *    `SIZE` (the per-element byte size, typically `sizeof(YourType)`) is a completely separate dimension
 *    from CAPACITY and is deliberately NOT tied to DSMAP_HANDLE_T: on an 8-bit target you might well want
 *    a `uint8_t` handle (a pool of, say, 40 elements) while `sizeof(YourType)` is comfortably over 255
 *    bytes. `size` stays `uint16_t` regardless of DSMAP_HANDLE_T for exactly this reason.
 *
 * DSMAP_HANDLE_T defaults to uint16_t (up to 65535 live elements). Predefine it as uint8_t on an 8-bit
 * target with a small pool to shrink every handle and every `lookup[]`/`handles[]` entry from 2 bytes to 1.
 *
 * ============================================================================
 * How this differs from darken.h and vsmap.h -- read this if you already know either header
 * ============================================================================
 *
 * All three headers solve "dense iteration over a shrinking/growing set of live things," and all three
 * use some flavor of swap-with-last to keep deletion O(1). They differ in what, exactly, is allowed to
 * move:
 *
 *   - darken.h: the entity's own address never moves; what gets reordered is a *pointer* to it. The
 *     caller addresses entities directly by that (never-changing) pointer.
 *   - vsmap.h: nothing is addressed by pointer at all across a mutation -- `vsmap_remove()` keeps the pool
 *     dense by *copying* the last live item's bytes into the vacated slot. The caller addresses elements
 *     by handle, resolved fresh through `VSMAP_DATA()` every time; holding a raw pointer across a mutating
 *     call is unsafe.
 *   - dsmap.h: splits the difference. `addrs[handle]` is a permanent, write-once-at-init address that
 *     never changes for the lifetime of the map -- `DSMAP_DATA(map, handle)` always returns the same
 *     pointer for a given handle, for as long as that handle stays valid, exactly like darken.h's
 *     address-stability guarantee, just keyed by handle instead of by pointer. Element bytes are never
 *     copied on removal, unlike vsmap.h. What DOES get reordered on removal is a second, purely internal
 *     array (`ptrs[]`, mirrored by `handles[]`/`lookup[]`) used only to give DSMAP_FOREACH O(1) dense
 *     iteration -- the public API never exposes this reordering, so a raw pointer obtained from
 *     `DSMAP_DATA()` (unlike one obtained mid-iteration from DSMAP_FOREACH's `data`, which is only valid
 *     for that one iteration step) stays good until that handle itself is removed.
 *
 * Like both siblings, dsmap.h has NO staleness/generation detection: once a handle is freed and a later
 * dsmap_alloc() reissues that same value, dsmap_valid() on your old copy returns true again -- for the new
 * occupant, not the one you originally got it for.
 */

#ifndef DSMAP_H
#define DSMAP_H

// dsmap.h does NOT #include anything -- not <stdint.h>, nothing. See "PORTABILITY / REQUIREMENTS" above.
// Make sure uint8_t/uint16_t/uint32_t are visible before this point.

// DSMAP_INLINE -- resolves to `static inline` on C99+ and to plain `static` on C89. Predefine yourself to
// override.
#ifndef DSMAP_INLINE
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define DSMAP_INLINE static inline
#else
#define DSMAP_INLINE static
#endif
#endif

// Width of every handle, index and count in this map. Default uint16_t; predefine as uint8_t for an
// 8-bit target with a small pool. Does NOT affect `size` (the per-element byte count) -- see item 4 of
// the PORTABILITY note above.
#ifndef DSMAP_HANDLE_T
#define DSMAP_HANDLE_T uint16_t
#endif
typedef DSMAP_HANDLE_T dsmap_handle_t;

// All-bits-set for whatever DSMAP_HANDLE_T resolves to -- not a hardcoded 0xFFFF. The inner cast forces
// the 0 (and the following bitwise-NOT, after the usual integer promotions) back down to dsmap_handle_t's
// own width before the outer cast is applied, which is what makes this produce the correct sentinel for
// any unsigned width, not only the one width a literal like 0xFFFFu happens to spell out.
#define DSMAP_INVALID_HANDLE ((dsmap_handle_t) ~(dsmap_handle_t)0)

typedef struct
{
    char *pool;              // capacity * size bytes, one element every `size` bytes
    char **ptrs;             // internal-iteration-order cache; see the big comment above
    char **addrs;            // addrs[h] = the permanent address of handle h's element, set once at init
    dsmap_handle_t *lookup;  // handle -> current dense position
    dsmap_handle_t *handles; // dense position -> handle, and (beyond count) the free-handle order
    dsmap_handle_t capacity; //
    uint16_t size;           // bytes per element -- always uint16_t; see item 4 of the PORTABILITY note above
    dsmap_handle_t count;
} dsmap_t;

/* ============================================================================
 * PUBLIC API — all inline, always available, nothing to compile separately
 * ============================================================================ */

// Dynamic allocation.
//
//     dsmap_t map = DSMAP_ALLOC(malloc, 100, sizeof(struct MyComponent));
//     dsmap_init(&map);
//     ...
//     DSMAP_FREE(free, &map);
//
// Does not handle allocation failure; validate every pointer field before calling dsmap_init(). ALLOC is
// trusted to hand back memory suitably aligned for `TYPE` -- true for malloc()/calloc() on a hosted
// implementation, same caveat as darken.h's DARKEN_ALLOC() for a hand-rolled allocator on a freestanding
// target.
#define DSMAP_ALLOC(ALLOC, CAPACITY, SIZE)                                         \
    (dsmap_t)                                                                      \
    {                                                                              \
        .pool = (char *)(ALLOC)((CAPACITY) * (SIZE)),                              \
        .ptrs = (char **)(ALLOC)((CAPACITY) * sizeof(char *)),                     \
        .addrs = (char **)(ALLOC)((CAPACITY) * sizeof(char *)),                    \
        .lookup = (dsmap_handle_t *)(ALLOC)((CAPACITY) * sizeof(dsmap_handle_t)),  \
        .handles = (dsmap_handle_t *)(ALLOC)((CAPACITY) * sizeof(dsmap_handle_t)), \
        .capacity = (dsmap_handle_t)(CAPACITY),                                    \
        .size = (uint16_t)(SIZE),                                                  \
        .count = 0,                                                                \
    }

// Frees every block DSMAP_ALLOC() allocated.
#define DSMAP_FREE(FREE, MAP)   \
    do                          \
    {                           \
        (FREE)((MAP)->pool);    \
        (FREE)((MAP)->addrs);   \
        (FREE)((MAP)->ptrs);    \
        (FREE)((MAP)->lookup);  \
        (FREE)((MAP)->handles); \
    } while (0)

// Static allocation with automatic or static storage duration (stack or global).
//
//     DSMAP_DECLARE(storage, 100, struct MyComponent);
//     dsmap_t map = DSMAP_BIND(storage);
//     dsmap_init(&map);
//
// Expands to a single declaration, so it can be prefixed with `static` at file scope too. CAPACITY == 0
// is rejected at compile time (negative array size on `pool`), same trick as darken.h's DARKEN_DECLARE().
#define DSMAP_DECLARE(NAME, CAPACITY, TYPE)      \
    struct                                       \
    {                                            \
        TYPE pool[(CAPACITY) ? (CAPACITY) : -1]; \
        char *ptrs[(CAPACITY)];                  \
        char *addrs[(CAPACITY)];                 \
        dsmap_handle_t lookup[(CAPACITY)];       \
        dsmap_handle_t handles[(CAPACITY)];      \
    } NAME

// Static/global initialization: compile-time constants.
#define DSMAP_BIND(NAME)                                                                \
    (dsmap_t)                                                                           \
    {                                                                                   \
        .pool = (char *)(NAME).pool,                                                    \
        .ptrs = (NAME).ptrs,                                                            \
        .addrs = (NAME).addrs,                                                          \
        .lookup = (NAME).lookup,                                                        \
        .handles = (NAME).handles,                                                      \
        .capacity = (dsmap_handle_t)(sizeof((NAME).lookup) / sizeof((NAME).lookup[0])), \
        .size = (uint16_t)sizeof((NAME).pool[0]),                                       \
        .count = 0,                                                                     \
    }

// Returns the permanent address bound to `handle` for as long as it stays valid. Unlike DSMAP_FOREACH's
// `data`, a pointer obtained here is good until that handle is dsmap_remove()d -- see "How this differs
// from darken.h and vsmap.h" above. Does not itself check validity; call dsmap_valid() first if unsure.
#define DSMAP_DATA(MAP, HANDLE) ((void *)((MAP)->addrs[(HANDLE)]))

// Visits every live element's data pointer, in internal (not insertion) order. Bound to `data` (a
// `void *`) inside CODE. Safe to dsmap_remove() the element CODE is currently looking at; removing a
// *different* element reorders the internal cache and can cause that element to be visited twice or (if
// it was swapped into an already-visited position) not at all in this same pass -- same one-pass caveat
// as darken.h's DARKEN_FOREACH and vsmap.h's VSMAP_FOREACH.
#define DSMAP_FOREACH(MAP, CODE)                         \
    for (dsmap_handle_t _i = 0; _i < (MAP)->count; _i++) \
    {                                                    \
        void *data = (MAP)->ptrs[_i];                    \
        CODE;                                            \
    }

// Must be called once after ALLOC/BIND, before the first dsmap_alloc(). Also doubles as a full reset:
// call it again any time to drop every element and start over (every handle issued before that call is
// no longer valid).
DSMAP_INLINE void dsmap_init(dsmap_t *map)
{
    map->count = 0;

    for (dsmap_handle_t i = 0; i < map->capacity; i++)
    {
        map->ptrs[i] = map->addrs[i] = map->pool + (uint16_t)(i * map->size);
        map->handles[i] = i;
    }
}

// Returns a fresh handle bound to a permanent element address, or DSMAP_INVALID_HANDLE if the pool is full.
DSMAP_INLINE dsmap_handle_t dsmap_alloc(dsmap_t *map)
{
    if (map->count >= map->capacity)
        return DSMAP_INVALID_HANDLE;

    dsmap_handle_t handle = map->handles[map->count];
    map->lookup[handle] = map->count++;

    return handle;
}

DSMAP_INLINE int dsmap_valid(dsmap_t *map, dsmap_handle_t handle)
{
    if (handle >= map->capacity)
        return 0;

    dsmap_handle_t slot = map->lookup[handle];

    if (slot >= map->count)
        return 0;

    return map->handles[slot] == handle;
}

// Frees `handle`. Its DSMAP_DATA() address is NOT reused by any *other* still-live handle -- only by
// `handle` itself, if dsmap_alloc() reissues it later (see the no-staleness-detection note above).
DSMAP_INLINE void dsmap_remove(dsmap_t *map, dsmap_handle_t handle)
{
    dsmap_handle_t slot = map->lookup[handle];
    dsmap_handle_t last = (dsmap_handle_t)(--map->count);

    if (slot != last)
    {
        dsmap_handle_t moved_handle = map->handles[last];

        map->handles[slot] = moved_handle;
        map->lookup[moved_handle] = slot;
        map->ptrs[slot] = map->ptrs[last];
        map->ptrs[last] = map->addrs[handle];
    }

    map->handles[last] = handle;
}

#endif // DSMAP_H
