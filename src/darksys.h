#pragma once

#include <stdint.h>

/*
 * darksys - Dynamic Slot Map System
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
 *     int16_t f = DARKSYS_ADD(&system, entity, position, velocity);
 *     void **data = darksys_data(&system, f);
 *
 *     darksys_remove(&system, c);
 *
 *     data = darksys_data(&system, f);
 *
 * The handle `f` still identifies the same group even if that group has moved
 * to a different physical slot.
 *
 * IMPORTANT:
 *
 *     capacity   = maximum number of groups.
 *     size       = number of active pointers.
 *     params     = number of pointers per group.
 *     limit      = maximum number of pointers.
 *     count      = number of active groups.
 *     next       = next handle to assign.
 *     free_head  = first released handle.
 *     free_count = number of released handles.
 */

typedef struct
{
    void **pool;
    uint16_t *lookup;
    uint16_t *handles;

    uint16_t capacity;   // Maximum number of groups.
    uint16_t size;       // Number of active pointers.
    uint16_t params;     // Number of pointers per group.
    uint16_t limit;      // Maximum number of pointers.
    uint16_t count;      // Number of active groups.
    uint16_t next;       // Next handle to assign.
    uint16_t free_head;  // First released handle.
    uint16_t free_count; // Number of released handles.
} darksys;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

// Dynamic allocation:
//     darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 100, 3);
//     int16_t handle = DARKSYS_ADD(&system, entity, position, velocity);
//     void **data = darksys_data(&system, handle);
//     ...
//     MEM_free(system.pool);
//     MEM_free(system.lookup);
//     MEM_free(system.handles);
#define DARKSYS_POOL_ALLOC(ALLOC, CAPACITY, PARAMS)                       \
    {                                                                     \
        .pool = (void **)(ALLOC)((CAPACITY) * (PARAMS) * sizeof(void *)), \
        .lookup = (ALLOC)((CAPACITY) * sizeof(uint16_t)),                 \
        .handles = (ALLOC)((CAPACITY) * sizeof(uint16_t)),                \
        .capacity = (CAPACITY),                                           \
        .size = 0,                                                        \
        .params = (PARAMS),                                               \
        .limit = (CAPACITY) * (PARAMS),                                   \
        .count = 0,                                                       \
        .next = 0,                                                        \
        .free_head = 0,                                                   \
        .free_count = 0,                                                  \
    }

// Static allocation:
//     DARKSYS_POOL_DECLARE(storage, 100, 3);
//     darksys system = DARKSYS_POOL_BIND(storage);
#define DARKSYS_POOL_DECLARE(NAME, CAPACITY, PARAMS) \
    struct                                           \
    {                                                \
        uint16_t capacity;                           \
        uint16_t params;                             \
        void *pool[(CAPACITY) * (PARAMS)];           \
        uint16_t lookup[(CAPACITY)];                 \
        uint16_t handles[(CAPACITY)];                \
    } NAME = {                                       \
        .capacity = (CAPACITY),                      \
        .params = (PARAMS),                          \
    }

// Static/global initialization.
#define DARKSYS_POOL_INIT(STORAGE, CAPACITY, PARAMS) \
    {                                                \
        .pool = (STORAGE).pool,                      \
        .lookup = (STORAGE).lookup,                  \
        .handles = (STORAGE).handles,                \
        .capacity = (CAPACITY),                      \
        .size = 0,                                   \
        .params = (PARAMS),                          \
        .limit = (CAPACITY) * (PARAMS),              \
        .count = 0,                                  \
        .next = 0,                                   \
        .free_head = 0,                              \
        .free_count = 0,                             \
    }

// Runtime binding.
#define DARKSYS_POOL_BIND(NAME)                   \
    {                                             \
        .pool = (NAME).pool,                      \
        .lookup = (NAME).lookup,                  \
        .handles = (NAME).handles,                \
        .capacity = (NAME).capacity,              \
        .size = 0,                                \
        .params = (NAME).params,                  \
        .limit = (NAME).capacity * (NAME).params, \
        .count = 0,                               \
        .next = 0,                                \
        .free_head = 0,                           \
        .free_count = 0,                          \
    }

// Adds one group of pointers.
//
// The number of arguments must match `params`.
//     int16_t handle = DARKSYS_ADD(&system, A);
//     int16_t handle = DARKSYS_ADD(&system, A, B, C);
//
// Returns:
//     >= 0 : stable handle.
//     -3   : invalid number of parameters.
//     -2   :
//     -1   : pool full.
#define DARKSYS_ADD(SYSTEM, ...) ({                                         \
    darksys *_s = (SYSTEM);                                                 \
    int16_t _handle = -3;                                                   \
    if (_DARKSYS_NARGS(__VA_ARGS__) == _s->params)                          \
    {                                                                       \
        _handle = darksys_add(_s);                                          \
        if (_handle >= 0)                                                   \
        {                                                                   \
            _DARKSYS_WRITE_N(_DARKSYS_NARGS(__VA_ARGS__), _s, __VA_ARGS__); \
            _s->size += _s->params;                                         \
        }                                                                   \
    }                                                                       \
    _handle;                                                                \
})

void **darksys_data(darksys *, uint16_t);
int16_t darksys_remove(darksys *, uint16_t);
void darksys_clear(darksys *);
int16_t darksys_add(darksys *);

#define DARKSYS_FOREACH(SYSTEM, ...) \
    _DARKSYS_FOREACH_DISPATCH(_DARKSYS_FOREACH_NARGS(__VA_ARGS__), SYSTEM, __VA_ARGS__)

