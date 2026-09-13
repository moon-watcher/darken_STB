#pragma once

#include <stdint.h>

typedef uint16_t darksys9_handle_t;

#define DARKSYS9_INVALID_HANDLE ((darksys9_handle_t)0xFFFFu)

typedef struct
{
    void **pool;
    uint16_t *lookup;
    uint16_t *handles;
    uint16_t capacity;
    uint16_t size;
    uint16_t params;
    uint16_t count;
    uint16_t free_head;
} darksys9;

/* ================================================================
 * STORAGE
 * ================================================================ */

#define DARKSYS9_POOL_ALLOC(ALLOC, CAPACITY, PARAMS)                      \
    {                                                                     \
        .pool = (void **)(ALLOC)((CAPACITY) * (PARAMS) * sizeof(void *)), \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),     \
        .handles = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),    \
        .capacity = (CAPACITY),                                           \
        .size = 0,                                                        \
        .params = (PARAMS),                                               \
        .count = 0,                                                       \
        .free_head = DARKSYS9_INVALID_HANDLE,                             \
    }

#define DARKSYS9_POOL_DECLARE(NAME, CAPACITY, PARAMS) \
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

#define DARKSYS9_POOL_INIT(STORAGE, CAPACITY, PARAMS) \
    {                                                 \
        .pool = (STORAGE).pool,                       \
        .lookup = (STORAGE).lookup,                   \
        .handles = (STORAGE).handles,                 \
        .capacity = (CAPACITY),                       \
        .size = 0,                                    \
        .params = (PARAMS),                           \
        .count = 0,                                   \
        .free_head = DARKSYS9_INVALID_HANDLE,         \
    }

#define DARKSYS9_POOL_BIND(NAME)              \
    {                                         \
        .pool = (NAME).pool,                  \
        .lookup = (NAME).lookup,              \
        .handles = (NAME).handles,            \
        .capacity = (NAME).capacity,          \
        .size = 0,                            \
        .params = (NAME).params,              \
        .count = 0,                           \
        .free_head = DARKSYS9_INVALID_HANDLE, \
    }

/* ================================================================
 * ADD
 * ================================================================ */

#define DARKSYS9_ADD(SYSTEM, ...)                                             \
    ({                                                                        \
        darksys9 *_s = (SYSTEM);                                              \
        int16_t _handle = -3;                                                 \
        if (_DARKSYS9_NARGS(__VA_ARGS__) == _s->params &&                     \
            (_handle = darksys9_add(_s)) >= 0)                                \
        {                                                                     \
            _DARKSYS9_WRITE_N(_DARKSYS9_NARGS(__VA_ARGS__), _s, __VA_ARGS__); \
            _s->size += _s->params;                                           \
        }                                                                     \
        _handle;                                                              \
    })

#define _DARKSYS9_NARGS(...) \
    _DARKSYS9_NARGS_I(__VA_ARGS__, 5, 4, 3, 2, 1)

#define _DARKSYS9_NARGS_I(_1, _2, _3, _4, _5, N, ...) N

#define _DARKSYS9_WRITE_N(N, SYSTEM, ...) \
    _DARKSYS9_WRITE_N_I(N, SYSTEM, __VA_ARGS__)

#define _DARKSYS9_WRITE_N_I(N, SYSTEM, ...) \
    _DARKSYS9_WRITE_##N(SYSTEM, __VA_ARGS__)

#define _DARKSYS9_WRITE_1(SYSTEM, A) \
    ((SYSTEM)->pool[(SYSTEM)->size] = (A))

#define _DARKSYS9_WRITE_2(SYSTEM, A, B) \
    (_DARKSYS9_WRITE_1(SYSTEM, A),      \
     (SYSTEM)->pool[(SYSTEM)->size + 1] = (B))

#define _DARKSYS9_WRITE_3(SYSTEM, A, B, C) \
    (_DARKSYS9_WRITE_2(SYSTEM, A, B),      \
     (SYSTEM)->pool[(SYSTEM)->size + 2] = (C))

#define _DARKSYS9_WRITE_4(SYSTEM, A, B, C, D) \
    (_DARKSYS9_WRITE_3(SYSTEM, A, B, C),      \
     (SYSTEM)->pool[(SYSTEM)->size + 3] = (D))

#define _DARKSYS9_WRITE_5(SYSTEM, A, B, C, D, E) \
    (_DARKSYS9_WRITE_4(SYSTEM, A, B, C, D),      \
     (SYSTEM)->pool[(SYSTEM)->size + 4] = (E))

/* ================================================================
 * FOREACH
 * ================================================================ */

#define DARKSYS9_FOREACH(SYSTEM, ...) \
    _DARKSYS9_FOREACH_DISPATCH(_DARKSYS9_FOREACH_NARGS(__VA_ARGS__), SYSTEM, __VA_ARGS__)

#define _DARKSYS9_FOREACH_DISPATCH(N, SYSTEM, ...) \
    _DARKSYS9_FOREACH_DISPATCH_I(N, SYSTEM, __VA_ARGS__)

#define _DARKSYS9_FOREACH_DISPATCH_I(N, SYSTEM, ...) \
    _DARKSYS9_FOREACH_##N(SYSTEM, __VA_ARGS__)

#define _DARKSYS9_FOREACH_NARGS(...) \
    _DARKSYS9_FOREACH_NARGS_I(__VA_ARGS__, 6, 5, 4, 3, 2, 1)

#define _DARKSYS9_FOREACH_NARGS_I(_1, _2, _3, _4, _5, _6, N, ...) N

