/**
 * darksys.h - Darksys (DARKula SYStems) Handle-based sparse pool ("slot map")
 *
 * darksys-1.0.0_dev
 *
 * * GNU C note:
 * - This header uses GNU C and statement expressions.
 * - Darksys targets GCC and the Motorola 68000.
 * - 16-bit members preference for optimal 68K performance.
 *
 *
 *
 * Stores up to `capacity` records of `params` void* fields each, packed
 * contiguously in `pool` so that iteration is a plain linear pointer walk.
 * Every record is reachable through a stable handle even after other
 * records are added/removed: `handles` (slot -> handle, dense) and
 * `lookup` (handle -> slot, sparse) are kept in sync with a swap-and-pop
 * scheme, so add()/remove() are O(1) and a live record never moves without
 * its handle staying valid.
 */
#pragma once

#include <stdint.h>

typedef uint16_t darksys_handle_t;
typedef void **darksys_data_t;

#define DARKSYS_INVALID_HANDLE ((darksys_handle_t)0xFFFFu)

typedef struct
{
    darksys_data_t pool;
    uint16_t *lookup;  // handle -> slot while active, free-list link while free
    uint16_t *handles; // slot -> handle (dense)

    uint16_t capacity;
    uint16_t params;
    uint16_t count;     // active records right now
    uint16_t next;      // highest handle ever issued (bump allocator)
    uint16_t free_head; // head of the free-handle list, or INVALID
} darksys_t;

/* ============================================================================
 * PRIVATE
 * ============================================================================ */

#define _DARKSYS_NARGS(...) _DARKSYS_NARGS_I(__VA_ARGS__, 5, 4, 3, 2, 1)
#define _DARKSYS_NARGS_I(_1, _2, _3, _4, _5, N, ...) N

#define _DARKSYS_WRITE_N(N, SYSTEM, ...) _DARKSYS_WRITE_N_I(N, SYSTEM, __VA_ARGS__)
#define _DARKSYS_WRITE_N_I(N, SYSTEM, ...) _DARKSYS_WRITE_##N(SYSTEM, __VA_ARGS__)

// Writes the fields of the record just placed at slot (count - 1). Same
// note as everywhere else: (count - 1) * params is used directly, not
// stored into a uint16_t first, so it stays a single MULU.W/MULS.W.
#define _DARKSYS_WRITE_1(SYSTEM, A) ((SYSTEM)->pool[((SYSTEM)->count - 1) * (SYSTEM)->params] = (A))
#define _DARKSYS_WRITE_2(SYSTEM, A, B) (_DARKSYS_WRITE_1(SYSTEM, A), (SYSTEM)->pool[((SYSTEM)->count - 1) * (SYSTEM)->params + 1] = (B))
#define _DARKSYS_WRITE_3(SYSTEM, A, B, C) (_DARKSYS_WRITE_2(SYSTEM, A, B), (SYSTEM)->pool[((SYSTEM)->count - 1) * (SYSTEM)->params + 2] = (C))
#define _DARKSYS_WRITE_4(SYSTEM, A, B, C, D) (_DARKSYS_WRITE_3(SYSTEM, A, B, C), (SYSTEM)->pool[((SYSTEM)->count - 1) * (SYSTEM)->params + 3] = (D))
#define _DARKSYS_WRITE_5(SYSTEM, A, B, C, D, E) (_DARKSYS_WRITE_4(SYSTEM, A, B, C, D), (SYSTEM)->pool[((SYSTEM)->count - 1) * (SYSTEM)->params + 4] = (E))

#define _DARKSYS_FOREACH_DISPATCH(N, SYSTEM, ...) _DARKSYS_FOREACH_DISPATCH_I(N, SYSTEM, __VA_ARGS__)
#define _DARKSYS_FOREACH_DISPATCH_I(N, SYSTEM, ...) _DARKSYS_FOREACH_##N(SYSTEM, __VA_ARGS__)

#define _DARKSYS_FOREACH_NARGS(...) _DARKSYS_FOREACH_NARGS_I(__VA_ARGS__, 6, 5, 4, 3, 2, 1, 0)
#define _DARKSYS_FOREACH_NARGS_I(_1, _2, _3, _4, _5, _6, N, ...) N

