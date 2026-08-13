#ifndef DARKEN_H
#define DARKEN_H

#include <genesis.h>

typedef void *(*de_state)(void *);

typedef struct de_entity *de_entity;
typedef struct de_manager de_manager;
typedef struct de_system de_system;

struct de_entity
{
    de_state state;
    de_state destructor;
    de_manager *manager;
    uint16_t slot;
    uint16_t tag;
    uint8_t data[];
};

struct de_manager
{
    de_entity *items;
    uint16_t size;
    uint16_t capacity;
    uint16_t pause_index;
};

struct de_system
{
    void **pool;
    uint16_t size;
    uint16_t capacity;
    uint16_t params;
};

//

#define de_state_delete ((de_state)0)
#define de_state_loop ((de_state)1)
#define de_state_pause ((de_state)2)

#define de_state_is_deleted(S) ((S) == de_state_delete)
#define de_state_is_loop(S) ((S) == de_state_loop)
#define de_state_is_paused(S) ((S) == de_state_pause)
#define de_state_is_active(S) ((S) > de_state_pause)

//

void de_entity_exec(de_entity);
void de_entity_update(de_entity);
void de_entity_pause(de_entity);
void de_entity_resume(de_entity);
void de_entity_delete(de_entity);
void de_entity_move_front(de_entity);
void de_entity_move_back(de_entity);

//

#define de_manager_create(MGR, CAPACITY, PAYLOAD)                                                             \
    de_entity _DE_UNIQUE(_i_, __LINE__)[(CAPACITY)];                                                          \
    uint8_t _DE_UNIQUE(_s_, __LINE__)[(CAPACITY) * _DE_ENTITY_STRIDE((PAYLOAD))] __attribute__((aligned(4))); \
    de_manager_init((MGR), _DE_UNIQUE(_i_, __LINE__), _DE_UNIQUE(_s_, __LINE__), (CAPACITY), (PAYLOAD))

void de_manager_init(de_manager *, de_entity *, void *, uint16_t, uint16_t);
de_entity de_manager_new(de_manager *);
void de_manager_update(de_manager *);
void de_manager_pause(de_manager *);
void de_manager_resume(de_manager *);
void de_manager_reset(de_manager *);

#define de_manager_iterate(M, CODE) _de_manager_iterate(M, (M)->pause_index, CODE)
#define de_manager_iterateAll(M, CODE) _de_manager_iterate(M, 0, CODE)
#define de_manager_apply(M, F, A) _de_manager_apply(de_manager_iterate, M, F, A)
#define de_manager_applyAll(M, F, A) _de_manager_apply(de_manager_iterateAll, M, F, A)

//

#define de_system_create(SYS, CAPACITY, PARAMS)              \
    void *_DE_UNIQUE(_sp_, __LINE__)[(CAPACITY) * (PARAMS)]; \
    de_system_init((SYS), _DE_UNIQUE(_sp_, __LINE__), (CAPACITY), (PARAMS))

void de_system_init(de_system *, void **, uint16_t, uint16_t);
uint16_t de_system_remove(de_system *, void *);

#define de_system_add(...)                                           \
    _DE_GET_MACRO(__VA_ARGS__, _,                                    \
                  _de_system_add5, _de_system_add4, _de_system_add3, \
                  _de_system_add2, _de_system_add1, unused)(__VA_ARGS__)

#define de_system_foreach(...)                                                      \
    _DE_GET_MACRO(__VA_ARGS__,                                                      \
                  _de_system_foreach_5, _de_system_foreach_4, _de_system_foreach_3, \
                  _de_system_foreach_2, _de_system_foreach_1, _de_system_foreach_0)(__VA_ARGS__)

#define de_system_iterator(...)                                                        \
    _DE_GET_MACRO(__VA_ARGS__,                                                         \
                  _de_system_iterator_5, _de_system_iterator_4, _de_system_iterator_3, \
                  _de_system_iterator_2, _de_system_iterator_1, _de_system_iterator_0)(__VA_ARGS__)

//

#define _de_manager_iterate(M, LIMIT, CODE)       \
    do                                            \
    {                                             \
        uint16_t INDEX = (M)->size;               \
        while (INDEX-- > (LIMIT))                 \
        {                                         \
            de_entity ENTITY = (M)->items[INDEX]; \
            CODE;                                 \
        }                                         \
    } while (0)

