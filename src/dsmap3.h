/**
 * dsmap.h — stable handle -> inline data slot map (owns its storage)
 *
 * dsmap-1.0.0
 *
 * Written for Sega Genesis / Mega Drive + SGDK (m68k-elf-gcc). Every public function is
 * `static inline`, defined directly in this header — same reasoning as vsmap.h: on a 7.6MHz
 * 68000, avoiding a JSR/RTS for something this small is a real, measurable win, and the cost
 * (possible duplicate copies across translation units) is negligible for functions this size.
 *
 *
 *
 * What this is for
 * ==================================================
 *
 * Like vsmap.h, this hands out a small stable handle for something you'll look up repeatedly
 * and remove later. The difference is *what* it's a handle to: vsmap stores a pointer to an
 * object that lives somewhere else; dsmap stores the object's actual bytes, inline, inside a
 * contiguous pool it owns. Use dsmap when you want fixed-size records (a struct of stats, a
 * transform, a save-slot) packed together in one block of memory with no separate allocation
 * per element — closer to darken.h in spirit, except darken.h also gives you a per-frame
 * update() callback and pause/resume; dsmap is just the storage-and-handle part,for when you
 * don't need the rest.
 *
 *
 *
 * Design: dense free list + sparse index, no generation
 * ==================================================
 *
 * A handle is a plain uint16_t index into `lookup[]` — a stable slot  number in [0, capacity)
 * that never changes while that element is alive, and gets reused (via a free list) once it's
 * removed. Same "recycled index, no staleness protection" trade-off as vsmap.h, made for the
 * same reason: keep handles out of long-lived storage you don't fully control, and this is safe.
 *
 * Three parallel, capacity-sized arrays back this:
 *
 *     pool[capacity]     — the actual element bytes. Where exactly a given handle's bytes live
 *                          depends on the mode (see below).
 *     lookup[index]      — while `index` is ALIVE: which position in `handles[]` currently
 *                          tracks it.
 *                          while `index` is FREE: the next free index, so the free list is
 *                          threaded through this same array at zero extra memory cost.
 *     handles[0, count)  — the inverse of `lookup`: handles[slot] is which handle currently
 *                          occupies tracking position `slot`. Used to fix up `lookup[]` when
 *                          the tracking array gets compacted.
 *
 * `lookup`/`handles` together are the exact same dense-array/free-list machinery as vsmap.h,
 * just tracking *aliveness and packing order* rather than a pointer. What differs between the
 * two selectable modes is whether `pool[]` gets compacted the same way alongside them.
 *
 *
 *
 * Two modes: dense (default) vs DSMAP3_STABLE
 * ==================================================
 *
 * Define DSMAP3_STABLE before including this header to switch modes.
 *
 * 1) Dense mode — default
 * ---------------------------------------------------------
 *     An element's bytes live at `pool + lookup[handle] * size` — i.e. wherever its tracking
 *     slot currently is. Removing an element swaps the last live element's bytes into the
 *     vacated slot (same swap-compaction as vsmap.h, just moving whole records instead of a
 *     pointer), so `pool[0, count)` is always fully packed with no gaps. That makes
 *     `for (i = 0; i < map.count; i++)` over `pool` cheap and cache-friendly — good when you
 *     frequently iterate or update every live element.
 *
 *     The cost: an element's address can change out from under you the moment *any* other
 *     element is removed, because that removal might be the one that gets swap-compacted into
 *     your slot's old neighbour... no — more precisely: removing element X relocates whichever
 *     element previously happened to be *last* in the pack. Never hold a raw pointer from
 *     dsmap3_data() across a dsmap3_remove() call in this mode; re-fetch it from the handle
 *     instead.
 *
 * 2) DSMAP3_STABLE mode
 * ---------------------------------------------------------
 *     An element's bytes live at a fixed `pool + handle * size` for as long as that handle
 *     is alive — full stop, regardless of what else gets added or removed. This is the same
 *     address-never-moves guarantee darken.h makes for its entities, just handle-indexed
 *     instead of pointer-indexed.
 *
 *     `lookup[]`/`handles[]` still get compacted exactly as in dense mode — that bookkeeping
 *     is what keeps dsmap3_valid()/add() O(1) —but `pool[]` itself is left alone. The trade-off:
 *     `pool[]` is no longer densely packed (a removed handle leaves its old slot sitting there,
 *     simply not addressed by anyone until that exact handle is reused), so iterating "every
 *     live element in pool order" isn't a plain loop over `pool[0, count)` anymore.
 *     Use DSMAP3_FOREACH for that either way — it already does the right thing per mode.
 *
 * Pick dense mode by default; reach for DSMAP3_STABLE specifically when you need to keep a raw
 * pointer around across removals of *other* elements (e.g. something else holds
 * `Entity *e = dsmap3_data(...)` and keeps using `e` directly for a while).
 *
 *
 *
 * Alignment
 * ==================================================
 *
 * `size` should always be `sizeof(YourStruct)`, obtained via `sizeof` — never a hand-picked
 * byte count. `sizeof` on any type already accounts for whatever padding that type needs to
 * keep every element of an array of it correctly aligned; DSMAP3_DECLARE relies on exactly
 * that by declaring `pool` as a real `TYPE pool[CAPACITY]` array (letting the compiler place
 * and space every element correctly) rather than a raw byte buffer with a hand-rolled stride.
 * This matters on real hardware: the original 68000 in a Genesis raises a bus error on a
 * misaligned word/long access, so getting this wrong isn't just a performance nit — it can
 * crash.
 *
 * Capacity tops out at 0xFFFE: index 0xFFFF is reserved as the free-list terminator.
 */