#define _DARKSYS_FOREACH_2(SYSTEM, A, IT) _DARKSYS_FOREACH_RUN(SYSTEM, A = _pool[0]; IT)
#define _DARKSYS_FOREACH_3(SYSTEM, A, B, IT) _DARKSYS_FOREACH_RUN(SYSTEM, A = _pool[0]; B = _pool[1]; IT)
#define _DARKSYS_FOREACH_4(SYSTEM, A, B, C, IT) _DARKSYS_FOREACH_RUN(SYSTEM, A = _pool[0]; B = _pool[1]; C = _pool[2]; IT)
#define _DARKSYS_FOREACH_5(SYSTEM, A, B, C, D, IT) _DARKSYS_FOREACH_RUN(SYSTEM, A = _pool[0]; B = _pool[1]; C = _pool[2]; D = _pool[3]; IT)
#define _DARKSYS_FOREACH_6(SYSTEM, A, B, C, D, E, IT) _DARKSYS_FOREACH_RUN(SYSTEM, A = _pool[0]; B = _pool[1]; C = _pool[2]; D = _pool[3]; E = _pool[4]; IT)

// `_pool` stays in scope inside IT: to write a field back in place (e.g. a
// system integrating position += velocity) assign to `_pool[N]` for the
// N-th bound field, the same slot the value was just read from. This is a
// plain `+= params` pointer walk -- no indexing, no multiply anywhere.
#define _DARKSYS_FOREACH_RUN(SYSTEM, CODE) \
    do                                     \
    {                                      \
        darksys_t *s = (SYSTEM);           \
        darksys_data_t _pool = s->pool;    \
        uint16_t _count = s->count;        \
        uint16_t _params = s->params;      \
                                           \
        while (_count--)                   \
        {                                  \
            CODE;                          \
            _pool += _params;              \
        }                                  \
    } while (0)

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

// Allocates pool/lookup/handles with ALLOC (e.g. malloc, or SGDK's MEM_alloc)
// and returns a brace-init darksys_t ready to use. Pair with DARKSYS_FREE.
#define DARKSYS_ALLOC(ALLOC, CAPACITY, PARAMS)                                   \
    {                                                                            \
        .pool = (darksys_data_t)(ALLOC)((CAPACITY) * (PARAMS) * sizeof(void *)), \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),            \
        .handles = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),           \
        .capacity = (CAPACITY),                                                  \
        .params = (PARAMS),                                                      \
        .count = 0,                                                              \
        .next = 0,                                                               \
        .free_head = DARKSYS_INVALID_HANDLE,                                     \
    }

#define DARKSYS_FREE(FREE, SYSTEM) \
    do                             \
    {                              \
        (FREE)((SYSTEM)->pool);    \
        (FREE)((SYSTEM)->lookup);  \
        (FREE)((SYSTEM)->handles); \
    } while (0)

// Declares a statically-sized storage struct (no malloc) named NAME, with
// CAPACITY records of PARAMS fields each. Use DARKSYS_BIND(NAME) to get a
// `darksys` view over it.
#define DARKSYS_DECLARE(NAME, CAPACITY, PARAMS)     \
    struct                                          \
    {                                               \
        darksys_data_t pool[(CAPACITY) * (PARAMS)]; \
        uint16_t lookup[(CAPACITY)];                \
        uint16_t handles[(CAPACITY)];               \
        uint16_t capacity;                          \
        uint16_t params;                            \
    } NAME = {                                      \
        .capacity = (CAPACITY),                     \
        .params = (PARAMS),                         \
    }

#define DARKSYS_BIND(NAME)                   \
    (darksys_t)                              \
    {                                        \
        .pool = (NAME).pool,                 \
        .lookup = (NAME).lookup,             \
        .handles = (NAME).handles,           \
        .capacity = (NAME).capacity,         \
        .params = (NAME).params,             \
        .count = 0,                          \
        .next = 0,                           \
        .free_head = DARKSYS_INVALID_HANDLE, \
    }

// Pointer to the first field of HANDLE's record; row[0..params-1] are its
// fields, in the order they were written. Does NOT check that HANDLE is
// valid -- call darksys_valid() first if that isn't already known.
#define DARKSYS_DATA(SYSTEM, HANDLE) ((darksys_data_t)((SYSTEM)->pool + (SYSTEM)->lookup[(HANDLE)] * (SYSTEM)->params))

