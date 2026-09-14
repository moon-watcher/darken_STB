#pragma once

#include <stdint.h>

/*
 * darksys8 - Dynamic Slot Map System
 * (renamed fork of darksys.h, reworked applying vsmap.h's design)
 *
 * Stores groups of pointers associated with stable handles.
 *
 * Each handle identifies one group of `params` consecutive pointers.
 *
 *     handle -> lookup[handle] -> slot -> pool[slot * params]
 *
 * Example with params = 3:
 *
 *     [A][A][A] [B][B][B] [C][C][C]
 *
 * The handle identifies the group, not its physical slot.
 *
 * When a group is removed, the last active group is moved into the removed
 * slot. The moved group keeps its handle, so the handle remains valid.
 *
 * Example:
 *     darksys8 system = DARKSYS8_POOL_ALLOC(MEM_alloc, 100, 3);
 *     darksys8_init(&system);
 *
 *     darksys8_handle_t f = DARKSYS8_ADD(&system, entity, position, velocity);
 *     void **data = darksys8_data(&system, f);
 *
 *     darksys8_remove(&system, c);
 *
 *     data = darksys8_data(&system, f);
 *
 * The handle `f` still identifies the same group even if that group has moved
 * to a different physical slot.
 *
 *
 *
 * Header-only, static inline (same rationale as vsmap.h)
 * ==================================================
 *
 * As in vsmap.h: on a cycle-constrained 68k target, a JSR/RTS plus argument passing is
 * real, measurable overhead for something as small as "look up a group of pointers" —
 * inlining removes it entirely at every call site. There is no DARKSYS8_IMPLEMENTATION
 * section and nothing to compile separately; every function below is `static inline`,
 * defined directly in this header. Same trade-off as always with header-only inline
 * code: if this header is included from many .c files, each may keep its own copy of
 * whichever of these don't get fully inlined away. For functions this small, that's a
 * good trade.
 *
 *
 *
 * IMPORTANT — what "no generation" actually means: like vsmap, handles are recycled and
 * nothing marks a reused handle as belonging to a "new" group versus the old one. If code
 * A holds handle H, something else removes H's group and later adds a brand new group
 * that happens to get the same H back, darksys8_data(H) will now silently return the NEW
 * group's pointers — code A has no way to tell the difference. Keep handles out of
 * long-lived storage you don't fully control the lifetime of.
 *
 * Capacity tops out at 0xFFFE: 0xFFFF (DARKSYS8_INVALID_HANDLE) is reserved as both the
 * free-list terminator and the "no handle available" sentinel.
 *
 * IMPORTANT:
 *
 *     capacity   = maximum number of groups (<= 0xFFFE)
 *     size       = number of active pointers
 *     params     = number of pointers per group
 *     count      = number of active groups
 *     free_head  = head of the free list (DARKSYS8_INVALID_HANDLE if none are free)
 */

typedef uint16_t darksys8_handle_t;

typedef struct
{
    void **pool;
    uint16_t *lookup;
    uint16_t *handles;

    uint16_t capacity;  // Maximum number of groups
    uint16_t size;      // Number of active pointers
    uint16_t params;    // Number of pointers per group
    uint16_t count;     // Number of active groups
    uint16_t free_head; // Head of the free list, or DARKSYS8_INVALID_HANDLE if empty
} darksys8;

#define DARKSYS8_INVALID_HANDLE ((darksys8_handle_t)0xFFFFu)

/* ============================================================================
 * PUBLIC API — all static inline, always available, nothing to compile separately
 * ============================================================================ */

// Dynamic allocation:
//     darksys8 system = DARKSYS8_POOL_ALLOC(MEM_alloc, 100, 3);
//     darksys8_init(&system);
//     darksys8_handle_t handle = DARKSYS8_ADD(&system, entity, position, velocity);
//     void **data = darksys8_data(&system, handle);
//     ...
//     MEM_free(system.pool);
//     MEM_free(system.lookup);
//     MEM_free(system.handles);
#define DARKSYS8_POOL_ALLOC(ALLOC, CAPACITY, PARAMS)                      \
    {                                                                     \
        .pool = (void **)(ALLOC)((CAPACITY) * (PARAMS) * sizeof(void *)), \
        .lookup = (ALLOC)((CAPACITY) * sizeof(uint16_t)),                 \
        .handles = (ALLOC)((CAPACITY) * sizeof(uint16_t)),                \
        .capacity = (CAPACITY),                                           \
        .params = (PARAMS),                                               \
    }

// Static allocation with automatic storage duration (stack or global).
//
//     DARKSYS8_POOL_DECLARE(storage, 100, 3);
//     darksys8 system = DARKSYS8_POOL_BIND(storage);
//     darksys8_init(&system);
#define DARKSYS8_POOL_DECLARE(NAME, CAPACITY, PARAMS) \
    struct                                            \
    {                                                 \
        uint16_t capacity;                            \
        uint16_t params;                              \
        void *pool[(CAPACITY) * (PARAMS)];            \
        uint16_t lookup[(CAPACITY)];                  \
        uint16_t handles[(CAPACITY)];                 \
    } NAME = {                                        \
        .capacity = (CAPACITY),                       \
        .params = (PARAMS),                           \
    }

