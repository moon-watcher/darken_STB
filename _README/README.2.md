# Darken — DARKula ENgine Entity System

A minimal entity pool for C, built for game engines with fixed memory and a tight cycle budget — GCC + Motorola 68000 is the stated target, but it's equally usable on any platform where you want to avoid per-entity `malloc`/`free`.

No dependencies beyond `<stdint.h>` and `<stddef.h>`. One header. No hidden allocations. No magic.

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
```

Define `DARKEN_IMPLEMENTATION` in exactly **one** `.c` file before including the header — the usual single-header-library pattern. Every other file just includes it plainly.

---

## ⚠️ Two things that will bite you if you skip them

**1. Nothing is initialized for you.** Neither `darken_init()`, nor `DARKEN_SPAWN()`, nor deleting an entity touches `update`, `destroy`, `tag`, or `usr`. A brand-new entity (or one recycled after a previous occupant of that physical slot was deleted) can arrive with **whatever** its previous owner left behind. Set all four fields yourself, every time, right after every `DARKEN_SPAWN()`. This is a deliberate design choice (avoiding the cost of clearing memory on every spawn/delete), not an oversight — but forget it and you won't get a compile error, you'll get an entity with a garbage function pointer the first time you update it.

**2. `update == DARKEN_DELETE` means "delete me."** It's the same value (`0`) for both "freshly spawned, not configured yet" and "this entity should go away." Spawn an entity and don't assign `update` before the next `darken_update()` call, and the library will destroy it automatically that same frame, silently. This is correct, intended behavior — it just surprises people the first time.

---

## Core concept

An **entity** (`darken_entity`) is a fixed-size struct (callbacks + metadata) followed by a variable-size payload that you define, via a [flexible array member](https://en.wikipedia.org/wiki/Flexible_array_member):

```c
struct darken_entity {
    uint16_t slot;         // private
    darken *owner;         // private

    darken_state update;   // what to do this frame
    darken_state destroy;  // what to do on death (optional)

    uint32_t tag;           // yours, free to use
    uint16_t usr;            // yours, free to use

    uint8_t data[];         // your payload (Enemy, Bullet, Particle...)
};
```

A **context** (`darken`) manages a pool of entities *of one payload type/size*. Internally it keeps an array of pointers (`pool[]`) split into three contiguous zones: 

```
[ active ][ free ][ paused ]
0         size    paused     capacity
```

- **Active** `[0, size)` — get `update()` every frame. This is where your gameplay lives.
- **Free** `[size, paused)` — slots with no entity assigned. This is where `DARKEN_SPAWN` pulls from.
- **Paused** `[paused, capacity)` — out of the update loop, but intact: `data[]` stays valid and its address never changes.

Moving an entity between zones is **swapping two pointers inside `pool[]`** (`O(1)`) — the entity itself never moves in memory. This is what makes it safe to hold on to a `darken_entity` or a pointer into `entity->data` across several frames, even while the entity gets paused, resumed, or shuffled around internally.

---

## Quick start

### Dynamic memory

```c
typedef struct { float x, y, vy; } Particle;

darken particles = DARKEN_POOL_ALLOC(malloc, 256, sizeof(Particle));
darken_init(&particles);

// ...
free(particles.pool);
free(particles.storage);
```

`ALLOC` accepts any function with `malloc`'s signature
(`void *(*)(size_t)`) — your own arena allocator, pool allocator,
whatever you have.

### Static memory (no `malloc` at all, ideal for 68K)

```c
DARKEN_POOL_DECLARE(bullet_storage, MAX_BULLETS, sizeof(Bullet));
darken bullets = DARKEN_POOL_BIND(bullet_storage);
darken_init(&bullets);
```

### Static memory, at **global** scope (compile-time constant)

At file scope, C requires initializers to be constant expressions — reading another variable's field (`bullet_storage.capacity`) isn't one, so here you use `DARKEN_POOL_INIT` instead of `DARKEN_POOL_BIND` (it derives everything via `sizeof`, which *is* a constant expression):

```c
DARKEN_POOL_DECLARE(g_bullet_storage, MAX_BULLETS, sizeof(Bullet));
darken g_bullets = DARKEN_POOL_INIT(g_bullet_storage);

int main(void) {
    darken_init(&g_bullets);
    // ...
}
```

---

## Full example

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdlib.h>

typedef struct { float y, vy; } Particle;

// Callbacks can take the payload's real type directly — see "Callback
// signatures" below for why this works without a cast.
void *particle_update(Particle *p) {
    p->y += p->vy;

    return p->y > 480.0f ? DARKEN_DELETE : DARKEN_LOOP;
}

int main(void) {
    darken particles = DARKEN_POOL_ALLOC(malloc, 256, sizeof(Particle));
    darken_init(&particles);

    darken_entity e = DARKEN_SPAWN(&particles);
    if (e) {
        // ALWAYS set these 4 fields after a spawn. See the warning above.
        e->update  = particle_update;
        e->destroy = DARKEN_DELETE;
        e->tag     = 0;
        e->usr     = 0;

        DARKEN_DATA(Particle, p, e);
        p->y  = 0.0f;
        p->vy = 2.5f;
    }

    for (;;) {
        darken_update(&particles);
        // ... render, wait for next frame, etc.
    }
}
```

