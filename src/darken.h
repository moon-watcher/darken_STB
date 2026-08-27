/**
 * darken.h — Darken (DARKula ENgine) 2.0 Object System
 *
 * darken-2.0.0-dev
 *
 * Full documentation: README.Darken.md
 *
 * GNU C note:
 * - This header uses GNU C __attribute__ extension.
 * - 4-byte align boundary because Darken targets GCC and the Motorola 68000.
 * - 16-bit members preference for optimal 68K performance.
 *
 *
 *
 * Object: Base object managed by the object ctx
 *
 * The object structure serves as a container for user data with lifecycle  management. The flexible array member
 * 'data[]' allows objects to have variable-sized payloads while maintaining contiguous memory layout.
 *
 * The stride between objects is pre-calculated during ctx initialization to enable O(1) access to any object
 * by index.
 *
 * An object's own memory address (this struct) never moves once allocated by darken_init(). What moves between
 * the ctx's zones is only the *pointer* to it inside darken.pool[]. This is what makes it safe to keep a
 * raw pointer into object->data even while the object gets paused/resumed/reordered.
 *
 *
 *
 * Ctx: Object container and lifecycle ctx. Maintains the pointer array in three logical zones:
 *
 * Array Layout:
 *    [ active objects ][   free slots    ][ paused objects ]
 *    0                 size               paused             capacity
 *
 * The object objects themselves live in the caller-provided storage block; * ctx->pool contains pointers to
 * those fixed addresses.
 *
 * - Active zone [0, size):
 *     Object pointers updated every frame by darken_update(). Iterable with DARKEN_FOREACH. Freely created
 *     (darken_spawn) and deleted.
 *
 * - Free zone [size, paused):
 *     Pointer slots not currently assigned to an object. This is where darken_spawn() takes its next object from,
 *     and where an active object's slot goes right after it's deleted.
 *
 * - Paused zone [paused, capacity):
 *     Object pointers parked out of the update loop. darken_update() never touches them and DARKEN_FOREACH
 *     never visits them. Crucially, darken_spawn() never hands out a slot from this zone, so a paused object's
 *     slot (and therefore its object->data pointer) stays valid and untouched until it's explicitly resumed or
 *     deleted. This is what lets keep safely pointing at a paused object's data.
 *
 *
 *
 * Object state/destroy callback type.
 *
 * Receives object->data and returns either another state callback or one of the Darken control values.
 */

#ifndef DARKEN_H
#define DARKEN_H

#include <stdint.h>

typedef void *(*darken_callback)();

typedef struct darken_object *darken_object;

typedef struct darken
{
    darken_object *pool;

    uint16_t capacity;
    uint16_t size;
    uint16_t paused;
} darken;

struct darken_object
{
    // Private
    uint16_t slot;
    darken *owner;

    // Lifecycle callbacks
    darken_callback update;
    darken_callback destroy;

    // User-defined fields
    uint32_t tag;
    uint16_t usr;

    // Payload
    uint8_t data[];
};

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

#define DARKEN_DATA(TYPE, VAR, OBJECT) TYPE *VAR = (TYPE *)(OBJECT)->data;
#define DARKEN_DATA_GET_OBJECT(DATA) ((darken_object)((uint8_t *)(DATA) - (uint32_t)&((darken_object)0)->data))

// Darken control values
#define DARKEN_DELETE ((void *)0)
#define DARKEN_LOOP ((void *)1)
#define DARKEN_PAUSE ((void *)2)

#define BARKEN_STATE_IS_DELETED(STATE) ((STATE) == (darken_callback)0)
#define DARKEN_STATE_IS_LOOP(STATE) ((STATE) == (darken_callback)1)
#define DARKEN_STATE_IS_PAUSED(STATE) ((STATE) == (darken_callback)2)
#define DARKEN_STATE_IS_ACTIVE(STATE) ((STATE) > (darken_callback)2)