/* ============================================================================
 * PRIVATE
 * ============================================================================ */

#define _DARKSYS_FOREACH_DISPATCH(N, SYSTEM, ...) _DARKSYS_FOREACH_DISPATCH_I(N, SYSTEM, __VA_ARGS__)
#define _DARKSYS_FOREACH_DISPATCH_I(N, SYSTEM, ...) _DARKSYS_FOREACH_##N(SYSTEM, __VA_ARGS__)
#define _DARKSYS_FOREACH_NARGS(...) _DARKSYS_FOREACH_NARGS_I(__VA_ARGS__, 6, 5, 4, 3, 2, 1)
#define _DARKSYS_FOREACH_NARGS_I(_1, _2, _3, _4, _5, _6, N, ...) N

#define _DARKSYS_FOREACH_1(SYSTEM, IT) _DARKSYS_FOREACH(SYSTEM, { IT; })
#define _DARKSYS_FOREACH_2(SYSTEM, A, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; IT; })
#define _DARKSYS_FOREACH_3(SYSTEM, A, B, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; IT; })
#define _DARKSYS_FOREACH_4(SYSTEM, A, B, C, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; C = _pool[2]; IT; })
#define _DARKSYS_FOREACH_5(SYSTEM, A, B, C, D, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; C = _pool[2]; D = _pool[3]; IT; })
#define _DARKSYS_FOREACH_6(SYSTEM, A, B, C, D, E, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; C = _pool[2]; D = _pool[3]; E = _pool[4]; IT; })

#define _DARKSYS_FOREACH(SYSTEM, CODE) \
    do                                 \
    {                                  \
        darksys *s = (SYSTEM);         \
        uint16_t _count = s->count;    \
        void **_pool = s->pool;        \
        uint16_t _params = s->params;  \
                                       \
        while (_count)                 \
        {                              \
            CODE;                      \
            _pool += _params;          \
            --_count;                  \
        }                              \
    } while (0)

#define _DARKSYS_NARGS(...) _DARKSYS_NARGS_I(__VA_ARGS__, 5, 4, 3, 2, 1)
#define _DARKSYS_NARGS_I(_1, _2, _3, _4, _5, N, ...) N

#define _DARKSYS_WRITE_N(N, SYSTEM, ...) _DARKSYS_WRITE_N_I(N, SYSTEM, __VA_ARGS__)
#define _DARKSYS_WRITE_N_I(N, SYSTEM, ...) _DARKSYS_WRITE_##N(SYSTEM, __VA_ARGS__)

#define _DARKSYS_WRITE_1(SYSTEM, A) \
    ((SYSTEM)->pool[(SYSTEM)->size] = (A))

#define _DARKSYS_WRITE_2(SYSTEM, A, B) \
    (_DARKSYS_WRITE_1(SYSTEM, A),      \
     (SYSTEM)->pool[(SYSTEM)->size + 1] = (B))

#define _DARKSYS_WRITE_3(SYSTEM, A, B, C) \
    (_DARKSYS_WRITE_2(SYSTEM, A, B),      \
     (SYSTEM)->pool[(SYSTEM)->size + 2] = (C))

#define _DARKSYS_WRITE_4(SYSTEM, A, B, C, D) \
    (_DARKSYS_WRITE_3(SYSTEM, A, B, C),      \
     (SYSTEM)->pool[(SYSTEM)->size + 3] = (D))

#define _DARKSYS_WRITE_5(SYSTEM, A, B, C, D, E) \
    (_DARKSYS_WRITE_4(SYSTEM, A, B, C, D),      \
     (SYSTEM)->pool[(SYSTEM)->size + 4] = (E))

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DARKSYS_IMPLEMENTATION

int16_t darksys_add(darksys *s)
{
    if (s->count >= s->capacity)
        return -1;

    uint16_t handle;
    uint16_t slot = s->count++;

    if (s->free_count)
    {
        handle = s->free_head;
        s->free_head = s->lookup[handle];
        --s->free_count;
    }
    else
    {
        if (s->next >= s->capacity)
        {
            --s->count;
            return -2;
        }

        handle = s->next++;
    }

    s->handles[slot] = handle;
    s->lookup[handle] = slot;

    return handle;
}

void **darksys_data(darksys *s, uint16_t handle)
{
    if (handle >= s->next)
        return 0;

    uint16_t slot = s->lookup[handle];

    if (slot >= s->count)
        return 0;

    if (s->handles[slot] != handle)
        return 0;

    return s->pool + (slot * s->params);
}

int16_t darksys_remove(darksys *s, uint16_t handle)
{
    if (handle >= s->next)
        return -1;

    uint16_t slot = s->lookup[handle];

    if (slot >= s->count)
        return -2;

    if (s->handles[slot] != handle)
        return -3;

    --s->count;

    if (slot != s->count)
    {
        uint16_t dst = slot * s->params;
        uint16_t src = s->count * s->params;

        for (uint16_t i = 0; i < s->params; ++i)
            s->pool[dst + i] = s->pool[src + i];

        s->handles[slot] = s->handles[s->count];
        s->lookup[s->handles[slot]] = slot;
    }

    s->size -= s->params;

    s->lookup[handle] = s->free_head;
    s->free_head = handle;
    ++s->free_count;

    return s->count;
}

void darksys_clear(darksys *s)
{
    s->count = 0;
    s->size = 0;
    s->next = 0;
    s->free_head = 0;
    s->free_count = 0;
}

#endif // DARKSYS_IMPLEMENTATION