#define _DARKSYS9_FOREACH_1(SYSTEM, IT) \
    do \
    { \
        darksys9 *_s = (SYSTEM); \
        void **_p = _s->pool; \
        uint16_t _n = _s->count; \
        uint16_t _step = _s->params; \
        while (_n--) \
        { \
            void **_pool = _p; \
            IT; \
            _p += _step; \
        } \
    } while (0)

#define _DARKSYS9_FOREACH_2(SYSTEM, A, IT) \
    do \
    { \
        darksys9 *_s = (SYSTEM); \
        void **_p = _s->pool; \
        uint16_t _n = _s->count; \
        uint16_t _step = _s->params; \
        while (_n--) \
        { \
            void **_pool = _p; \
            A = _pool[0]; \
            IT; \
            _p += _step; \
        } \
    } while (0)

#define _DARKSYS9_FOREACH_3(SYSTEM, A, B, IT) \
    do \
    { \
        darksys9 *_s = (SYSTEM); \
        void **_p = _s->pool; \
        uint16_t _n = _s->count; \
        uint16_t _step = _s->params; \
        while (_n--) \
        { \
            void **_pool = _p; \
            A = _pool[0]; \
            B = _pool[1]; \
            IT; \
            _p += _step; \
        } \
    } while (0)

#define _DARKSYS9_FOREACH_4(SYSTEM, A, B, C, IT) \
    do \
    { \
        darksys9 *_s = (SYSTEM); \
        void **_p = _s->pool; \
        uint16_t _n = _s->count; \
        uint16_t _step = _s->params; \
        while (_n--) \
        { \
            void **_pool = _p; \
            A = _pool[0]; \
            B = _pool[1]; \
            C = _pool[2]; \
            IT; \
            _p += _step; \
        } \
    } while (0)

#define _DARKSYS9_FOREACH_5(SYSTEM, A, B, C, D, IT) \
    do \
    { \
        darksys9 *_s = (SYSTEM); \
        void **_p = _s->pool; \
        uint16_t _n = _s->count; \
        uint16_t _step = _s->params; \
        while (_n--) \
        { \
            void **_pool = _p; \
            A = _pool[0]; \
            B = _pool[1]; \
            C = _pool[2]; \
            D = _pool[3]; \
            IT; \
            _p += _step; \
        } \
    } while (0)

#define _DARKSYS9_FOREACH_6(SYSTEM, A, B, C, D, E, IT) \
    do \
    { \
        darksys9 *_s = (SYSTEM); \
        void **_p = _s->pool; \
        uint16_t _n = _s->count; \
        uint16_t _step = _s->params; \
        while (_n--) \
        { \
            void **_pool = _p; \
            A = _pool[0]; \
            B = _pool[1]; \
            C = _pool[2]; \
            D = _pool[3]; \
            E = _pool[4]; \
            IT; \
            _p += _step; \
        } \
    } while (0)

#define DARKSYS9_FOREACH(SYSTEM, CODE) \
    do \
    { \
        darksys9 *_s = (SYSTEM); \
        void **_p = _s->pool; \
        uint16_t _n = _s->count; \
        uint16_t _step = _s->params; \
        while (_n--) \
        { \
            void **_pool = _p; \
            CODE; \
            _p += _step; \
        } \
    } while (0)

/* ================================================================
 * INIT
 * ================================================================ */

static inline void darksys9_init(darksys9 *s)
{
    s->size = 0;
    s->count = 0;
    s->free_head = DARKSYS9_INVALID_HANDLE;

    if (s->capacity)
    {
        s->free_head = 0;

        for (uint16_t i = 0; i < s->capacity; i++)
            s->lookup[i] = i + 1;

        s->lookup[s->capacity - 1] = DARKSYS9_INVALID_HANDLE;
    }
}

/* ================================================================
 * ADD
 * ================================================================ */

static inline int16_t darksys9_add(darksys9 *s)
{
    if (s->count >= s->capacity)
        return -1;

    if (s->free_head == DARKSYS9_INVALID_HANDLE)
        return -2;

    uint16_t handle = s->free_head;
    s->free_head = s->lookup[handle];

    uint16_t slot = s->count++;

    s->handles[slot] = handle;
    s->lookup[handle] = slot;

    return (int16_t)handle;
}

/* ================================================================
 * DATA
 * ================================================================ */

static inline void **darksys9_data(darksys9 *s, darksys9_handle_t handle)
{
    if (handle >= s->capacity)
        return 0;

    uint16_t slot = s->lookup[handle];

    if (slot >= s->count)
        return 0;

    if (s->handles[slot] != handle)
        return 0;

    return s->pool + slot * s->params;
}

/* ================================================================
 * REMOVE
 * ================================================================ */

static inline int16_t darksys9_remove(darksys9 *s, darksys9_handle_t handle)
{
    if (handle >= s->capacity)
        return -1;

    uint16_t slot = s->lookup[handle];

    if (slot >= s->count)
        return -2;

    if (s->handles[slot] != handle)
        return -3;

    uint16_t last = --s->count;

    if (slot != last)
    {
        void **dst = s->pool + slot * s->params;
        void **src = s->pool + last * s->params;

        for (uint16_t i = 0; i < s->params; i++)
            dst[i] = src[i];

        uint16_t moved_handle = s->handles[last];

        s->handles[slot] = moved_handle;
        s->lookup[moved_handle] = slot;
    }

    s->size -= s->params;
    s->lookup[handle] = s->free_head;
    s->free_head = handle;

    return (int16_t)s->count;
}

/* ================================================================
 * CLEAR
 * ================================================================ */

static inline void darksys9_clear(darksys9 *s)
{
    darksys9_init(s);
}