// Adds a record and writes its fields in one call:
//     darksys_handle_t h = DARKSYS_ADD(&pool, x, y, sprite);
//
// Returns DARKSYS_INVALID_HANDLE (and writes nothing) if the argument count
// doesn't match `params`, or if the pool is full.
#define DARKSYS_ADD(SYSTEM, ...)                                                                                \
    ({                                                                                                          \
        darksys_t *_s = (SYSTEM);                                                                               \
        darksys_handle_t _handle = DARKSYS_INVALID_HANDLE;                                                      \
                                                                                                                \
        if (_DARKSYS_NARGS(__VA_ARGS__) == _s->params && (_handle = darksys_add(_s)) != DARKSYS_INVALID_HANDLE) \
            _DARKSYS_WRITE_N(_DARKSYS_NARGS(__VA_ARGS__), _s, __VA_ARGS__);                                     \
        _handle;                                                                                                \
    })

// Iterates active records in pool order (not handle order), binding one
// local per field:
//     void *sprite; int16_t x, y;
//     DARKSYS_FOREACH(&pool, sprite, x, y, { move(sprite, x, y); });
//
// See the write-back note on _DARKSYS_FOREACH_RUN above if the loop body
// needs to update a field in place.
#define DARKSYS_FOREACH(SYSTEM, ...) _DARKSYS_FOREACH_DISPATCH(_DARKSYS_FOREACH_NARGS(__VA_ARGS__), SYSTEM, __VA_ARGS__)

/* ============================================================================
 * FUNCTIONS
 * ============================================================================ */

static inline darksys_handle_t darksys_add(darksys_t *s)
{
    // `s->count >= s->capacity` is the real "pool full" check. The second
    // half of the OR is unreachable in practice -- if free_head is
    // INVALID, no handle has ever been freed, so count == next always
    // holds and count < capacity (already checked) implies next < capacity
    // too -- but it costs nothing to keep as a defensive check.
    if (s->count >= s->capacity || (s->free_head == DARKSYS_INVALID_HANDLE && s->next >= s->capacity))
        return DARKSYS_INVALID_HANDLE;

    darksys_handle_t handle = s->free_head;

    if (s->free_head != DARKSYS_INVALID_HANDLE)
        // Reuse a handle freed by a previous remove(). While a handle is
        // not in use, lookup[handle] doubles as the free list's "next"
        // pointer.
        s->free_head = s->lookup[handle];
    else
        handle = s->next++;

    uint16_t slot = s->count++;

    s->handles[slot] = handle;
    s->lookup[handle] = slot;

    return handle;
}

static inline uint16_t darksys_valid(const darksys_t *s, darksys_handle_t handle)
{
    if (handle >= s->next)
        return 0;

    uint16_t slot = s->lookup[handle];

    return slot < s->count && s->handles[slot] == handle;
}

// Swap-and-pop removal: the last active record takes the freed slot, so
// `pool` stays dense and iteration/add() never have to skip holes.
//
// Does NOT validate `handle` -- the caller is expected to only pass
// handles known to be valid (e.g. one just returned by darksys_add(), or
// checked with darksys_valid()). Calling this with a handle that was never
// issued reads lookup[] out of bounds; calling it twice on the same handle
// (or on one darksys_valid() would reject) still runs `--s->count`, which
// underflows to 0xFFFF and corrupts the free list. If that can happen in a
// given call site, guard it with darksys_valid() there.
static inline void darksys_remove(darksys_t *s, darksys_handle_t handle)
{
    uint16_t slot = s->lookup[handle];
    uint16_t last = --s->count;

    if (slot != last)
    {
        // Move the last record into the freed slot. `params` doubles as
        // the loop counter, counting down to 0: on m68k this compiles to
        // DBRA, and the post-incremented pool pointers compile to (An)+
        // addressing -- the cheapest way to walk memory on this CPU.
        uint16_t params = s->params;
        darksys_data_t dst = s->pool + slot * params;
        darksys_data_t src = s->pool + last * params;

        while (params--)
            *dst++ = *src++;

        uint16_t moved = s->handles[last];

        s->handles[slot] = moved;
        s->lookup[moved] = slot;
    }

    s->lookup[handle] = s->free_head;
    s->free_head = handle;
}

static inline void darksys_clear(darksys_t *s)
{
    s->count = 0;
    s->next = 0;
    s->free_head = DARKSYS_INVALID_HANDLE;
}
