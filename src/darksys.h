/**
 * darksys.h
 *
 * System: Flat packed pool of data pointers.
 *
 * Each item/entity occupies `params` consecutive pointers:
 *
 *     params = 2
 *
 *     [A.a, A.b] [B.a, B.b] [C.a, C.b]...
 *
 * `capacity` is expressed in GROUPS/items.
 * `size` is expressed in occupied POINTERS.
 */

#pragma once

#include <stdint.h>

typedef struct
{
    void **pool;
    uint16_t capacity; // Number of groups/items the pool can contain
    uint16_t size;     // Number of pointers currently stored
    uint16_t params;   // Number of pointers associated with each group
    uint16_t limit;    // Maximum number of pointers
} darksys;

/* ============================================================================
 * PUBLIC API
 * ========================================================================== */

// Dynamic allocation
//     darksys m = DARKSYS_POOL_ALLOC(MEM_alloc, 5, 2);
//     DARKSYS_ADD(&m, a);
//     DARKSYS_ADD(&m, b);
//     free(m.pool);
#define DARKSYS_POOL_ALLOC(ALLOC, CAPACITY, PARAMS)                       \
    {                                                                     \
        .pool = (void **)(ALLOC)((CAPACITY) * (PARAMS) * sizeof(void *)), \
        .capacity = (CAPACITY),                                           \
        .size = 0,                                                        \
        .params = (PARAMS),                                               \
        .limit = (CAPACITY) * (PARAMS),                                   \
    }

// Static allocation
//     DARKSYS_POOL_DECLARE(storage, 5, 2);
//     darksys m = DARKSYS_POOL_BIND(storage);
#define DARKSYS_POOL_DECLARE(NAME, CAPACITY, PARAMS) \
    struct                                           \
    {                                                \
        uint16_t capacity;                           \
        uint16_t params;                             \
        void *pool[(CAPACITY) * (PARAMS)];           \
    } NAME = {                                       \
        .capacity = (CAPACITY),                      \
        .params = (PARAMS),                          \
    }

// Static/global initialization
#define DARKSYS_POOL_INIT(STORAGE, CAPACITY, PARAMS) \
    {                                                \
        .pool = (STORAGE).pool,                      \
        .capacity = (CAPACITY),                      \
        .size = 0,                                   \
        .params = (PARAMS),                          \
        .limit = (CAPACITY) * (PARAMS),              \
    }

// Runtime binding
#define DARKSYS_POOL_BIND(NAME)                   \
    {                                             \
        .pool = (NAME).pool,                      \
        .capacity = (NAME).capacity,              \
        .size = 0,                                \
        .params = (NAME).params,                  \
        .limit = (NAME).capacity * (NAME).params, \
    }

#define DARKSYS_ADD(SYSTEM, VALUE) ({                      \
    darksys *s = (SYSTEM);                                 \
    s->size < s->limit ? s->pool[s->size++] = (VALUE) : 0; \
})

#define DARKSYS_FOREACH(...) _DARKSYS_FOREACH_DISPATCH(_DARKSYS_NARGS(__VA_ARGS__), __VA_ARGS__)

uint16_t darksys_remove(darksys *, void *);
void darksys_clear(darksys *);

/* ============================================================================
 * INTERNAL MACRO IMPLEMENTATIONS
 * ============================================================================ */

#define _DARKSYS_FOREACH(SYSTEM, CODE) \
    do                                 \
    {                                  \
        darksys *s = (SYSTEM);         \
        uint16_t _size = s->size;      \
        void **_pool = s->pool;        \
        uint16_t _params = s->params;  \
                                       \
        while (_size)                  \
        {                              \
            CODE;                      \
            _pool += _params;          \
            _size -= _params;          \
        }                              \
    } while (0)

#define _DARKSYS_FOREACH_DISPATCH(N, ...) _DARKSYS_FOREACH_CONCAT(N, __VA_ARGS__)
#define _DARKSYS_FOREACH_CONCAT(N, ...) _DARKSYS_FOREACH_##N(__VA_ARGS__)

#define _DARKSYS_NARGS(...) _DARKSYS_NARGS_I(__VA_ARGS__, 5, 4, 3, 2, 1, 0, -1)
#define _DARKSYS_NARGS_I(_1, _2, _3, _4, _5, _6, _7, N, ...) N

#define _DARKSYS_FOREACH_0(SYSTEM, IT) _DARKSYS_FOREACH(SYSTEM, { IT; })
#define _DARKSYS_FOREACH_1(SYSTEM, A, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; IT; })
#define _DARKSYS_FOREACH_2(SYSTEM, A, B, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; IT; })
#define _DARKSYS_FOREACH_3(SYSTEM, A, B, C, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; C = _pool[2]; IT; })
#define _DARKSYS_FOREACH_4(SYSTEM, A, B, C, D, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; C = _pool[2]; D = _pool[3]; IT; })
#define _DARKSYS_FOREACH_5(SYSTEM, A, B, C, D, E, IT) _DARKSYS_FOREACH(SYSTEM, { A = _pool[0]; B = _pool[1]; C = _pool[2]; D = _pool[3]; E = _pool[4]; IT; })

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DARKSYS_IMPLEMENTATION

uint16_t darksys_remove(darksys *$, void *first)
{
    uint16_t params = $->params;
    void **pool = $->pool;
    uint16_t i = $->size;

    while (i)
    {
        i -= params;

        if (pool[i] != first)
            continue;

        uint16_t size = $->size -= params;

        if (i != size)
            while (params--)
                pool[i + params] = pool[size + params];

        return 1;
    }

    return 0;
}

void darksys_clear(darksys *$)
{
    $->size = 0;
}

#endif // DARKSYS_IMPLEMENTATION
