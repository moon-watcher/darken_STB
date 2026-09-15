/**
 * darksys - handle-based sparse pool ("slot map") for SGDK / Sega Genesis.
 *
 * Stores up to `capacity` records of `params` void* fields each, packed
 * contiguously in `pool` so that iteration is a plain linear pointer walk.
 * Every record is reachable through a stable handle even after other
 * records are added/removed: `handles` (slot -> handle, dense) and
 * `lookup` (handle -> slot, sparse) are kept in sync with a swap-and-pop
 * scheme, so add()/remove() are O(1) and a live record never moves without
 * its handle staying valid.
 *
 * GCC / m68000 notes (checked against SGDK's toolchain, -m68000, -O0..-O3):
 *
 *  - The 68000 has no 32x32 hardware multiply, only 16x16->32
 *    (MULU.W/MULS.W, ~40-70 cycles); a genuine 32-bit multiply falls back
 *    to libgcc's __mulsi3 (several hundred cycles, call+return included).
 *    Every `slot * params` / `count * params` below keeps both operands as
 *    uint16_t and uses the product directly (never narrows it into a
 *    uint16_t temporary first). That is what lets GCC emit a single
 *    MULU.W/MULS.W instead of __mulsi3 -- confirmed by inspecting the
 *    actual .s output, not just inferred from the C types.
 *  - The swap-copy in remove() walks with `while (params--) *dst++ = *src++;`
 *    (post-incremented pointers, count down to zero): the m68k backend
 *    turns that into DBRA plus (An)+ addressing, the cheapest way to walk
 *    memory on this CPU. DARKSYS_FOREACH uses the same idiom, and never
 *    needed a multiply to begin with (it just walks pool by += params).
 */
#pragma once

#include <stdint.h>

typedef uint16_t darksys_handle;
typedef void **darksys_data;

#define DARKSYS_INVALID_HANDLE ((darksys_handle)0xFFFFu)

typedef struct
{
    darksys_data pool;
    uint16_t *lookup;  // handle -> slot while active, free-list link while free
    uint16_t *handles; // slot -> handle (dense)

    uint16_t capacity;
    uint16_t params;
    uint16_t count;     // active records right now
    uint16_t next;      // highest handle ever issued (bump allocator)
    uint16_t free_head; // head of the free-handle list, or INVALID
} darksys;

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
        darksys *s = (SYSTEM);             \
        darksys_data _pool = s->pool;      \
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
// and returns a brace-init darksys ready to use. Pair with DARKSYS_POOL_FREE.
#define DARKSYS_POOL_ALLOC(ALLOC, CAPACITY, PARAMS)                            \
    {                                                                          \
        .pool = (darksys_data)(ALLOC)((CAPACITY) * (PARAMS) * sizeof(void *)), \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),          \
        .handles = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),         \
        .capacity = (CAPACITY),                                                \
        .params = (PARAMS),                                                    \
        .count = 0,                                                            \
        .next = 0,                                                             \
        .free_head = DARKSYS_INVALID_HANDLE,                                   \
    }

#define DARKSYS_POOL_FREE(FREE, SYSTEM) \
    do                                  \
    {                                   \
        (FREE)((SYSTEM)->pool);         \
        (FREE)((SYSTEM)->lookup);       \
        (FREE)((SYSTEM)->handles);      \
    } while (0)

// Declares a statically-sized storage struct (no malloc) named NAME, with
// CAPACITY records of PARAMS fields each. Use DARKSYS_POOL_BIND(NAME) to
// get a `darksys` view over it.
#define DARKSYS_POOL_DECLARE(NAME, CAPACITY, PARAMS) \
    struct                                           \
    {                                                \
        darksys_data pool[(CAPACITY) * (PARAMS)];    \
        uint16_t lookup[(CAPACITY)];                 \
        uint16_t handles[(CAPACITY)];                \
        uint16_t capacity;                           \
        uint16_t params;                             \
    } NAME = {                                       \
        .capacity = (CAPACITY),                      \
        .params = (PARAMS),                          \
    }

#define DARKSYS_POOL_BIND(NAME)              \
    (darksys)                                \
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
#define DARKSYS_DATA(SYSTEM, HANDLE) ((darksys_data)((SYSTEM)->pool + (SYSTEM)->lookup[(HANDLE)] * (SYSTEM)->params))

// Adds a record and writes its fields in one call:
//     darksys_handle h = DARKSYS_ADD(&pool, x, y, sprite);
//
// Returns DARKSYS_INVALID_HANDLE (and writes nothing) if the argument count
// doesn't match `params`, or if the pool is full.
#define DARKSYS_ADD(SYSTEM, ...)                                                                                \
    ({                                                                                                          \
        darksys *_s = (SYSTEM);                                                                                 \
        darksys_handle _handle = DARKSYS_INVALID_HANDLE;                                                        \
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

static inline darksys_handle darksys_add(darksys *s)
{
    // `s->count >= s->capacity` is the real "pool full" check. The second
    // half of the OR is unreachable in practice -- if free_head is
    // INVALID, no handle has ever been freed, so count == next always
    // holds and count < capacity (already checked) implies next < capacity
    // too -- but it costs nothing to keep as a defensive check.
    if (s->count >= s->capacity || (s->free_head == DARKSYS_INVALID_HANDLE && s->next >= s->capacity))
        return DARKSYS_INVALID_HANDLE;

    darksys_handle handle = s->free_head;

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

static inline uint16_t darksys_valid(const darksys *s, darksys_handle handle)
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
static inline void darksys_remove(darksys *s, darksys_handle handle)
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
        darksys_data dst = s->pool + slot * params;
        darksys_data src = s->pool + last * params;

        while (params--)
            *dst++ = *src++;

        uint16_t moved = s->handles[last];

        s->handles[slot] = moved;
        s->lookup[moved] = slot;
    }

    s->lookup[handle] = s->free_head;
    s->free_head = handle;
}

static inline void darksys_clear(darksys *s)
{
    s->count = 0;
    s->next = 0;
    s->free_head = DARKSYS_INVALID_HANDLE;
}
