/**
 * darksys.h
 *
 * Full documentation: README.md
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

#include <stdi nt.h>

typedef struct de_system *de_system;

struct de_system
{
    void **pool;
    void **end;
    uint16_t capacity;
    uint16_t size;
    uint16_t params;
};

#define DE_SYSTEM_STORAGE _DE_SYSTEM_STORAGE
#define DE_SYSTEM_ARGS _DE_SYSTEM_ARGS
#define DE_SYSTEM_ADD(...) _DE_CONCAT(_DE_SYSTEM_ADD_, _DE_ADD_NARGS(__VA_ARGS__))(__VA_ARGS__)
#define DE_SYSTEM_FOREACH(...) _DE_CONCAT(_DE_SYSTEM_FOREACH_, _DE_FOREACH_NARGS(__VA_ARGS__))(__VA_ARGS__)

void de_system_init(de_system, void **, uint16_t, uint16_t);
uint16_t de_system_remove(de_system, void *);
void de_system_clear(de_system);

/* ============================================================================
 * INTERNAL MACRO IMPLEMENTATIONS
 * ============================================================================ */

#define _DE_ADD_NARGS(...) _DE_ADD_NARGS_I(__VA_ARGS__, 5, 4, 3, 2, 1, 0)
#define _DE_ADD_NARGS_I(_1, _2, _3, _4, _5, _6, N, ...) N
#define _DE_FOREACH_NARGS(...) _DE_FOREACH_NARGS_I(__VA_ARGS__, 5, 4, 3, 2, 1, 0, -1)
#define _DE_FOREACH_NARGS_I(_1, _2, _3, _4, _5, _6, _7, N, ...) N
#define _DE_CONCAT_INNER(A, B) A##B
#define _DE_CONCAT(A, B) _DE_CONCAT_INNER(A, B)

#define _DE_SYSTEM_STORAGE(NAME, CAPACITY, PARAMS) \
    struct                                         \
    {                                              \
        void *pool[(CAPACITY) * (PARAMS)];         \
        uint16_t capacity;                         \
        uint16_t params;                           \
    } NAME = {                                     \
        .capacity = (CAPACITY),                    \
        .params = (PARAMS),                        \
    }

#define _DE_SYSTEM_ARGS(NAME) \
    (NAME).pool, (NAME).capacity, (NAME).params

#define _DE_SYSTEM_ADD(SYSTEM, ARGS, ...)      \
    ({                                         \
        de_system _s = (SYSTEM);               \
        uint16_t _r = 0;                       \
                                               \
        if (_s->size + (ARGS) <= _s->capacity) \
        {                                      \
            void **_p = _s->end;               \
            __VA_ARGS__                        \
            _s->size += (ARGS);                \
            _s->end += (ARGS);                 \
            _r = 1;                            \
        }                                      \
        _r;                                    \
    })

#define _DE_SYSTEM_FOREACH(SYSTEM, IT) \
    do                                 \
    {                                  \
        de_system _s = (SYSTEM);       \
        void **_p = _s->pool;          \
        void **_e = _s->end;           \
        uint16_t _c = _s->params;      \
                                       \
        while (_p < _e)                \
        {                              \
            IT;                        \
            _p += _c;                  \
        }                              \
    } while (0)

#define _DE_SYSTEM_ADD_1(SYS, A) _DE_SYSTEM_ADD(SYS, 1, _p[0] = (void *)(A);)
#define _DE_SYSTEM_ADD_2(SYS, A, B) _DE_SYSTEM_ADD(SYS, 2, _p[0] = (void *)(A); _p[1] = (void *)(B);)
#define _DE_SYSTEM_ADD_3(SYS, A, B, C) _DE_SYSTEM_ADD(SYS, 3, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C);)
#define _DE_SYSTEM_ADD_4(SYS, A, B, C, D) _DE_SYSTEM_ADD(SYS, 4, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C); _p[3] = (void *)(D);)
#define _DE_SYSTEM_ADD_5(SYS, A, B, C, D, E) _DE_SYSTEM_ADD(SYS, 5, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C); _p[3] = (void *)(D); _p[4] = (void *)(E);)

#define _DE_SYSTEM_FOREACH_0(SYSTEM, IT) _DE_SYSTEM_FOREACH(SYSTEM, { IT; })
#define _DE_SYSTEM_FOREACH_1(SYSTEM, A, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = _p[0]; IT; })
#define _DE_SYSTEM_FOREACH_2(SYSTEM, A, B, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = _p[0]; B = _p[1]; IT; })
#define _DE_SYSTEM_FOREACH_3(SYSTEM, A, B, C, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = _p[0]; B = _p[1]; C = _p[2]; IT; })
#define _DE_SYSTEM_FOREACH_4(SYSTEM, A, B, C, D, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = _p[0]; B = _p[1]; C = _p[2]; D = _p[3]; IT; })
#define _DE_SYSTEM_FOREACH_5(SYSTEM, A, B, C, D, E, IT) _DE_SYSTEM_FOREACH(SYSTEM, { A = _p[0]; B = _p[1]; C = _p[2]; D = _p[3]; E = _p[4]; IT; })

#endif // DARKSYS_H

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DARKSYS_IMPLEMENTATION

void de_system_init(de_system $, void **storage, uint16_t capacity_groups, uint16_t params)
{
    $->pool = storage;
    $->end = storage;
    $->size = 0;
    $->capacity = capacity_groups * params;
    $->params = params;
}

uint16_t de_system_remove(de_system $, void *first)
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
        $->end -= params;

        if (i != size)
            while (params--)
                pool[i + params] = pool[size + params];

        return 1;
    }

    return 0;
}

void de_system_clear(de_system $)
{
    $->end = $->pool;
    $->size = 0;
}

#endif // DARKSYS_IMPLEMENTATION
