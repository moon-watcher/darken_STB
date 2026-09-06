# Darken

**Darken** (part of the DARKula ENgine) is a small, single-header C entity system: a fixed-capacity pool that owns your game objects' memory, updates them every frame, and lets you pause, resume or delete them in O(1) — without ever moving an object's own memory address.

It's written in GNU C and aimed at resource-constrained / retro targets — GCC + Motorola 68000 (e.g. Sega Genesis / Mega Drive homebrew via SGDK) — but there's nothing 68k-specific about the design; it works anywhere a GNU-C-compatible compiler is available.

## What it's for

Darken is the layer under your game's actors: enemies, projectiles, particles, pickups — anything with a per-instance payload, a per-frame update, and a lifetime that starts and ends at runtime. It gives you:

- A pool with a **fixed maximum entity count**, decided once, with no hidden allocation during gameplay.
- **O(1) spawn, delete, pause and resume**, implemented as index swaps inside a single pointer array.
- A **stable memory address** for every live entity's payload, even while the pool reorders itself around it — so you can hold on to a raw pointer into an entity's data across frames, pauses and resumes.

## Features

- **Single header, header-only.** Drop `darken.h` into your project; define `DARKEN_IMPLEMENTATION` in exactly one translation unit.
- **Three-zone pool layout** — active / free / paused — so paused entities are completely invisible to the update loop and to `DARKEN_FOREACH`, without being deleted or losing their storage slot.
- **Variable-sized payload per pool**, via a flexible array member; the per-entity stride is computed and 4-byte-aligned for you.
- **Two selectable callback styles** (see below): a return-value-driven **state machine** (the default) and a **direct** mode where the callback gets the entity handle and manages its own lifecycle.
- **No forced entity struct.** Your payload is a plain `struct` of your choosing; Darken only wraps it with a small fixed header.
- Built on GNU C statement expressions and unprototyped function pointers, so `DARKEN_SPAWN` is an expression and callbacks can declare only the arguments they actually use.

## Requirements

- A compiler with GNU C extensions (statement expressions, `__attribute__`). GCC works; this includes cross-compilers such as `m68k-elf-gcc`.
- C99 or later (uses `<stdint.h>`, flexible array members, designated initializers).

## Getting started

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
```

Do this in exactly one `.c` file — like any STB-style single-header library, every other file that needs the API just does a plain
`#include "darken.h"` without defining `DARKEN_IMPLEMENTATION`.

### Minimal example (default mode: state machine)

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"

struct bullet { int16_t x, y, vy; };

void *bullet_update(struct bullet *data)
{
    data->y += data->vy;
    if (data->y > 240)
        return DARKEN_DELETE;      // destroy (if set) + free the slot

    return DARKEN_CONTINUE;        // stay active, same callback next frame
}

// Static pool: capacity 64, payload = sizeof(struct bullet)
DARKEN_POOL_DECLARE(bullet_storage, 64, sizeof(struct bullet));
darken bullets = DARKEN_POOL_INIT(bullet_storage);

void spawn_bullet(int16_t x, int16_t y)
{
    darken_entity e = DARKEN_SPAWN(&bullets);
    if (!e) return;                // pool full

    DARKEN_DATA(struct bullet, data, e);
    data->x = x;
    data->y = y;
    data->vy = -4;
    e->update = bullet_update;
    e->destroy = 0;
}

int main(void)
{
    darken_init(&bullets);         // call this once, after binding, before first use
    // ... spawn_bullet() / game_loop_frame() from here on

    while (1)
      darken_update(&bullets);     // runs bullet_update() on every active bullet
}
```

`DARKEN_POOL_INIT` only builds the initializer for `bullets`; it doesn't set up the pool's internal zones for you — call `darken_init()` once, after binding and before the first `DARKEN_SPAWN`/`darken_update`.

## The two modes

Pick one by defining (or not defining) `DARKEN_DIRECT` **before** including `darken.h`. The choice affects only the signature of your `update`/`destroy` callbacks and what `darken_update()` does with them — everything else in the API is identical either way.

### State-machine mode (default)

```c
void *callback(void *data);
```

Only the entity's payload is passed — never the handle. `darken_update()` reads the return value and drives the lifecycle for you:

| Return value                   | Effect                                                        |
| ------------------------------ | ------------------------------------------------------------- |
| `DARKEN_CONTINUE`              | stay active, keep the same `update` callback                  |
| `DARKEN_DELETE`                | call `destroy` (if set), then delete the entity               |
| `DARKEN_PAUSE`                 | move the entity straight to the paused zone                   |
| any other `darken_state` value | treated as a new `update` callback — installed for next frame |

```c
void *enemy_idle();    // forward decl for the transition below

