#ifndef DARKEN_H
#define DARKEN_H

#include <genesis.h>
// #include <stdint.h>

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

typedef struct de_manager
{
    de_entity **items;
    uint16_t size;
    uint16_t capacity;
    uint16_t pause_index;
} de_manager;

/* ============================================================
 * ALIGNMENT (mandatory on m68k, not an optimization)
 * ============================================================
 * The 68000 throws an Address Error / Illegal Instruction if a word
 * (16-bit) or long (32-bit) is accessed at an ODD address. de_entity
 * starts with function pointers (32-bit), so every entity inside the
 * storage buffer MUST land on an even address.
 *
 * Two separate things are needed for that, and BOTH matter:
 *   1) The STRIDE between entities must be even -> _DE_ALIGN2 below.
 *   2) The STORAGE BUFFER ITSELF must start at an even address, since
 *      a plain uint8_t[] has no alignment guarantee by default ->
 *      __attribute__((aligned(2))) on the array in de_manager_create.
 * If either one is missing, entities end up at odd addresses and the
 * CPU crashes as soon as it touches e->state / e->destructor.
 */
#define _DE_ALIGN2(X) (((X) + 1U) & ~1U)
#define _DE_ENTITY_STRIDE(PAYLOAD) (sizeof(de_entity) + _DE_ALIGN2(PAYLOAD))

void de_manager_init(de_manager *mgr, de_entity **items, void *storage, uint16_t capacity, uint16_t bytes)
{
    mgr->items = items;
    mgr->capacity = capacity;
    mgr->size = 0;
    mgr->pause_index = 0;

    /* Must use the SAME stride formula as de_manager_create's buffer-size
     * calculation below, or entities would walk past the end of storage. */
    uint16_t stride = _DE_ENTITY_STRIDE(bytes);

    for (uint16_t i = 0; i < mgr->capacity; ++i)
        mgr->items[i] = (de_entity *)((uint8_t *)storage + i * stride);
}

de_entity *de_manager_new(de_manager *mgr)
{
    if (mgr->size >= mgr->capacity)
        return 0;

    de_entity *e = mgr->items[mgr->size];
    e->manager = mgr;
    e->slot = mgr->size;
    e->state = (de_state)0;
    e->destructor = 0;
    e->tag = 0;

    ++mgr->size;

    return e;
}

void de_entity_swap(de_entity *a, de_entity *b)
{
    de_manager *mgr = a->manager;
    uint16_t i = a->slot;
    uint16_t j = b->slot;

    mgr->items[i] = b;
    b->slot = i;
    mgr->items[j] = a;
    a->slot = j;
}

void de_entity_pause(de_entity *e)
{
    de_manager *mgr = e->manager;

    if (e->slot >= mgr->pause_index && e->slot < mgr->size)
    {
        de_entity *other = mgr->items[mgr->pause_index];
        de_entity_swap(e, other);
        ++mgr->pause_index;
    }
}

void de_entity_resume(de_entity *e)
{
    de_manager *mgr = e->manager;

    if (e->slot < mgr->pause_index)
    {
        --mgr->pause_index;
        de_entity *other = mgr->items[mgr->pause_index];
        de_entity_swap(e, other);
    }
}

void de_entity_delete(de_entity *e)
{
    de_manager *mgr = e->manager;
    if (e->slot >= mgr->size)
        return;

    e->state = de_state_delete;
    if (e->destructor)
        e->state = e->destructor(e->data);

    if (de_state_is_deleted(e->state))
    {
        if (e->slot < mgr->pause_index)
        {
            --mgr->pause_index;
            de_entity *other = mgr->items[mgr->pause_index];
            de_entity_swap(e, other);
        }
        --mgr->size;
        if (e->slot != mgr->size)
        {
            de_entity *last = mgr->items[mgr->size];
            de_entity_swap(e, last);
        }
    }
}

void de_manager_pause(de_manager *mgr)
{
    mgr->pause_index = mgr->size;
}

void de_manager_resume(de_manager *mgr)
{
    mgr->pause_index = 0;
}

void de_manager_reset(de_manager *mgr)
{
    mgr->pause_index = 0;
    while (mgr->size)
    {
        de_entity *e = mgr->items[mgr->size - 1];
        de_entity_delete(e);
    }
}

void de_entity_update(de_entity *e)
{
    de_state s = e->state;
    if (de_state_is_active(s))
    {
        s = s(e->data);
        if (!de_state_is_loop(s))
            e->state = s;
    }
}

void de_manager_update(de_manager *mgr)
{
    uint16_t i = mgr->size;
    while (i-- > mgr->pause_index)
    {
        de_entity *e = mgr->items[i];
        // e->slot = i;
        de_state s = e->state;

        if (de_state_is_active(s))
            de_entity_update(e);
        else if (de_state_is_paused(s))
            de_entity_pause(e);
        else if (de_state_is_deleted(s))
            de_entity_delete(e);
    }
}

#define de_manager_iterate(M, CODE) \
    _de_manager_iterate(M, (M)->pause_index, CODE)

#define de_manager_iterateAll(M, CODE) \
    _de_manager_iterate(M, 0, CODE)

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
            (ACTION)(_targets[_count]);            \
    } while (0)

#define _DE_CONCAT(A, B) A##B
#define _DE_UNIQUE(A, B) _DE_CONCAT(A, B)

/* MGR must be a POINTER (&g_manager). */
#define de_manager_create(MGR, CAPACITY, PAYLOAD)                                                                                                                    \
    static de_entity *_DE_UNIQUE(_i_, __LINE__)[(CAPACITY)];                                                                                                         \
    static uint8_t _DE_UNIQUE(_s_, __LINE__)[(CAPACITY) * _DE_ENTITY_STRIDE(PAYLOAD)] __attribute__((aligned(2))); /* buffer must itself start at an even address */ \
    de_manager_init((MGR), _DE_UNIQUE(_i_, __LINE__), _DE_UNIQUE(_s_, __LINE__), (CAPACITY), (PAYLOAD))

#endif /* DARKEN_H */