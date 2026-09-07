/**
 * darksys.h
 *
 * darksys-1.1.0_dev
 *
 * System: Flat packed pool of data pointers.
 *
 * Each item/entity occupies `params` consecutive pointers:
 *
 *     params = 2
 *     [A.a, A.b] [B.a, B.b] [C.a, C.b]...
 *
 * `capacity` is expressed in GROUPS/items.
 * `count` is the number of occupied GROUPS/items.
 * `size` is the number of occupied POINTERS.
 *
 * Each group is identified by its slot.
 */

#pragma once

#include <stdint.h>

typedef struct
{
    void **pool;
    uint16_t capacity; // Maximum number of groups/items
    uint16_t size;     // Number of occupied pointers
    uint16_t params;   // Number of pointers per group
    uint16_t limit;    // Maximum number of pointers
    uint16_t count;    // Number of occupied groups/items
} darksys;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

// Dynamic allocation:
//     darksys system = DARKSYS_POOL_ALLOC(malloc, 100, 3);
//     uint16_t slot = DARKSYS_ADD(&system, entity, position, velocity);
//     darksys_remove(&system, slot);
//     free(system.pool);
#define DARKSYS_POOL_ALLOC(ALLOC, CAPACITY, PARAMS)                       \
    {                                                                     \
        .pool = (void **)(ALLOC)((CAPACITY) * (PARAMS) * sizeof(void *)), \
        .capacity = (CAPACITY),                                           \
        .size = 0,                                                        \
        .params = (PARAMS),                                               \
        .limit = (CAPACITY) * (PARAMS),                                   \
        .count = 0,                                                       \
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
    } NAME = {                                       \
        .capacity = (CAPACITY),                     \
        .params = (PARAMS),                         \
    }

// Static/global initialization
#define DARKSYS_POOL_INIT(STORAGE, CAPACITY, PARAMS) \
    {                                                \
        .pool = (STORAGE).pool,                      \
        .capacity = (CAPACITY),                      \
        .size = 0,                                   \
        .params = (PARAMS),                          \
        .limit = (CAPACITY) * (PARAMS),              \
        .count = 0,                                  \
    }

// Runtime binding
#define DARKSYS_POOL_BIND(NAME)                   \
    {                                             \
        .pool = (NAME).pool,                      \
        .capacity = (NAME).capacity,              \
        .size = 0,                                \
        .params = (NAME).params,                  \
        .limit = (NAME).capacity * (NAME).params, \
        .count = 0,                               \
    }

// The number of arguments must match `params`.
//     uint16_t slot = DARKSYS_ADD(&system, A);
//     uint16_t slot = DARKSYS_ADD(&system, A, B, C);
//
// Returns:
//     0 .. capacity - 1 : slot
//     -1                : pool full
#define DARKSYS_ADD(SYSTEM, ...) ({                                             \
    darksys *s = (SYSTEM);                                                      \
    (s->count < s->capacity) ? _DARKSYS_WRITE(s, __VA_ARGS__), s->count++ : -1; \
})

#define DARKSYS_FOREACH(SYSTEM, ...) _DARKSYS_FOREACH_DISPATCH(_DARKSYS_FOREACH_NARGS(__VA_ARGS__), SYSTEM, __VA_ARGS__)

uint16_t darksys_remove(darksys *, uint16_t);
void darksys_clear(darksys *);

/* ============================================================================
 * PRIVATE
 * ============================================================================ */

#define _DARKSYS_NARGS(...) _DARKSYS_NARGS_I(__VA_ARGS__, 5, 4, 3, 2, 1)
#define _DARKSYS_NARGS_I(_1, _2, _3, _4, _5, N, ...) N

#define _DARKSYS_WRITE(SYSTEM, ...) _DARKSYS_WRITE_N(_DARKSYS_NARGS(__VA_ARGS__), SYSTEM, __VA_ARGS__)
#define _DARKSYS_WRITE_N(N, SYSTEM, ...) _DARKSYS_WRITE_N_I(N, SYSTEM, __VA_ARGS__)
#define _DARKSYS_WRITE_N_I(N, SYSTEM, ...) _DARKSYS_WRITE_##N(SYSTEM, __VA_ARGS__)

#define _DARKSYS_WRITE_1(SYSTEM, A) \
    (SYSTEM)->pool[(SYSTEM)->size++] = (A)

#define _DARKSYS_WRITE_2(SYSTEM, A, B) \
    _DARKSYS_WRITE_1(SYSTEM, A);       \
    _DARKSYS_WRITE_1(SYSTEM, B)

#define _DARKSYS_WRITE_3(SYSTEM, A, B, C) \
    _DARKSYS_WRITE_2(SYSTEM, A, B);       \
    _DARKSYS_WRITE_1(SYSTEM, C)

#define _DARKSYS_WRITE_4(SYSTEM, A, B, C, D) \
    _DARKSYS_WRITE_3(SYSTEM, A, B, C);       \
    _DARKSYS_WRITE_1(SYSTEM, D)

#define _DARKSYS_WRITE_5(SYSTEM, A, B, C, D, E) \
    _DARKSYS_WRITE_4(SYSTEM, A, B, C, D);       \
    _DARKSYS_WRITE_1(SYSTEM, E)

#define _DARKSYS_FOREACH_DISPATCH(N, ...) _DARKSYS_FOREACH_DISPATCH_I(N, __VA_ARGS__)
#define _DARKSYS_FOREACH_DISPATCH_I(N, ...) _DARKSYS_FOREACH_##N(__VA_ARGS__)

#define _DARKSYS_FOREACH_NARGS(...) _DARKSYS_FOREACH_NARGS_I(__VA_ARGS__, 5, 4, 3, 2, 1, 0)
#define _DARKSYS_FOREACH_NARGS_I(_1, _2, _3, _4, _5, _6, N, ...) N

#define _DARKSYS_FOREACH_0(SYSTEM, IT) _DARKSYS_FOREACH(SYSTEM, { IT; })
#define _DARKSYS_FOREACH_1(SYSTEM, A, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; IT; })
#define _DARKSYS_FOREACH_2(SYSTEM, A, B, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; IT; })
#define _DARKSYS_FOREACH_3(SYSTEM, A, B, C, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; C = _pool[2]; IT; })
#define _DARKSYS_FOREACH_4(SYSTEM, A, B, C, D, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; C = _pool[2]; D = _pool[3]; IT; })
#define _DARKSYS_FOREACH_5(SYSTEM, A, B, C, D, E, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; C = _pool[2]; D = _pool[3]; E = _pool[4]; IT; })

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

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DARKSYS_IMPLEMENTATION

uint16_t darksys_remove(darksys *s, uint16_t slot)
{
    if (slot >= s->count)
        return 0;

    uint16_t last = (uint16_t)(s->count - 1u);

    if (slot != last)
    {
        uint16_t dst = (uint16_t)(slot * s->params);
        uint16_t src = (uint16_t)(last * s->params);

        for (uint16_t i = 0; i < s->params; ++i)
            s->pool[dst + i] = s->pool[src + i];
    }

    --s->count;
    s->size -= s->params;

    return 1;
}

void darksys_clear(darksys *s)
{
    s->count = 0;
    s->size = 0;
}

#endif // DARKSYS_IMPLEMENTATION