#define _de_manager_apply(ITER, M, FILTER, ACTION) \
    do                                             \
    {                                              \
        de_entity _targets[(M)->size + 1];         \
        uint16_t _count = 0;                       \
        ITER(M, { if (FILTER) _targets[_count++] = ENTITY; });                              \
        while (_count--)                           \
            ACTION(_targets[_count]);              \
    } while (0)

#define _de_system_add(SYS, N, ...)          \
    ({                                       \
        de_system *_s = (SYS);               \
        uint16_t _ok = 0;                    \
        if (_s->size + (N) <= _s->capacity)  \
        {                                    \
            void **_p = &_s->pool[_s->size]; \
            __VA_ARGS__                      \
            _s->size += (N);                 \
            _ok = 1;                         \
        }                                    \
        _ok;                                 \
    })

#define _de_system_add1(SYS, A) \
    _de_system_add(SYS, 1, _p[0] = (void *)(A);)

#define _de_system_add2(SYS, A, B) \
    _de_system_add(SYS, 2, _p[0] = (void *)(A); _p[1] = (void *)(B);)

#define _de_system_add3(SYS, A, B, C) \
    _de_system_add(SYS, 3, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C);)

#define _de_system_add4(SYS, A, B, C, D) \
    _de_system_add(SYS, 4, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C); _p[3] = (void *)(D);)

#define _de_system_add5(SYS, A, B, C, D, E) \
    _de_system_add(SYS, 5, _p[0] = (void *)(A); _p[1] = (void *)(B); _p[2] = (void *)(C); _p[3] = (void *)(D); _p[4] = (void *)(E);)

#define _de_system_iterator(NAME, FOREACH, ...) \
    void *NAME(de_system *system)               \
    {                                           \
        FOREACH(system, __VA_ARGS__);           \
        return (void *)de_state_loop;           \
    }

#define _de_system_foreach(SYSTEM, IT)                     \
    void **items = (SYSTEM)->pool;                         \
    for (uint16_t i = 0, size = (SYSTEM)->size; i < size;) \
        IT;

#define _de_system_foreach_0(SYSTEM, IT) \
    _de_system_foreach(SYSTEM, { IT; })

#define _de_system_foreach_1(SYSTEM, A, IT) \
    _de_system_foreach(SYSTEM, { A = items[i++]; IT; })

#define _de_system_foreach_2(SYSTEM, A, B, IT) \
    _de_system_foreach(SYSTEM, { A = items[i++]; B = items[i++]; IT; })

#define _de_system_foreach_3(SYSTEM, A, B, C, IT) \
    _de_system_foreach(SYSTEM, { A = items[i++]; B = items[i++]; C = items[i++]; IT; })

#define _de_system_foreach_4(SYSTEM, A, B, C, D, IT) \
    _de_system_foreach(SYSTEM, { A = items[i++]; B = items[i++]; C = items[i++]; D = items[i++]; IT; })

#define _de_system_foreach_5(SYSTEM, A, B, C, D, E, IT) \
    _de_system_foreach(SYSTEM, { A = items[i++]; B = items[i++]; C = items[i++]; D = items[i++]; E = items[i++]; IT; })

#define _de_system_iterator_0(NAME, IT) \
    _de_system_iterator(NAME, _de_system_foreach_0, IT)

#define _de_system_iterator_1(NAME, A, IT) \
    _de_system_iterator(NAME, _de_system_foreach_1, A, IT)

#define _de_system_iterator_2(NAME, A, B, IT) \
    _de_system_iterator(NAME, _de_system_foreach_2, A, B, IT)

#define _de_system_iterator_3(NAME, A, B, C, IT) \
    _de_system_iterator(NAME, _de_system_foreach_3, A, B, C, IT)

#define _de_system_iterator_4(NAME, A, B, C, D, IT) \
    _de_system_iterator(NAME, _de_system_foreach_4, A, B, C, D, IT)

#define _de_system_iterator_5(NAME, A, B, C, D, E, IT) \
    _de_system_iterator(NAME, _de_system_foreach_5, A, B, C, D, E, IT)

#define _DE_ALIGN4(X) (((X) + 3U) & ~3U)
#define _DE_CONCAT(A, B) A##B
#define _DE_UNIQUE(A, B) _DE_CONCAT(A, B)
#define _DE_ENTITY_STRIDE(PAYLOAD) _DE_ALIGN4(sizeof(struct de_entity) + (PAYLOAD))
#define _DE_GET_MACRO(_1, _2, _3, _4, _5, _6, _7, NAME, ...) NAME