void *enemy_chase(struct enemy *data)
{
    move_towards_player(data);

    if (lost_player(data))
        return enemy_idle; // switch state

    return DARKEN_CONTINUE;
}
```

`destroy` has the same `(data)`-only signature as `update`, but its return value is always ignored — it's called purely for its side effects by `darken_reset()` and `darken_entity_delete()`.

If a callback needs the entity handle anyway (say, to read/write `usr` or `tag`), recover it with `DARKEN_ENTITY(data)`.

### Direct mode (`DARKEN_DIRECT`)

```c
void callback(darken_entity entity, void *data);   // or just: void callback(darken_entity entity);
```

Both the handle and the payload are passed, entity first. `darken_update()` just calls `entity->update(entity, entity->data)` every frame and ignores any return value — the callback is fully responsible for its own lifecycle: change state by assigning `entity->update` (and/or `entity->destroy`) directly, and pause/resume/delete by calling `darken_entity_pause()` / `darken_entity_resume()` / `darken_entity_delete()` yourself.

```c
#define DARKEN_DIRECT
#define DARKEN_IMPLEMENTATION
#include "darken.h"

void enemy_chase(darken_entity entity, struct enemy *data)
{
    move_towards_player(data);

    if (lost_player(data))
        entity->update = enemy_idle;

    if (data->hp <= 0)
        darken_entity_delete(entity);
}
```

A callback can declare fewer parameters than the engine actually passes (e.g. just `darken_entity entity`, reaching for the payload yourself with `DARKEN_DATA`) — `darken_state` is deliberately declared without a prototype, so the callee only reads however many leading arguments it needs; the rest are pushed and simply ignored.

## Public API

### Types

| Type            | Meaning                                                                                                                                                                                                           |
| --------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `darken`        | A pool / context: the pointer array, the backing storage, and the three zone boundaries (`size`, `paused`, `capacity`).                                                                                           |
| `darken_entity` | Handle to a live entity (`struct darken_entity *`). Stable for as long as the entity exists.                                                                                                                      |
| `darken_state`  | The `update`/`destroy` callback's function-pointer type. Declared without a prototype on purpose (see above). Its concrete meaning (`void`- vs `void *`-returning) depends on whether `DARKEN_DIRECT` is defined. |

`struct darken_entity` fields you're meant to touch directly:

| Field     | Meaning                                                   |
| --------- | --------------------------------------------------------- |
| `update`  | Your per-frame callback.                                  |
| `destroy` | Your cleanup callback, called on delete/reset.            |
| `usr`     | Free-form `uint16_t` for your own use.                    |
| `tag`     | Free-form `uint32_t` for your own use (e.g. entity type). |
| `data[]`  | The payload — cast it to your struct type.                |

`slot` and `owner` are private (used internally to track the entity's position and pool); don't write to them.

### Lifecycle functions

| Function                                     | Description                                                                                                                      |
| -------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| `void darken_init(darken *ctx)`              | One-time setup: links every slot in `ctx->pool` to its storage, and resets the zones (everything starts in the free zone).       |
| `void darken_update(darken *ctx)`            | Runs `update` on every **active** entity, once each, in reverse order. Drives the state-machine protocol in that mode.           |
| `void darken_reset(darken *ctx)`             | Calls `destroy` (if set) on every active entity, then empties the pool — everything becomes free again, active and paused alike. |
| `void darken_entity_pause(darken_entity e)`  | Moves an active entity to the paused zone. No-op if it isn't currently active.                                                   |
| `void darken_entity_resume(darken_entity e)` | Moves a paused entity back to active. No-op if it isn't currently paused.                                                        |
| `void darken_entity_delete(darken_entity e)` | If active: calls `destroy` (if set), then frees the slot. If paused: frees the slot **without** calling `destroy`.               |

### Setting up a pool

| Macro                                                 | Use case                                                                                                                                                   |
| ----------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DARKEN_POOL_ALLOC(alloc_fn, capacity, payload_size)` | Designated-initializer expression that allocates `.pool`/`.storage` via your own allocator (e.g. `malloc`-shaped). You own the two `free()` calls.         |
| `DARKEN_POOL_DECLARE(name, capacity, payload_size)`   | Declares a `name` variable (an anonymous struct) holding the pool array and the byte storage with automatic or static storage duration — no heap involved. |
| `DARKEN_POOL_INIT(storage)`                           | Designated-initializer binding a `darken` to a `DARKEN_POOL_DECLARE`d `storage`, for compile-time / global initialization.                                 |
| `DARKEN_POOL_BIND(storage)`                           | Same idea, for runtime (re)binding — locals, reassignment, any context.                                                                                    |

