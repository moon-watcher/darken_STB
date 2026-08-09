#ifndef DARKEN_H
#define DARKEN_H

#include <genesis.h>

#define _DE_ALIGN4(X) (((X) + 3U) & ~3U)
#define _DE_ENTITY_STRIDE(PAYLOAD) _DE_ALIGN4(sizeof(de_entity) + (PAYLOAD))

typedef void *(*de_state)(void *);

#define de_state_delete ((de_state)0)
#define de_state_loop ((de_state)1)
#define de_state_pause ((de_state)2)

#define de_state_is_deleted(S) ((S) == de_state_delete)
#define de_state_is_loop(S) ((S) == de_state_loop)
#define de_state_is_paused(S) ((S) == de_state_pause)
#define de_state_is_active(S) ((S) > de_state_pause)

typedef struct de_manager de_manager;

typedef struct de_entity
{
    de_state state;
    de_state destructor;
    de_manager *manager;
    uint16_t slot;
    uint16_t tag;
    uint8_t data[];
} de_entity;

struct de_manager
{
    de_entity **items;
    uint16_t size;
    uint16_t capacity;
    uint16_t pause_index;
    void *ctx;
};

void de_entity_swap(de_entity *, de_entity *);
void de_entity_pause(de_entity *);
void de_entity_resume(de_entity *);
void de_entity_delete(de_entity *);

//

void de_manager_init(de_manager *, de_entity **, void *, uint16_t, uint16_t);
de_entity *de_manager_new(de_manager *);
void de_manager_pause(de_manager *);
void de_manager_resume(de_manager *);
void de_manager_reset(de_manager *);
void de_entity_update(de_entity *);
void de_manager_update(de_manager *);

#define de_manager_iterate(M, CODE) _de_manager_iterate(M, (M)->pause_index, CODE)
#define de_manager_iterateAll(M, CODE) _de_manager_iterate(M, 0, CODE)

#define _de_manager_iterate(M, LIMIT, CODE)        \
    do                                             \
    {                                              \
        uint16_t INDEX = (M)->size;                \
        while (INDEX-- > (LIMIT))                  \
        {                                          \
            de_entity *ENTITY = (M)->items[INDEX]; \
            CODE;                                  \
        }                                          \
    } while (0)

#define de_manager_apply(M, F, A) _de_manager_apply(de_manager_iterate, M, F, A)
#define de_manager_applyAll(M, F, A) _de_manager_apply(de_manager_iterateAll, M, F, A)

#define _de_manager_apply(ITER, M, FILTER, ACTION) \
    do                                             \
    {                                              \
        de_entity *_targets[(M)->size + 1];        \
        uint16_t _count = 0;                       \
        ITER(M, { if (FILTER) _targets[_count++] = ENTITY; });                              \
        while (_count--)                           \
            ACTION(_targets[_count]);              \
    } while (0)

#define _DE_CONCAT(A, B) A##B
#define _DE_UNIQUE(A, B) _DE_CONCAT(A, B)

#define de_manager_create(MGR, CAPACITY, PAYLOAD)                                                             \
    de_entity *_DE_UNIQUE(_i_, __LINE__)[(CAPACITY)];                                                         \
    uint8_t _DE_UNIQUE(_s_, __LINE__)[(CAPACITY) * _DE_ENTITY_STRIDE((PAYLOAD))] __attribute__((aligned(4))); \
    de_manager_init((MGR), _DE_UNIQUE(_i_, __LINE__), _DE_UNIQUE(_s_, __LINE__), (CAPACITY), (PAYLOAD))

#endif

#ifdef DARKEN_IMPLEMENTATION

void de_entity_update(de_entity *$)
{
    de_state s = $->state;

    if (de_state_is_active(s))
    {
        s = s($->data);

        if (!de_state_is_loop(s))
            $->state = s;
    }
}

void de_entity_swap(de_entity *a, de_entity *b)
{
    de_manager *m = a->manager;
    uint16_t i = a->slot;
    uint16_t j = b->slot;

    m->items[i] = b;
    b->slot = i;
    m->items[j] = a;
    a->slot = j;
}

void de_entity_pause(de_entity *$)
{
    de_manager *m = $->manager;

    if ($->slot >= m->pause_index && $->slot < m->size)
        de_entity_swap($, m->items[m->pause_index++]);
}

void de_entity_resume(de_entity *$)
{
    de_manager *m = $->manager;

    if ($->slot < m->pause_index)
        de_entity_swap($, m->items[--m->pause_index]);
}

void de_entity_delete(de_entity *$)
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
            de_entity_swap($, m->items[--m->pause_index]);

        --m->size;

        if ($->slot != m->size)
            de_entity_swap($, m->items[m->size]);
    }
}

//

void de_manager_init(de_manager *$, de_entity **items, void *storage, uint16_t capacity, uint16_t bytes)
{
    $->items = items;
    $->capacity = capacity;
    $->size = 0;
    $->pause_index = 0;
    $->ctx = 0;

    uint16_t stride = _DE_ENTITY_STRIDE(bytes);

    for (uint16_t i = 0; i < capacity; ++i)
        $->items[i] = (de_entity *)((uint8_t *)storage + i * stride);
}

de_entity *de_manager_new(de_manager *$)
{
    if ($->size >= $->capacity)
        return 0;

    de_entity *e = $->items[$->size];
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
        de_entity *e = $->items[i];
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

    while ($->size)
        de_entity_delete($->items[$->size - 1]);
}

#endif