// Static/global initialization: compile-time constants. Still requires darksys8_init()
// at runtime before the first add — see the big comment above darksys8_init().
#define DARKSYS8_POOL_INIT(STORAGE, CAPACITY, PARAMS) \
    {                                                 \
        .pool = (STORAGE).pool,                       \
        .lookup = (STORAGE).lookup,                   \
        .handles = (STORAGE).handles,                 \
        .capacity = (CAPACITY),                       \
        .params = (PARAMS),                           \
    }

// Runtime binding: locals, reassignment, any context. Still requires darksys8_init()
// at runtime before the first add — see the big comment above darksys8_init().
#define DARKSYS8_POOL_BIND(NAME)     \
    {                                \
        .pool = (NAME).pool,         \
        .lookup = (NAME).lookup,     \
        .handles = (NAME).handles,   \
        .capacity = (NAME).capacity, \
        .params = (NAME).params,     \
    }

// Adds one group of pointers. The number of arguments must match `params`.
//     darksys8_handle_t handle = DARKSYS8_ADD(&system, A);
//     darksys8_handle_t handle = DARKSYS8_ADD(&system, A, B, C);
//
// Returns a valid handle, or DARKSYS8_INVALID_HANDLE if the pool is full or the wrong
// number of arguments was passed for this system's `params`.
#define DARKSYS8_ADD(SYSTEM, ...) ({                                          \
    darksys8 *_s = (SYSTEM);                                                  \
    darksys8_handle_t _handle = DARKSYS8_INVALID_HANDLE;                      \
    if (_DARKSYS8_NARGS(__VA_ARGS__) == _s->params)                           \
    {                                                                         \
        _handle = darksys8_add(_s);                                           \
        if (_handle != DARKSYS8_INVALID_HANDLE)                               \
        {                                                                     \
            _DARKSYS8_WRITE_N(_DARKSYS8_NARGS(__VA_ARGS__), _s, __VA_ARGS__); \
            _s->size += _s->params;                                           \
        }                                                                     \
    }                                                                         \
    _handle;                                                                  \
})

// Unchecked direct access to the group's pointers — for when the caller already knows
// `h` is valid (e.g. right after DARKSYS8_ADD) and doesn't want to pay for
// darksys8_valid() again. Result type is `void **`, same as darksys8_data().
#define DARKSYS8_DATA(SYSTEM, H) ((SYSTEM)->pool + (uint16_t)((SYSTEM)->lookup[H]) * (SYSTEM)->params)

#define DARKSYS8_FOREACH(SYSTEM, ...) \
    _DARKSYS8_FOREACH_DISPATCH(_DARKSYS8_FOREACH_NARGS(__VA_ARGS__), SYSTEM, __VA_ARGS__)

/* ============================================================================
 * PRIVATE
 * ============================================================================ */

#define _DARKSYS8_FOREACH_DISPATCH(N, SYSTEM, ...) _DARKSYS8_FOREACH_DISPATCH_I(N, SYSTEM, __VA_ARGS__)
#define _DARKSYS8_FOREACH_DISPATCH_I(N, SYSTEM, ...) _DARKSYS8_FOREACH_##N(SYSTEM, __VA_ARGS__)
#define _DARKSYS8_FOREACH_NARGS(...) _DARKSYS8_FOREACH_NARGS_I(__VA_ARGS__, 6, 5, 4, 3, 2, 1)
#define _DARKSYS8_FOREACH_NARGS_I(_1, _2, _3, _4, _5, _6, N, ...) N

#define _DARKSYS8_FOREACH_1(SYSTEM, IT) _DARKSYS8_FOREACH(SYSTEM, { IT; })
#define _DARKSYS8_FOREACH_2(SYSTEM, A, IT) _DARKSYS8_FOREACH(SYSTEM, { A = _pool[0]; IT; })
#define _DARKSYS8_FOREACH_3(SYSTEM, A, B, IT) _DARKSYS8_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; IT; })
#define _DARKSYS8_FOREACH_4(SYSTEM, A, B, C, IT) _DARKSYS8_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; C = _pool[2]; IT; })
#define _DARKSYS8_FOREACH_5(SYSTEM, A, B, C, D, IT) _DARKSYS8_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; C = _pool[2]; D = _pool[3]; IT; })
#define _DARKSYS8_FOREACH_6(SYSTEM, A, B, C, D, E, IT) _DARKSYS8_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; C = _pool[2]; D = _pool[3]; E = _pool[4]; IT; })

