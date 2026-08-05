/*
 * darken.h — single-header entity/manager library (STB-style)
 *
 * USAGE
 *   #include "darken.h" in every file.
 *   All functions are static inline; no separate implementation.
 *
 *   Declare a manager with its own storage:
 *       DE_MANAGER_DECLARE(name, capacity, payload);
 *   Then inside a function, initialise it:
 *       DE_MANAGER_INIT(name);
 *
 *   Or manually:
 *       de_manager mgr;
 *       de_entity *ptrs[CAPACITY];
 *       uint8_t storage[CAPACITY * (sizeof(de_entity) + sizeof(Payload))];
 *       de_manager_def mem = { .items = ptrs, .storage = storage,
 *                                 .capacity = CAPACITY, .bytes = sizeof(Payload) };
 *       de_manager_init(&mgr, &mem, "mgr");
 *
 * LICENSE
 *   Public domain.
 */

#ifndef DARKEN_H
#define DARKEN_H

// #include <stdint.h>
// #include <stddef.h>
#include <genesis.h>

/* ============================================================
 * STATE
 * ============================================================ */

typedef void *(*de_state)(void *);

#define de_state_delete ((de_state)0)
#define de_state_loop   ((de_state)1)
#define de_state_pause  ((de_state)2)

#define de_state_is_deleted(S) ((S) == de_state_delete)
#define de_state_is_loop(S)    ((S) == de_state_loop  )
#define de_state_is_paused(S)  ((S) == de_state_pause )
#define de_state_is_active(S)  ((S) >  de_state_pause )

/* ============================================================
 * ENTITY
 * ============================================================ */

typedef struct de_manager de_manager;

typedef struct de_entity
{
    de_state state;
    de_state destructor;
    de_manager *manager;
    uint16_t slot;  /* position in manager->items[] */
    uint16_t tag;   /* user‑defined identifier */
    uint8_t data[]; /* flexible payload */
} de_entity;

/* ============================================================
 * MANAGER
 * ============================================================ */

typedef struct de_manager
{
    de_entity **items;    /* array of pointers (size = capacity) */
    uint16_t size;        /* number of live entities */
    uint16_t capacity;    /* max entities */
    uint16_t pause_index; /* split: paused < pause_index <= active */
} de_manager;

/* Memory bundle passed to de_manager_init */
typedef struct de_manager_def
{
    de_entity **items; /* pointer array, size = capacity */
    void *storage;     /* raw memory block for all entities */
    uint16_t capacity; /* max number of entities */
    uint16_t bytes;    /* size of each entity's payload (data[]) */
} de_manager_def;

/* ---- Initialisation ---- */

static inline void de_manager_init(de_manager *mgr, const de_manager_def *mem)
{
    mgr->items = mem->items;
    mgr->capacity = mem->capacity;
    mgr->size = 0;
    mgr->pause_index = 0;

    size_t ent_size = sizeof(de_entity) + mem->bytes;
    for (uint16_t i = 0; i < mem->capacity; ++i)
    {
        mem->items[i] = (de_entity *)((uint8_t *)mem->storage + i * ent_size);
    }
}

/* ---- Entity creation ---- */

static inline de_entity *de_manager_new(de_manager *mgr)
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

/* ---- Internal swap helper ---- */

static inline void de_entity_swap(de_entity *a, de_entity *b)
{
    de_manager *mgr = a->manager;
    uint16_t i = a->slot;
    uint16_t j = b->slot;

    mgr->items[i] = b;
    b->slot = i;
    mgr->items[j] = a;
    a->slot = j;
}

/* ---- Pause / Resume / Delete ---- */

static inline void de_entity_pause(de_entity *e)
{
    de_manager *mgr = e->manager;
    if (e->slot >= mgr->pause_index && e->slot < mgr->size)
    {
        de_entity *other = mgr->items[mgr->pause_index];
        de_entity_swap(e, other);
        ++mgr->pause_index;
    }
}

static inline void de_entity_resume(de_entity *e)
{
    de_manager *mgr = e->manager;
    if (e->slot < mgr->pause_index)
    {
        --mgr->pause_index;
        de_entity *other = mgr->items[mgr->pause_index];
        de_entity_swap(e, other);
    }
}

static inline void de_entity_delete(de_entity *e)
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

/* ---- Manager bulk operations ---- */

static inline void de_manager_pause(de_manager *mgr)
{
    mgr->pause_index = mgr->size;
}

static inline void de_manager_resume(de_manager *mgr)
{
    mgr->pause_index = 0;
}

static inline void de_manager_reset(de_manager *mgr)
{
    mgr->pause_index = 0;
    while (mgr->size)
    {
        de_entity *e = mgr->items[mgr->size - 1];
        de_entity_delete(e);
    }
}

/* ---- Update ---- */

static inline void de_entity_update(de_entity *e)
{
    de_state s = e->state;
    if (de_state_is_active(s))
    {
        s = s(e->data);
        if (!de_state_is_loop(s))
            e->state = s;
    }
}

static inline void de_manager_update(de_manager *mgr)
{
    uint16_t i = mgr->size;
    while (i-- > mgr->pause_index)
    {
        de_entity *e = mgr->items[i];
        e->slot = i;
        de_state s = e->state;

        if (de_state_is_active(s))
            de_entity_update(e);
        else if (de_state_is_paused(s))
            de_entity_pause(e);
        else if (de_state_is_deleted(s))
            de_entity_delete(e);
    }
}

/* ============================================================
 * ITERATION MACROS
 * ============================================================ */

#define de_manager_iterate(M, CODE) \
    _de_manager_iterate(M, (M)->pause_index, CODE)

#define de_manager_iterateAll(M, CODE) \
    _de_manager_iterate(M, 0, CODE)

#define _de_manager_iterate(M, LIMIT, CODE)          \
    do                                               \
    {                                                \
        uint16_t INDEX = (M)->size;                  \
        if (INDEX > (LIMIT))                         \
        {                                            \
            de_entity **ENTITIES = (M)->items;       \
            while (INDEX-- > (LIMIT))                \
            {                                        \
                de_entity *ENTITY = ENTITIES[INDEX]; \
                CODE;                                \
            }                                        \
        }                                            \
    } while (0)

/* ============================================================
 * APPLY MACROS
 * ============================================================ */

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

/* ============================================================
 * CONVENIENCE MACROS
 * ============================================================ */

/*
 * Declare a manager variable with its own pointer array and storage.
 * Use inside a function (or globally, but then you must call DE_MANAGER_INIT
 * before using it).
 */
#define DE_MANAGER_STORAGE(CAPACITY, PAYLOAD)                   \
    {                                                           \
        de_entity *ptrs[CAPACITY];                              \
        uint8_t data[CAPACITY * (sizeof(de_entity) + PAYLOAD)]; \
    }

/*
 * Build a de_manager_def from a variable declared with DE_MANAGER_DECLARE.
 * This is a compound literal (C99), so it can be used as an expression.
 */
#define DE_MANAGER_DEFINITION(NAME)                                                                          \
    &(de_manager_def){                                                                                       \
        .items = (NAME).ptrs,                                                                                \
        .storage = (NAME).data,                                                                              \
        .capacity = sizeof((NAME).ptrs) / sizeof((NAME).ptrs[0]),                                            \
        .bytes = (sizeof((NAME).data) / (sizeof((NAME).ptrs) / sizeof((NAME).ptrs[0]))) - sizeof(de_entity), \
    }

#endif /* DARKEN_H */