Whichever you use, call `darken_init(&ctx)` once afterwards.

### Working with entities

| Macro                            | Description                                                                                                                                                                                                                                                                     |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DARKEN_SPAWN(ctx)`              | Takes the next entity from the free zone and returns its handle, or `0` if the pool is full. Does **not** clear any fields — `update`, `destroy`, `usr`, `tag` and the payload may still hold whatever the slot's previous occupant left behind; set everything you care about. |
| `DARKEN_FOREACH(ctx, code)`      | Runs `code` once per active entity (bound to `_entity`), iterating **in reverse** (from the last active index down to 0) — this is what makes it safe to delete the current entity from inside the loop.                                                                        |
| `DARKEN_DATA(type, var, entity)` | Declares `type *var`, pointing at `entity`'s payload.                                                                                                                                                                                                                           |
| `DARKEN_ENTITY(data)`            | The inverse: recovers the owning `darken_entity` handle from a raw payload pointer. Mostly useful in state-machine mode, where callbacks only receive `data`.                                                                                                                   |

### Zone queries

| Macro                             | Description                                    |
| --------------------------------- | ---------------------------------------------- |
| `DARKEN_ENTITY_IN_ACTIVE(entity)` | True if the entity is in the active zone.      |
| `DARKEN_ENTITY_IN_PAUSED(entity)` | True if the entity is in the paused zone.      |
| `DARKEN_ENTITY_IN_FREE(entity)`   | True if the entity's slot is currently unused. |
| `DARKEN_COUNT_ACTIVE(ctx)`        | Number of active entities.                     |
| `DARKEN_COUNT_FREE(ctx)`          | Number of free slots.                          |
| `DARKEN_COUNT_PAUSED(ctx)`        | Number of paused entities.                     |

### State-machine sentinels (only defined when `DARKEN_DIRECT` is **not**)

| Value             | Meaning when returned from `update`   |
| ----------------- | ------------------------------------- |
| `DARKEN_CONTINUE` | Stay active, no change.               |
| `DARKEN_DELETE`   | Call `destroy` (if set), then delete. |
| `DARKEN_PAUSE`    | Move to the paused zone.              |

Any other value is treated as a new `update` callback pointer.

## Design notes & gotchas

- **Nothing is cleared for you.** `DARKEN_SPAWN` recycles a slot as-is; always set `update`, `destroy`, `usr`, `tag` and the payload explicitly after spawning.
- **`destroy` is skipped for paused entities.** `darken_entity_delete()` only calls `destroy` if the entity was active at the time of the call.
- **An entity's payload address never moves.** Only its *slot index* changes as the pool reorders itself; a raw pointer you took from `entity->data` stays valid across pause/resume, as long as the entity itself isn't deleted.
- **`DARKEN_FOREACH` / `darken_update()` never see paused entities** — they simply don't exist as far as the update loop is concerned, without being destroyed.
- Mode is a compile-time choice per translation unit (via `DARKEN_DIRECT`); mixing modes across files that share the same pool isn't supported.