---

## Public API reference

### Types

| Type            | What it is                                                                                                                                                                          |
| --------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `darken`        | The context/manager. Readable public fields: `pool`, `storage`, `capacity`, `size`, `paused`, `stride`. Only write them through the setup macros and `darken_init()` — not by hand. |
| `darken_entity` | Opaque pointer to an entity. Public fields: `update`, `destroy`, `tag`, `usr`, `data[]`. `slot` and `owner` are for internal use.                                                   |
| `darken_state`  | Your state/lifecycle callback type. Declared with unspecified arguments on purpose — see "Callback signatures" below.                                                               |

### Control values

What an `update`/`destroy` callback can return (or be directly assigned):

| Value                          | Effect                                                                                                      |
| ------------------------------ | ----------------------------------------------------------------------------------------------------------- |
| `DARKEN_DELETE`                | Deletes the entity (runs `destroy` if set, frees the slot).                                                 |
| `DARKEN_LOOP`                  | Keeps calling the *same* `update` next frame.                                                               |
| `DARKEN_PAUSE`                 | Moves the entity to the paused zone; stops receiving `update`.                                              |
| *(any other function pointer)* | Treated as the *next* callback to use — this is how you chain phases/states without a separate state table. |

`DARKEN_PAUSE` and `DARKEN_DELETE` returned from `update()` take effect on the **following** `darken_update()` call, not the one that returned them — see "Warnings" below. Calling `darken_entity_pause()`/`darken_entity_delete()` directly, by contrast, takes effect immediately.

### Callback signatures

`darken_state` is deliberately typed as `void *(*)()` — unspecified arguments, K&R-style — instead of `void *(*)(void *)`. The library always calls it as `update(entity->data)`, but because the type is unprototyped, C lets you assign a function taking **any pointer type** to `update`/ `destroy` without a cast or a warning:

```c
static void *particle_update(Particle *p) { ... }   // works directly
```

This relies on all object pointers sharing the same representation (true on every real target, including 68K), and on `darken_state` staying unprototyped — it's not strictly portable ISO C, but it's the intended, idiomatic way to write callbacks in this library, since it avoids a `void *` cast in every single one.

Use a plain `void *data` parameter plus `DARKEN_DATA`/`DARKEN_DATA_GET_ENTITY` only when you specifically need to reach the `darken_entity` handle itself from inside the callback (e.g. to read `tag`/`usr`, or to pause/delete the entity from within its own update) — see "Payload access" below.

### Pool setup

| Macro                                          | Use                                                                                                                |
| ---------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| `DARKEN_POOL_ALLOC(ALLOC, CAPACITY, PAYLOAD)`  | `darken` initializer backed by memory from `ALLOC`. `free(x.pool)` and `free(x.storage)` when done.                |
| `DARKEN_POOL_DECLARE(NAME, CAPACITY, PAYLOAD)` | Declares `NAME` as fixed-size backing storage (embedded arrays, no `malloc`).                                      |
| `DARKEN_POOL_BIND(NAME)`                       | `darken` initializer from a `NAME` declared with `DARKEN_POOL_DECLARE`. Use in **locals or any runtime context**.  |
| `DARKEN_POOL_INIT(STORAGE)`                    | Same as `DARKEN_POOL_BIND`, but computed via `sizeof` so it's valid as a **global/file-scope static initializer**. |

### Context lifecycle

| Function                          | What it does                                                                                             |
| --------------------------------- | -------------------------------------------------------------------------------------------------------- |
| `void darken_init(darken *ctx)`   | Prepares `ctx` for use. Call once, right after building the initializer, before spawning anything.       |
| `void darken_update(darken *ctx)` | One frame: runs `update` on every active entity and applies loop/pause/delete transitions automatically. |
| `void darken_reset(darken *ctx)`  | Deletes (via `destroy`) every active entity and returns the pool to its empty starting state.            |

### Creating entities

```c
darken_entity e = DARKEN_SPAWN(&ctx);
if (!e) { /* pool full */ }
```