#endif

#ifdef DARKEN_IMPLEMENTATION

void de_entity_exec(de_entity $)
{
    if (!de_state_is_deleted($->state))
        $->state($->data);
}

void de_entity_update(de_entity $)
{
    de_state s = $->state;

    if (de_state_is_active(s))
    {
        s = s($->data);

        if (!de_state_is_loop(s))
            $->state = s;
    }
}

static void _de_entity_swap(de_entity a, de_entity b)
{
    de_manager *m = a->manager;
    uint16_t i = a->slot;
    uint16_t j = b->slot;

    m->items[i] = b;
    b->slot = i;
    m->items[j] = a;
    a->slot = j;
}

void de_entity_pause(de_entity $)
{
    de_manager *m = $->manager;

    if ($->slot >= m->pause_index && $->slot < m->size)
        _de_entity_swap($, m->items[m->pause_index++]);
}

void de_entity_resume(de_entity $)
{
    de_manager *m = $->manager;

    if ($->slot < m->pause_index)
        _de_entity_swap($, m->items[--m->pause_index]);
}

void de_entity_delete(de_entity $)
{
    de_manager *m = $->manager;

    if ($->slot >= m->size)
        return;

    $->state = de_state_delete;

    if ($->destructor)
        $->state = $->destructor($->data);

    if (de_state_is_deleted($->state))
    {
        if ($->slot < m->pause_index)
            _de_entity_swap($, m->items[--m->pause_index]);

        --m->size;

        if ($->slot != m->size)
            _de_entity_swap($, m->items[m->size]);
    }
}

void de_entity_move_front(de_entity $)
{
    de_manager *m = $->manager;

    if ($->slot < m->pause_index || $->slot >= m->size)
        return;

    de_entity last = m->items[m->size - 1];
    if ($ != last)
        _de_entity_swap($, last);
}

void de_entity_move_back(de_entity $)
{
    de_manager *m = $->manager;

    if ($->slot < m->pause_index || $->slot >= m->size)
        return;

    de_entity first = m->items[m->pause_index];
    if ($ != first)
        _de_entity_swap($, first);
}

//

void de_manager_init(de_manager *$, de_entity *items, void *storage, uint16_t capacity, uint16_t bytes)
{
    $->items = items;
    $->capacity = capacity;
    $->size = 0;
    $->pause_index = 0;

    uint16_t stride = _DE_ENTITY_STRIDE(bytes);

    for (uint16_t i = 0; i < capacity; ++i)
        $->items[i] = (de_entity)((uint8_t *)storage + i * stride);
}

de_entity de_manager_new(de_manager *$)
{
    if ($->size >= $->capacity)
        return 0;

    de_entity e = $->items[$->size];
    e->manager = $;
    e->slot = $->size++;
    e->state = de_state_delete;
    e->destructor = 0;
    e->tag = 0;

    return e;
}

void de_manager_update(de_manager *$)
{
    uint16_t i = $->size;

    while (i-- > $->pause_index)
    {
        de_entity e = $->items[i];
        de_state s = e->state;

        if (de_state_is_active(s))
        {
            s = s(e->data);

            if (!de_state_is_loop(s))
                e->state = s;
        }

        else if (de_state_is_paused(s))
            de_entity_pause(e);

        else if (de_state_is_deleted(s))
            de_entity_delete(e);
    }
}

void de_manager_pause(de_manager *$)
{
    $->pause_index = $->size;
}

void de_manager_resume(de_manager *$)
{
    $->pause_index = 0;
}

void de_manager_reset(de_manager *$)
{
    $->pause_index = 0;

    uint16_t remaining = $->size;
    while (remaining--)
        de_entity_delete($->items[$->size - 1]);
}

//

void de_system_init(de_system *$, void **storage, uint16_t capacity_groups, uint16_t params)
{
    $->pool = storage;
    $->size = 0;
    $->capacity = capacity_groups * params;
    $->params = params;
}

uint16_t de_system_remove(de_system *$, void *first)
{
    uint16_t params = $->params;

    if (!params)
        return 0;

    for (uint16_t i = 0; i < $->size; i += params)
        if ($->pool[i] == first)
        {
            $->size -= params;

            if (i != $->size)
                for (uint16_t k = 0; k < params; ++k)
                    $->pool[i + k] = $->pool[$->size + k];

            return 1;
        }

    return 0;
}

#endif