#pragma once

#include <stdint.h>

typedef uint16_t dsmap3_handle_t;

#define DSMAP3_INVALID_HANDLE ((dsmap3_handle_t)0xFFFFu)

typedef struct
{
    char *pool;         // the element bytes -- see the big comment above for where, per mode
    uint16_t *lookup;   // index -> tracking slot (alive), or next free index (free)
    uint16_t *handles;  // tracking slot -> index; the inverse of `lookup`
    uint16_t capacity;  // must be <= 0xFFFE
    uint16_t size;      // bytes per element -- always sizeof(YourStruct)
    uint16_t free_head; // head of the free list, or DSMAP3_INVALID_HANDLE if empty
    uint16_t count;
} dsmap3_t;

/* ============================================================================
 * PUBLIC API — all static inline, always available, nothing to compile separately
 * ============================================================================ */

// Dynamic allocation: use with malloc/calloc or a custom allocator.
// Call dsmap3_init() once afterwards.
//
//     dsmap3_t map = DSMAP3_ALLOC(malloc, 100, sizeof(Entity));
//     dsmap3_init(&map);
//     ...
//     free(map.pool);
//     free(map.lookup);
//     free(map.handles);
#define DSMAP3_ALLOC(ALLOC, CAPACITY, SIZE)                             \
    {                                                                  \
        .pool = (char *)(ALLOC)((CAPACITY) * (SIZE)),                  \
        .lookup = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)),  \
        .handles = (uint16_t *)(ALLOC)((CAPACITY) * sizeof(uint16_t)), \
        .capacity = (CAPACITY),                                        \
        .size = (SIZE),                                                \
    }

// Static allocation with automatic storage duration (stack or global).
// TYPE decides `size` for you (see the Alignment note above) -- always a real type, never a
// raw byte count.
//
//     DSMAP3_DECLARE(storage, 100, Entity);
//     dsmap3_t map = DSMAP3_BIND(storage);
//     dsmap3_init(&map);
#define DSMAP3_DECLARE(NAME, CAPACITY, TYPE) \
    struct                                  \
    {                                       \
        uint16_t capacity;                  \
        TYPE pool[(CAPACITY)];              \
        uint16_t lookup[(CAPACITY)];        \
        uint16_t handles[(CAPACITY)];       \
    } NAME = {                              \
        .capacity = (CAPACITY),             \
    }

// Static/global initialization: compile-time constants.
#define DSMAP3_INIT(STORAGE)                                                 \
    {                                                                       \
        .pool = (char *)(STORAGE).pool,                                     \
        .lookup = (STORAGE).lookup,                                         \
        .handles = (STORAGE).handles,                                       \
        .capacity = sizeof((STORAGE).lookup) / sizeof((STORAGE).lookup[0]), \
        .size = sizeof((STORAGE).pool[0]),                                  \
    }

// Runtime binding: locals, reassignment, any context.
#define DSMAP3_BIND(NAME)                \
    {                                   \
        .pool = (char *)(NAME).pool,    \
        .lookup = (NAME).lookup,        \
        .handles = (NAME).handles,      \
        .capacity = (NAME).capacity,    \
        .size = sizeof((NAME).pool[0]), \
    }

// Visits every live element, from last to first (safe to dsmap3_remove() the current ITEM's
// handle from inside CODE -- same swap-with-last compaction trick as vsmap.h's
// VSMAP_FOREACH / darken.h's DARKEN_FOREACH).
// Bound to `_data` (a `void *` to that element's bytes -- cast it to your type) and `_handle`
// (its dsmap3_handle_t) inside CODE. Correct for either mode: in dense mode this walks `pool`
// directly; in DSMAP3_STABLE mode `pool` may have gaps, so this walks the dense `handles[]`
// tracking array instead and resolves each one's fixed address from its handle.
#ifdef DSMAP3_STABLE
#define DSMAP3_FOREACH(MAP, CODE)                                             \
    do                                                                       \
    {                                                                        \
        uint16_t _index = (MAP)->count;                                      \
        if (_index)                                                          \
        {                                                                    \
            while (_index--)                                                 \
            {                                                                \
                dsmap3_handle_t _handle = (MAP)->handles[_index];             \
                void *_data = (MAP)->pool + (uint32_t)_handle * (MAP)->size; \
                CODE;                                                        \
            }                                                                \
        }                                                                    \
    } while (0)