#define DARKEN_OBJECT_IN_ACTIVE(OBJECT) ((OBJECT)->slot < (OBJECT)->owner->size)
#define DARKEN_OBJECT_IN_PAUSED(OBJECT) ((OBJECT)->slot >= (OBJECT)->owner->paused)
#define DARKEN_OBJECT_IN_FREE(OBJECT) (!(DARKEN_OBJECT_IN_ACTIVE(OBJECT) || DARKEN_OBJECT_IN_PAUSED(OBJECT)))

uint16_t darken_object_run(darken_object);
uint16_t darken_object_update(darken_object);
uint16_t darken_object_pause(darken_object);
uint16_t darken_object_resume(darken_object);
uint16_t darken_object_delete(darken_object);

/**
 * Align a byte count to a 4-byte boundary.
 *
 * The Motorola 68000 requires word alignment for word/long accesses.
 * Longword alignment keeps object strides predictable and efficient.
 */
#define DARKEN_ALIGN4(X) (((X) + 3U) & ~3U)

// Ensures proper alignment between consecutive objects
#define DARKEN_OBJECT_STRIDE(PAYLOAD) DARKEN_ALIGN4(sizeof(struct darken_object) + (PAYLOAD))

#define DARKEN_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)                                                 \
    struct                                                                                           \
    {                                                                                                \
        uint16_t capacity;                                                                           \
        uint16_t payload_size;                                                                       \
        darken_object pool[(CAPACITY)];                                                              \
        uint8_t data[(CAPACITY) * DARKEN_OBJECT_STRIDE((PAYLOAD_SIZE))] __attribute__((aligned(4))); \
    } NAME = {                                                                                       \
        .capacity = (CAPACITY),                                                                      \
        .payload_size = (PAYLOAD_SIZE),                                                              \
    }

#define DARKEN_ARGS(NAME) \
    (NAME).pool, (NAME).data, (NAME).capacity, (NAME).payload_size

#define DARKEN_FOREACH(CTX, CODE)                      \
    do                                                 \
    {                                                  \
        uint16_t _index = (CTX)->size;                 \
        if (_index)                                    \
        {                                              \
            darken_object *_pool = (CTX)->pool;        \
                                                       \
            while (_index--)                           \
            {                                          \
                darken_object _object = _pool[_index]; \
                (void)_object;                         \
                CODE;                                  \
            }                                          \
        }                                              \
    } while (0)

void darken_init(darken *, darken_object[], void *, uint16_t, uint16_t);
void darken_init_ex(darken *, darken_object[], void *, uint16_t, uint16_t);
darken_object darken_spawn(darken *);
void darken_update(darken *);
void darken_reset(darken *);

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef DARKEN_IMPLEMENTATION

#define _DARKEN_ASSERT(COND, CODE, RET) \
    if (!(COND))                        \
        return 0;                       \
                                        \
    CODE;                               \
                                        \
    return RET;

#define _DARKEN_BLOCK(CODE) \
    do                      \
    {                       \
        CODE                \
    } while (0)

#define _DARKEN_RUN(OBJECT) \
    OBJECT->update(OBJECT->data);

#define _DARKEN_UPDATE(OBJECT) _DARKEN_BLOCK(       \
    darken_callback callback = _DARKEN_RUN(OBJECT); \
                                                    \
    if (!DARKEN_STATE_IS_LOOP(callback))            \
        OBJECT->update = callback;)

#define _DARKEN_PAUSE(OBJECT) _DARKEN_BLOCK(                                \
    _darken_swap(OBJECT->owner->pool, OBJECT->slot, --OBJECT->owner->size); \
    _darken_swap(OBJECT->owner->pool, OBJECT->slot, --OBJECT->owner->paused);)

#define _DARKEN_RESUME(OBJECT) _DARKEN_BLOCK(           \
    darken *ctx = OBJECT->owner;                        \
                                                        \
    _darken_swap(ctx->pool, OBJECT->slot, ctx->paused); \
    _darken_swap(ctx->pool, OBJECT->slot, ctx->size);   \
                                                        \
    ++ctx->paused;                                      \
    ++ctx->size;)