`DARKEN_SPAWN(CTX)` evaluates `CTX` exactly once (even if it's an expression with side effects) and returns `NULL` when there's no room left. **It initializes nothing** — see the warning at the top of this document.

### Iterating active entities

```c
DARKEN_FOREACH(&ctx, {
    DARKEN_DATA(Enemy, e, _entity);
    if (e->hp <= 0)
        darken_entity_delete(_entity);
});
```

Inside the block, `_entity` is the current `darken_entity`. **It's safe to pause or delete entities from inside `DARKEN_FOREACH` itself** — verified with tests: every active entity is visited exactly once, no skips, no duplicates, because iteration runs backward through the array and swaps always happen against the boundary of the zone that's currently shrinking. (Implementation detail: if the internal iteration order ever changes, re-verify this guarantee.)

### Manual entity control

For acting on one specific entity outside the normal `darken_update()` loop:

| Function                  | Effect                                                                                                                               | Returns                                                    |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------- |
| `darken_entity_run(e)`    | Calls `e->update(e->data)` once, **without** applying the resulting state transition. For "running it again" without changing phase. | `1` if it was active, `0` otherwise.                       |
| `darken_entity_update(e)` | Same call, but **does** apply the transition (equivalent to what `darken_update()` does for this one entity).                        | `1` / `0`                                                  |
| `darken_entity_pause(e)`  | Moves `e` to the paused zone, immediately.                                                                                           | `1` if it was active, `0` otherwise (e.g. already paused). |
| `darken_entity_resume(e)` | Returns `e` to the active zone, immediately.                                                                                         | `1` if it was paused, `0` otherwise.                       |
| `darken_entity_delete(e)` | Runs `e->destroy` if set and frees the slot, immediately.                                                                            | `1` if it was in use (active or paused), `0` otherwise.    |

All of these return `0` instead of raising any error when the entity
wasn't in the expected state for that operation — **check the return
value** if you care whether anything actually happened.

### Payload access

```c
#define DARKEN_DATA(TYPE, VAR, ENTITY)
#define DARKEN_DATA_GET_ENTITY(DATA)
```

`DARKEN_DATA(Enemy, e, entity)` declares `Enemy *e` pointing at `entity`'s payload — handy right after `DARKEN_SPAWN()`, when you have the `darken_entity` but not yet a typed pointer. `DARKEN_DATA_GET_ENTITY(data)` does the reverse: given a payload pointer, get back the `darken_entity` that owns it. You need this whenever a callback has to reach entity-level data it wasn't handed directly:

```c
static void *enemy_update(Enemy *e)
{
    darken_entity self = DARKEN_DATA_GET_ENTITY(e);

    if (self->tag == TAG_FROZEN)
        return DARKEN_PAUSE;

    e->hp -= 1;
    return e->hp <= 0 ? DARKEN_DELETE : DARKEN_LOOP;
}
```

---

## Strengths

- **Stable addresses.** An entity never moves in memory after `darken_init()`. You can hold on to a `darken_entity` or a pointer into `entity->data` across several frames, even through pauses and resumes — only pointers inside `pool[]` get reordered, never the data itself.
- **Everything is `O(1)`.** Spawn, pause, resume, and delete are a couple of pointer swaps, not a scan or a compaction pass.
- **No forced allocation.** With `DARKEN_POOL_DECLARE`, there's no `malloc` anywhere — the whole pool lives in static/global memory, sized exactly as requested at compile time. With `DARKEN_POOL_ALLOC`, you can use *any* allocator with `malloc`'s signature, including your own.
- **Mutation-safe iteration.** Pausing or deleting entities from inside `DARKEN_FOREACH` doesn't skip or double-visit anything (verified).
- **State chaining without a separate table.** Returning a different function pointer from `update` defines the next state directly — handy for simple per-entity state machines (attack → recover → idle, say) without an enum or a switch statement.
- **Cast-free typed callbacks.** Thanks to `darken_state` staying unprototyped, you write `static void *foo(Particle *p)` directly instead of casting a `void *` in every single callback.
- **One header, no dependencies beyond the standard library.**

## Warnings

- **Nothing initializes automatically** (see the warning at the top). Set `update`, `destroy`, `tag`, and `usr` on every `DARKEN_SPAWN()`.
- **`DARKEN_PAUSE`/`DARKEN_DELETE` returned from `update()` take one extra frame to apply.** The zone change happens on the *following* `darken_update()` call, not the one where the callback returned that value — confirmed by testing. If you need the change to happen immediately, call `darken_entity_pause()`/`darken_entity_delete()` directly instead of returning a control value.
- **One `darken` = one payload type.** The `stride` (distance between entities) is fixed once, from the `PAYLOAD` you gave when setting up the pool. Don't mix entity types in the same `darken` — use one context per type (one for enemies, one for bullets, etc.).
- **GCC only.** The header uses GNU C extensions (`__attribute__` for alignment, a statement expression in `DARKEN_SPAWN`). It's not portable standard C; don't expect it to compile as-is on MSVC or other non-GCC/Clang compilers.
- **Not thread-safe.** Built for a single-threaded game loop; there's no internal synchronization of any kind.
- **`DARKEN_SPAWN` can return `NULL`** when the pool is full. Always check before touching the returned entity.
- **`darken_entity_run` and `darken_entity_update` are not interchangeable.** The former ignores the callback's return value (no state change); the latter applies it. Using the wrong one can leave an entity "stuck" in a state you thought you'd already changed.
- **`DARKEN_FOREACH`'s iteration order is not creation order.** It walks the internal array back to front, and that order can get reshuffled by every pause/resume/delete. Don't rely on "first entity created is the first one visited."