#else
#define DSMAP3_FOREACH(MAP, CODE)                                            \
    do                                                                      \
    {                                                                       \
        uint16_t _index = (MAP)->count;                                     \
        if (_index)                                                         \
        {                                                                   \
            while (_index--)                                                \
            {                                                               \
                dsmap3_handle_t _handle = (MAP)->handles[_index];            \
                void *_data = (MAP)->pool + (uint32_t)_index * (MAP)->size; \
                CODE;                                                       \
            }                                                               \
        }                                                                   \
    } while (0)
#endif

// Must be called once after ALLOC/BIND, before the first dsmap3_alloc().
// Also doubles as a full reset: call it again any time to drop every element and start over
// (every handle issued before that call is no longer valid -- same "everything gets freed"
// contract as darken_reset() / vsmap_init()).
static inline void dsmap3_init(dsmap3_t *map)
{
    map->count = 0;
    map->free_head = DSMAP3_INVALID_HANDLE;

    if (map->capacity)
    {
        map->free_head = 0;

        for (uint16_t i = 0; i < map->capacity; i++)
            map->lookup[i] = i + 1; // chain every slot into the free list...

        map->lookup[map->capacity - 1] = DSMAP3_INVALID_HANDLE; // ...terminated here
    }
}

// Reserves a slot and returns its handle, or DSMAP3_INVALID_HANDLE if the pool is full.
// The slot's bytes are NOT cleared -- use dsmap3_data() to get a writable pointer and fill
// in whatever fields you need. Called `_alloc` rather than `_add` (unlike vsmap_add())
// as a reminder that there's no value parameter: you get an empty slot, not a copy of
// something you hand in.
static inline dsmap3_handle_t dsmap3_alloc(dsmap3_t *map)
{
    if (map->count >= map->capacity || map->free_head == DSMAP3_INVALID_HANDLE)
        return DSMAP3_INVALID_HANDLE;

    dsmap3_handle_t handle = map->free_head;
    map->free_head = map->lookup[handle]; // pop the free list

    uint16_t slot = map->count++;
    map->lookup[handle] = slot;
    map->handles[slot] = handle;

    return handle;
}

// True if `handle` currently refers to a live element.
static inline uint16_t dsmap3_valid(dsmap3_t *map, dsmap3_handle_t handle)
{
    if (handle >= map->capacity)
        return 0;

    uint16_t slot = map->lookup[handle];
    if (slot >= map->count)
        return 0;

    return map->handles[slot] == handle;
}

// Returns a pointer to `handle`'s bytes, or NULL if it's not currently valid. In dense
// mode, don't hold onto this across a dsmap3_remove() of a *different* handle -- see the
// mode comparison above.
static inline void *dsmap3_data(dsmap3_t *map, dsmap3_handle_t handle)
{
    if (!dsmap3_valid(map, handle))
        return 0;

#ifdef DSMAP3_STABLE
    return map->pool + (uint32_t)handle * map->size;
#else
    return map->pool + (uint32_t)map->lookup[handle] * map->size;
#endif
}

// Removes `handle` if valid and returns a pointer to its last-known bytes, or NULL
// (and does nothing) if the handle was already invalid.
//
// That returned pointer is only good until the next dsmap3_alloc() call: unlike
// vsmap_remove() (which hands back an external pointer nothing here controls the lifetime
// of), the bytes it points at live inside this pool's own free space and WILL get
// overwritten the moment that slot is reused. Read whatever you need from it immediately
// -- logging it, releasing a resource it references, copying it out -- before doing
// anything else with this map.
//
// In dense mode this is *why* removal swaps the two records instead of just copying the
// survivor over the vacated slot: a plain one-way copy would leave the removed element's
// old bytes sitting wherever the survivor used to be, not at the address this function
// is about to return. Swapping puts the removed element's bytes at that address on purpose.
static inline void *dsmap3_remove(dsmap3_t *map, dsmap3_handle_t handle)
{
    if (!dsmap3_valid(map, handle))
        return 0;

    uint16_t slot = map->lookup[handle];
    uint16_t last = --map->count;

    if (slot != last)
    {
        dsmap3_handle_t moved_handle = map->handles[last];

#ifndef DSMAP3_STABLE
        char *data = map->pool + (uint32_t)slot * map->size;
        char *last_data = map->pool + (uint32_t)last * map->size;
        uint16_t size = map->size;

        while (size--)
        {
            char temp = *data;
            *data++ = *last_data;
            *last_data++ = temp;
        }
#endif

        map->handles[slot] = moved_handle;
        map->lookup[moved_handle] = slot;
    }

    map->lookup[handle] = map->free_head; // push back onto the free list
    map->free_head = handle;

#ifdef DSMAP3_STABLE
    return map->pool + (uint32_t)handle * map->size;
#else
    return map->pool + (uint32_t)last * map->size;
#endif
}