#define _DARKEN_DELETE(OBJECT) _DARKEN_BLOCK(    \
    darken *ctx = OBJECT->owner;                 \
                                                 \
    if (DARKEN_STATE_IS_ACTIVE(OBJECT->destroy)) \
        OBJECT->destroy(OBJECT->data);           \
                                                 \
    _darken_swap(ctx->pool, OBJECT->slot, --ctx->size);)

static inline uint16_t _darken_swap(darken_object pool[], uint16_t i, uint16_t j)
{
    _DARKEN_ASSERT(i != j, {
            darken_object tmp = pool[i];
            pool[i] = pool[j];
            pool[j] = tmp;
            pool[i]->slot = i;
            pool[j]->slot = j; }, 1);
}

//

void darken_init(darken *$, darken_object pool[], void *param_storage, uint16_t capacity, uint16_t payload_size)
{
    darken_init_ex($, pool, param_storage, capacity, DARKEN_OBJECT_STRIDE(payload_size));
}

/*
// Uso de almacenamiento dinámico

#define MAX_ENTITIES 100
#define PAYLOAD_SIZE  sizeof(MyData)

darken enemies_manager;
darken_object *pool = malloc(MAX_ENTITIES * sizeof(darken_object));
uint16_t stride = DARKEN_OBJECT_STRIDE(PAYLOAD_SIZE);
uint8_t *storage = malloc(MAX_ENTITIES * stride);

// Inicialización con stride calculado
darken_init_ex(&enemies_manager, pool, storage, MAX_ENTITIES, stride);

// ...

free(pool);
free(storage);
*/
void darken_init_ex(darken *$, darken_object pool[], void *storage, uint16_t capacity, uint16_t stride)
{
    $->pool = pool;
    $->capacity = capacity;
    $->size = 0;
    $->paused = capacity;

    uint8_t *object_storage = (uint8_t *)storage;

    for (uint16_t i = 0; i < capacity; ++i)
    {
        darken_object object = (darken_object)object_storage;

        pool[i] = object;
        object->owner = $;
        object->slot = i;

        object_storage += stride;
    }
}

darken_object darken_spawn(darken *$)
{
    _DARKEN_ASSERT($->size < $->paused, , $->pool[$->size++];);
}

void darken_update(darken *$)
{
    DARKEN_FOREACH($, {
        if (DARKEN_STATE_IS_ACTIVE(_object->update))
            _DARKEN_UPDATE(_object);

        else if (DARKEN_STATE_IS_PAUSED(_object->update))
            _DARKEN_PAUSE(_object);

        else if (BARKEN_STATE_IS_DELETED(_object->update))
            _DARKEN_DELETE(_object);
    });
}

void darken_reset(darken *$)
{
    DARKEN_FOREACH($, _DARKEN_DELETE(_object));

    $->size = 0;
    $->paused = $->capacity;
}

uint16_t darken_object_run(darken_object $)
{
    _DARKEN_ASSERT(DARKEN_STATE_IS_ACTIVE($->update), _DARKEN_RUN($), 1);
}

uint16_t darken_object_update(darken_object $)
{
    _DARKEN_ASSERT(DARKEN_STATE_IS_ACTIVE($->update), _DARKEN_UPDATE($), 1);
}

uint16_t darken_object_pause(darken_object $)
{
    _DARKEN_ASSERT(DARKEN_OBJECT_IN_ACTIVE($), _DARKEN_PAUSE($), 1);
}

uint16_t darken_object_resume(darken_object $)
{
    _DARKEN_ASSERT(DARKEN_OBJECT_IN_PAUSED($), _DARKEN_RESUME($), 1);
}

uint16_t darken_object_delete(darken_object $)
{
    _DARKEN_ASSERT(!DARKEN_OBJECT_IN_FREE($), {
            if (DARKEN_OBJECT_IN_ACTIVE($))
                _DARKEN_DELETE($);
            else
                _darken_swap($->owner->pool, $->slot, $->owner->paused++); }, 1);
}

#endif // DARKEN_IMPLEMENTATION

#endif // DARKEN_H
