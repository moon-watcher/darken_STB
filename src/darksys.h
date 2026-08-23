/**
 * darksys.h
 *
 * Full documentation: README.Darksys.md
 *
 * GNU C note:
 * - This header uses GNU C statement expression.
 *
 *
 *
 * System: Flat packed pool of data pointers.
 *
 * The pool is organized in groups of 'params' pointers. Each group can hold
 * the pointers associated with one processed item/entity.
 *
 * Pool Layout (params=2):
 *    [A.a, A.b] [B.a, B.b] [C.a, C.b]...
 */

#ifndef DARKSYS_H
#define DARKSYS_H

#include <stdint.h>
#include <stdarg.h>

typedef struct
{
    // Pool
    void **pool;
    uint16_t capacity;
    uint16_t size;

    // Dedicated members
    uint16_t params;
} darksys;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

#define DARKSYS_STORAGE _DARKSYS_STORAGE
#define DARKSYS_ARGS _DARKSYS_ARGS
#define DARKSYS_FOREACH(...) _DARKSYS_FOREACH_DISPATCH(_DARKSYS_NARGS(__VA_ARGS__), __VA_ARGS__)

void darksys_init(darksys *, void **, uint16_t, uint16_t);
uint16_t darksys_add(darksys *, ...);
uint16_t darksys_remove(darksys *, void *);
void darksys_clear(darksys *);

/* ============================================================================
 * INTERNAL MACRO IMPLEMENTATIONS
 * ============================================================================ */

#define _DARKSYS_STORAGE(NAME, CAPACITY, PARAMS) \
    struct                                       \
    {                                            \
        void *pool[(CAPACITY) * (PARAMS)];       \
        uint16_t capacity;                       \
        uint16_t params;                         \
    } NAME = {                                   \
        .capacity = (CAPACITY),                  \
        .params = (PARAMS),                      \
    }

#define _DARKSYS_ARGS(NAME) \
    (NAME).pool, (NAME).capacity, (NAME).params

#define _DARKSYS_FOREACH(SYSTEM, CODE)      \
    do                                      \
    {                                       \
        darksys *_system = (SYSTEM);        \
        uint16_t _size = _system->size;     \
        void **_pool = _system->pool;       \
        uint16_t _params = _system->params; \
                                            \
        while (_size)                       \
        {                                   \
            CODE;                           \
            _pool += _params;               \
            _size -= _params;               \
        }                                   \
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

#endif // DARKSYS_H

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DARKSYS_IMPLEMENTATION

void darksys_init(darksys *$, void **storage, uint16_t capacity_groups, uint16_t params)
{
    $->pool = storage;
    $->capacity = capacity_groups * params;
    $->size = 0;
    $->params = params;
}

uint16_t darksys_add(darksys *$, ...)
{
    uint16_t size = $->size;
    uint16_t params = $->params;

    if (size + params > $->capacity)
        return 0;

    void **pool = $->pool + size;
    $->size = size + params;

    va_list args;
    va_start(args, $);

    while (params--)
        *pool++ = va_arg(args, void *);

    va_end(args);

    return 1;
}

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
