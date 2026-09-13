#pragma once

#include <stdint.h>


/*
 * ============================================================
 * dsmap2
 * ============================================================
 *
 * Variante experimental de dsmap:
 *
 *   - pool denso
 *   - lookup handle -> slot
 *   - handle almacenado dentro de cada elemento
 *   - NO existe handles[]
 *   - alloc O(1)
 *   - valid O(1)
 *   - data O(1)
 *   - remove O(1)
 *   - swap-remove O(1)
 *
 *
 * IMPORTANTE
 * ============================================================
 *
 * El TYPE usado por el usuario debe comenzar con:
 *
 *     uint16_t handle;
 *
 * Ejemplo:
 *
 *     typedef struct
 *     {
 *         uint16_t handle;
 *         uint16_t x;
 *         uint16_t y;
 *         uint16_t vx;
 *         uint16_t vy;
 *     } Entity;
 *
 *
 * El handle forma parte del objeto.
 *
 * Esto es lo que permite eliminar handles[] sin perder
 * la información necesaria para actualizar lookup[] cuando
 * hacemos swap-remove.
 */


/*
 * ============================================================
 * TYPES
 * ============================================================
 */

typedef uint16_t dsmap2_handle_t;


typedef struct
{
    char *pool;
    uint16_t *lookup;

    uint16_t capacity;
    uint16_t size;
    uint16_t count;
    uint16_t free_head;
} dsmap2_t;


#define DSMAP2_INVALID_HANDLE ((dsmap2_handle_t)0xFFFFu)


/*
 * El handle ocupa siempre los primeros 2 bytes.
 */
#define DSMAP2_HANDLE(DATA) \
    (*(uint16_t *)(DATA))


/*
 * ============================================================
 * STORAGE
 * ============================================================
 */

#define DSMAP2_ALLOC(ALLOC, CAPACITY, SIZE)                  \
    {                                                        \
        .pool = (char *)(ALLOC)((CAPACITY) * (SIZE)),        \
        .lookup = (uint16_t *)(ALLOC)(                      \
            (CAPACITY) * sizeof(uint16_t)),                  \
        .capacity = (CAPACITY),                              \
        .size = (SIZE),                                      \
    }


#define DSMAP2_DECLARE(NAME, CAPACITY, TYPE) \
    struct                                  \
    {                                       \
        uint16_t capacity;                  \
        TYPE pool[(CAPACITY)];              \
        uint16_t lookup[(CAPACITY)];        \
    } NAME = {                              \
        .capacity = (CAPACITY),             \
    }


#define DSMAP2_INIT(STORAGE, TYPE)                                      \
    {                                                                   \
        .pool = (char *)(STORAGE).pool,                                 \
        .lookup = (STORAGE).lookup,                                     \
        .capacity = sizeof((STORAGE).lookup) /                          \
                    sizeof((STORAGE).lookup[0]),                        \
        .size = sizeof(TYPE),                                           \
    }


/*
 * ============================================================
 * INTERNAL SWAP
 * ============================================================
 */

static inline void dsmap2_swap(
    char *a,
    char *b,
    uint16_t size)
{
    while (size--)
    {
        char temp = *a;

        *a++ = *b;
        *b++ = temp;
    }
}


/*
 * ============================================================
 * INIT
 * ============================================================
 */

static inline void dsmap2_init(dsmap2_t *map)
{
    uint16_t i;

    map->count = 0;
    map->free_head = DSMAP2_INVALID_HANDLE;

    if (!map->capacity)
        return;

    map->free_head = 0;

    for (i = 0; i < map->capacity; i++)
        map->lookup[i] = i + 1;

    map->lookup[map->capacity - 1] =
        DSMAP2_INVALID_HANDLE;
}


/*
 * ============================================================
 * VALID
 * ============================================================
 */

static inline uint16_t dsmap2_valid(
    dsmap2_t *map,
    dsmap2_handle_t handle)
{
    uint16_t slot;

    if (handle >= map->capacity)
        return 0;

    slot = map->lookup[handle];

    if (slot >= map->count)
        return 0;

    return DSMAP2_HANDLE(
        map->pool + ((uint32_t)slot * map->size)
    ) == handle;
}


/*
 * ============================================================
 * DATA
 * ============================================================
 */

static inline void *dsmap2_data(
    dsmap2_t *map,
    dsmap2_handle_t handle)
{
    uint16_t slot;

    if (handle >= map->capacity)
        return 0;

    slot = map->lookup[handle];

    if (slot >= map->count)
        return 0;

    if (DSMAP2_HANDLE(
            map->pool + ((uint32_t)slot * map->size)
        ) != handle)
        return 0;

    return map->pool +
           ((uint32_t)slot * map->size);
}


/*
 * ============================================================
 * ALLOC
 * ============================================================
 */

static inline dsmap2_handle_t dsmap2_alloc(
    dsmap2_t *map)
{
    dsmap2_handle_t handle;
    uint16_t slot;
    char *data;

    if (map->count >= map->capacity ||
        map->free_head == DSMAP2_INVALID_HANDLE)
        return DSMAP2_INVALID_HANDLE;

    handle = map->free_head;

    map->free_head = map->lookup[handle];

    slot = map->count++;

    map->lookup[handle] = slot;

    data = map->pool +
           ((uint32_t)slot * map->size);

    DSMAP2_HANDLE(data) = handle;

    return handle;
}


/*
 * ============================================================
 * REMOVE
 * ============================================================
 *
 * El elemento eliminado termina en pool[last].
 *
 * El elemento que estaba en last se mueve a slot y su handle
 * se obtiene directamente desde el objeto movido.
 */

static inline void *dsmap2_remove(
    dsmap2_t *map,
    dsmap2_handle_t handle)
{
    uint16_t slot;
    uint16_t last;

    char *data;
    char *last_data;

    if (!dsmap2_valid(map, handle))
        return 0;

    slot = map->lookup[handle];

    last = --map->count;

    data = map->pool +
           ((uint32_t)slot * map->size);

    last_data = map->pool +
                ((uint32_t)last * map->size);

    if (slot != last)
    {
        uint16_t moved_handle;

        /*
         * El último elemento contiene su propio handle.
         */
        moved_handle = DSMAP2_HANDLE(last_data);

        dsmap2_swap(
            data,
            last_data,
            map->size
        );

        map->lookup[moved_handle] = slot;
    }

    /*
     * El handle eliminado entra en la free-list.
     */
    map->lookup[handle] = map->free_head;
    map->free_head = handle;

    /*
     * El objeto eliminado está ahora en pool[last].
     */
    return last_data;
}