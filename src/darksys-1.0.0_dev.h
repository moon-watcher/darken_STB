#pragma once

#include <stdint.h>

typedef uint16_t darksys_handle;

#define DARKSYS_INVALID_HANDLE ((darksys_handle)0xFFFFu)

typedef struct
{
    void **pool;
    uint16_t *lookup;
    uint16_t *handles;

    uint16_t capacity;
    uint16_t params;
    uint16_t count;
    uint16_t next;
    uint16_t free_head;
} darksys;

/* ============================================================================
 * PRIVATE
 * ============================================================================ */

#define _DARKSYS_NARGS(...) _DARKSYS_NARGS_I(__VA_ARGS__, 5, 4, 3, 2, 1)
#define _DARKSYS_NARGS_I(_1, _2, _3, _4, _5, N, ...) N

#define _DARKSYS_WRITE_N(N, SYSTEM, ...) _DARKSYS_WRITE_N_I(N, SYSTEM, __VA_ARGS__)
#define _DARKSYS_WRITE_N_I(N, SYSTEM, ...) _DARKSYS_WRITE_##N(SYSTEM, __VA_ARGS__)

#define _DARKSYS_WRITE_1(SYSTEM, A) \
    ((SYSTEM)->pool[((SYSTEM)->count - 1) * (SYSTEM)->params] = (A))

#define _DARKSYS_WRITE_2(SYSTEM, A, B) \
    (_DARKSYS_WRITE_1(SYSTEM, A),      \
     (SYSTEM)->pool[((SYSTEM)->count - 1) * (SYSTEM)->params + 1] = (B))

#define _DARKSYS_WRITE_3(SYSTEM, A, B, C) \
    (_DARKSYS_WRITE_2(SYSTEM, A, B),      \
     (SYSTEM)->pool[((SYSTEM)->count - 1) * (SYSTEM)->params + 2] = (C))

#define _DARKSYS_WRITE_4(SYSTEM, A, B, C, D) \
    (_DARKSYS_WRITE_3(SYSTEM, A, B, C),      \
     (SYSTEM)->pool[((SYSTEM)->count - 1) * (SYSTEM)->params + 3] = (D))

#define _DARKSYS_WRITE_5(SYSTEM, A, B, C, D, E) \
    (_DARKSYS_WRITE_4(SYSTEM, A, B, C, D),      \
     (SYSTEM)->pool[((SYSTEM)->count - 1) * (SYSTEM)->params + 4] = (E))

#define _DARKSYS_FOREACH_DISPATCH(N, SYSTEM, ...) _DARKSYS_FOREACH_DISPATCH_I(N, SYSTEM, __VA_ARGS__)
#define _DARKSYS_FOREACH_DISPATCH_I(N, SYSTEM, ...) _DARKSYS_FOREACH_##N(SYSTEM, __VA_ARGS__)

#define _DARKSYS_FOREACH_NARGS(...) _DARKSYS_FOREACH_NARGS_I(__VA_ARGS__, 6, 5, 4, 3, 2, 1, 0)
#define _DARKSYS_FOREACH_NARGS_I(_1, _2, _3, _4, _5, _6, N, ...) N

#define _DARKSYS_FOREACH_2(SYSTEM, A, IT) _DARKSYS_FOREACH_RUN(SYSTEM, A = _pool[0]; IT)
#define _DARKSYS_FOREACH_3(SYSTEM, A, B, IT) _DARKSYS_FOREACH_RUN(SYSTEM, A = _pool[0]; B = _pool[1]; IT)
#define _DARKSYS_FOREACH_4(SYSTEM, A, B, C, IT) _DARKSYS_FOREACH_RUN(SYSTEM, A = _pool[0]; B = _pool[1]; C = _pool[2]; IT)
#define _DARKSYS_FOREACH_5(SYSTEM, A, B, C, D, IT) _DARKSYS_FOREACH_RUN(SYSTEM, A = _pool[0]; B = _pool[1]; C = _pool[2]; D = _pool[3]; IT)
#define _DARKSYS_FOREACH_6(SYSTEM, A, B, C, D, E, IT) _DARKSYS_FOREACH_RUN(SYSTEM, A = _pool[0]; B = _pool[1]; C = _pool[2]; D = _pool[3]; E = _pool[4]; IT)

#define _DARKSYS_FOREACH_RUN(SYSTEM, CODE) \
    do                                     \
    {                                      \
        darksys *s = (SYSTEM);             \
        void **_pool = s->pool;            \
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

#define DARKSYS_POOL_ALLOC(ALLOC, CAPACITY, PARAMS)                       \
    {                                                                     \
        .pool = (void **)(ALLOC)((CAPACITY) * (PARAMS) * sizeof(void *)), \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),     \
        .handles = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),    \
        .capacity = (CAPACITY),                                           \
        .params = (PARAMS),                                               \
        .count = 0,                                                       \
        .next = 0,                                                        \
        .free_head = DARKSYS_INVALID_HANDLE,                              \
    }

#define DARKSYS_POOL_FREE(FREE, SYSTEM) \
    do                                  \
    {                                   \
        (FREE)((SYSTEM)->pool);         \
        (FREE)((SYSTEM)->lookup);       \
        (FREE)((SYSTEM)->handles);      \
    } while (0)

#define DARKSYS_POOL_DECLARE(NAME, CAPACITY, PARAMS) \
    struct                                           \
    {                                                \
        void *pool[(CAPACITY) * (PARAMS)];           \
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

#define DARKSYS_DATA(SYSTEM, HANDLE) \
    ((SYSTEM)->pool + (SYSTEM)->lookup[(HANDLE)] * (SYSTEM)->params)

#define DARKSYS_ADD(SYSTEM, ...)                                                                                \
    ({                                                                                                          \
        darksys *_s = (SYSTEM);                                                                                 \
        darksys_handle _handle = DARKSYS_INVALID_HANDLE;                                                        \
        if (_DARKSYS_NARGS(__VA_ARGS__) == _s->params && (_handle = darksys_add(_s)) != DARKSYS_INVALID_HANDLE) \
            _DARKSYS_WRITE_N(_DARKSYS_NARGS(__VA_ARGS__), _s, __VA_ARGS__);                                     \
        _handle;                                                                                                \
    })

#define DARKSYS_FOREACH(SYSTEM, ...) _DARKSYS_FOREACH_DISPATCH(_DARKSYS_FOREACH_NARGS(__VA_ARGS__), SYSTEM, __VA_ARGS__)

/* ============================================================================
 * FUNCTIONS
 * ============================================================================ */

static inline darksys_handle darksys_add(darksys *s)
{
    if (s->count >= s->capacity || (s->free_head == DARKSYS_INVALID_HANDLE && s->next >= s->capacity))
        return DARKSYS_INVALID_HANDLE;

    darksys_handle handle = s->free_head;

    if (s->free_head != DARKSYS_INVALID_HANDLE)
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

static inline void darksys_remove(darksys *s, darksys_handle handle)
{
    uint16_t slot = s->lookup[handle];
    uint16_t last = --s->count;

    if (slot != last)
    {
        uint16_t params = s->params;
        void **dst = s->pool + slot * params;
        void **src = s->pool + last * params;

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