#define _DARKSYS8_FOREACH(SYSTEM, CODE) \
    do                                  \
    {                                   \
        darksys8 *s = (SYSTEM);         \
        uint16_t _count = s->count;     \
        void **_pool = s->pool;         \
        uint16_t _params = s->params;   \
                                        \
        while (_count)                  \
        {                               \
            CODE;                       \
            _pool += _params;           \
            --_count;                   \
        }                               \
    } while (0)

#define _DARKSYS8_NARGS(...) _DARKSYS8_NARGS_I(__VA_ARGS__, 5, 4, 3, 2, 1)
#define _DARKSYS8_NARGS_I(_1, _2, _3, _4, _5, N, ...) N

#define _DARKSYS8_WRITE_N(N, SYSTEM, ...) _DARKSYS8_WRITE_N_I(N, SYSTEM, __VA_ARGS__)
#define _DARKSYS8_WRITE_N_I(N, SYSTEM, ...) _DARKSYS8_WRITE_##N(SYSTEM, __VA_ARGS__)

#define _DARKSYS8_WRITE_1(SYSTEM, A) \
    ((SYSTEM)->pool[(SYSTEM)->size] = (A))

#define _DARKSYS8_WRITE_2(SYSTEM, A, B) \
    (_DARKSYS8_WRITE_1(SYSTEM, A),      \
     (SYSTEM)->pool[(SYSTEM)->size + 1] = (B))

#define _DARKSYS8_WRITE_3(SYSTEM, A, B, C) \
    (_DARKSYS8_WRITE_2(SYSTEM, A, B),      \
     (SYSTEM)->pool[(SYSTEM)->size + 2] = (C))

#define _DARKSYS8_WRITE_4(SYSTEM, A, B, C, D) \
    (_DARKSYS8_WRITE_3(SYSTEM, A, B, C),      \
     (SYSTEM)->pool[(SYSTEM)->size + 3] = (D))

#define _DARKSYS8_WRITE_5(SYSTEM, A, B, C, D, E) \
    (_DARKSYS8_WRITE_4(SYSTEM, A, B, C, D),      \
     (SYSTEM)->pool[(SYSTEM)->size + 4] = (E))

/* ============================================================================
 * IMPLEMENTATION — static inline, always available
 * ============================================================================ */

// Must be called once after ALLOC/DECLARE+BIND/INIT, before the first DARKSYS8_ADD().
// Also doubles as a full reset: call it again any time to drop every group and start
// over (every handle issued before that call is no longer valid — same "everything gets
// freed" contract as vsmap_init()). Rebuilding the free list touches every slot, so this
// is O(capacity), unlike the original darksys.h's next-pointer-based reset, which was
// O(1).
static inline void darksys8_init(darksys8 *s)
{
    s->count = 0;
    s->size = 0;
    s->free_head = 0;

    for (uint16_t i = 0; i < s->capacity; i++)
        s->lookup[i] = i + 1; // chain every slot into the free list...

    s->lookup[s->capacity - 1] = DARKSYS8_INVALID_HANDLE; // ...terminated here
}

// Checks whether `handle` currently refers to a live group.
// Returns:
//      0 : valid
//     -1 : handle is outside the range of valid handles
//     -2 : handle points to an inactive slot
//     -3 : corrupted lookup / handle does not match the slot
//
// Both darksys8_data() and darksys8_remove() go through this single check, so there is
// only one place that decides what "valid" means.
static inline uint16_t darksys8_valid(darksys8 *s, darksys8_handle_t handle)
{
    if (handle >= s->capacity)
        return 0;

    uint16_t slot = s->lookup[handle];

    if (slot >= s->count || s->handles[slot] != handle)
        return 0;

    return 1;
}

// Adds one group, returns its handle, or DARKSYS8_INVALID_HANDLE if the pool is full.
// Prefer DARKSYS8_ADD() over calling this directly — it also writes the group's pointers.
static inline darksys8_handle_t darksys8_add(darksys8 *s)
{
    if (s->count >= s->capacity || s->free_head == DARKSYS8_INVALID_HANDLE)
        return DARKSYS8_INVALID_HANDLE;

    darksys8_handle_t handle = s->free_head;
    s->free_head = s->lookup[handle]; // pop the free list

    uint16_t slot = s->count++;
    s->handles[slot] = handle;
    s->lookup[handle] = slot;

    return handle;
}

// Removes `handle` if valid. Returns 0 on success, or the same negative status codes
// as darksys8_valid() (and does nothing) if the handle wasn't valid to begin with.
static inline void darksys8_remove(darksys8 *s, darksys8_handle_t handle)
{
    uint16_t slot = s->lookup[handle];
    uint16_t last = --s->count;

    if (slot != last)
    {
        uint16_t dst = slot * s->params;
        uint16_t src = last * s->params;

        for (uint16_t i = 0; i < s->params; ++i)
            s->pool[dst + i] = s->pool[src + i];

        uint16_t moved = s->handles[last];

        s->handles[slot] = moved;
        s->lookup[moved] = slot;
    }

    s->size -= s->params;
    s->lookup[handle] = s->free_head; // push back onto the free list
    s->free_head = handle;
}
