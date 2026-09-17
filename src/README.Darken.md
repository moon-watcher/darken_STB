# Darken — DARKula ENgine Entity System

`darken.h` is a generic, single-header entity/lifecycle manager for C — target-agnostic, with no dependency on any specific platform, engine, or toolchain. It operates purely over plain caller-provided storage and pointers, so it drops into any C project. No dynamic dispatch, no hidden allocations after setup, no handle tables — entities are plain pointers into a caller-provided storage block, kept dense by swap-and-pop.

Current version: **darken-1.1.0_dev**

- [Darken — DARKula ENgine Entity System](#darken--darkula-engine-entity-system)
  - [Requirements](#requirements)
  - [Core idea](#core-idea)
  - [The three zones](#the-three-zones)
  - [Two guarantees you can rely on](#two-guarantees-you-can-rely-on)
  - [Getting a `darken_t` — three ways](#getting-a-darken_t--three-ways)
  - [Spawning an entity](#spawning-an-entity)
  - [Update / lifecycle control — two modes](#update--lifecycle-control--two-modes)
    - [STATE-MACHINE mode (default)](#state-machine-mode-default)
    - [DIRECT mode (`#define DARKEN_DIRECT`)](#direct-mode-define-darken_direct)
  - [Pause, resume, delete](#pause-resume-delete)
  - [Iterating](#iterating)
  - [API reference](#api-reference)
  - [Complexity](#complexity)
  - [Limits](#limits)
  - [Gotchas](#gotchas)

## Requirements

Two things are required of whoever includes `darken.h`:

1. **Fixed-width integer types visible before including the header.** Darken deliberately does **not** `#include <stdint.h>` itself — the including project must provide them, whether via a plain `#include <stdint.h>` or whatever equivalent your target already defines them through. Even on bare-metal/freestanding targets without a full C library, GCC ships its own self-contained `<stdint.h>` — so a plain `#include <stdint.h>` typically works regardless of whether your platform's own headers define these types themselves.
2. **A GNU C compiler — GCC or Clang.** Darken relies on GNU C statement expressions (`DARKEN_SPAWN`, `DARKEN_FOREACH`) and the `__attribute__((aligned))` extension (`DARKEN_DECLARE`). It will not build under a strict ISO-C-only compiler.

Beyond that, Darken makes no assumptions about the target: pointer width, struct alignment, and endianness are all whatever the compiler says they are for the platform it's building for — entity storage alignment is computed with `__alignof__` rather than any hardcoded value.

## Core idea

A `darken_t` (the **ctx**) owns an array of pointers (`pool[]`) into a fixed block of entity storage you provide. Each entity is a `struct darken_entity_t` with a flexible array member `data[]` for your own payload — so one allocation holds the entity's bookkeeping (`slot`, `update`, `destroy`, `tag`, `usr`, `owner`) and your game-specific fields back to back.

The ctx keeps `pool[]` partitioned into three contiguous zones and moves entities between them purely by swapping pointers around — an entity's own address never changes, only its *position* in `pool[]` does.

## The three zones

```
[ active entities ][   free slots    ][ paused entities ]
0                 size               paused             capacity
```

| Zone   | Range                | Meaning                                                                                                                                                                           |
| ------ | -------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Active | `[0, size)`          | Updated every frame by `darken_update()`. Visited by `DARKEN_FOREACH` (reverse order).                                                                                            |
| Free   | `[size, paused)`     | Unused slots. `DARKEN_SPAWN()` always takes from here.                                                                                                                            |
| Paused | `[paused, capacity)` | Parked out of the update loop. `DARKEN_SPAWN()` never touches this zone, so a paused entity's slot — and its `data` pointer — stays put until you explicitly resume or delete it. |

## Two guarantees you can rely on

1. **An entity's address never moves** once `darken_init()` has run. Pausing, resuming, spawning, or deleting *other* entities only ever reorders `pool[]`; it never relocates the storage itself. That's what makes it safe to keep a raw pointer into `entity->data` across frames.

2. **The ctx's own address must be stable too**, for the same reason: `darken_init()` bakes the address you gave it into every entity's `->owner`. A helper that builds a `darken_t` on its own stack and returns it *by value* compiles fine and silently leaves every `->owner` dangling into a dead stack frame the moment the helper returns.

   ```c
   // WRONG — m's address dies with the function
   static darken_t make_pool(void) {
       darken_t m = DARKEN_ALLOC(malloc, 8, sizeof(Enemy));
       darken_init(&m);
       return m;
   }

   // RIGHT — build it in its final home
   static void make_pool(darken_t *m) {
       *m = DARKEN_ALLOC(malloc, 8, sizeof(Enemy));
       darken_init(m);
   }
   ```

Neither `darken_init()`, `DARKEN_SPAWN()`, nor deletion clears `update`/`destroy`/`tag`/`usr` — a freshly spawned entity (first use or recycled) carries whatever the previous occupant left behind. Set every field you care about on every spawn.

## Getting a `darken_t` — three ways

```c
// Dynamic allocation
darken_t m = DARKEN_ALLOC(malloc, 5, sizeof(struct MyComponent));
darken_init(&m);
...
DARKEN_FREE(free, &m);

// Static storage, bound at runtime (reassignable, e.g. "rebind this ctx to different storage later")
DARKEN_DECLARE(storage, 5, sizeof(struct MyComponent));
darken_t m = DARKEN_BIND(storage);
darken_init(&m);

// Static storage, bound as a compile-time constant (file-scope globals)
DARKEN_DECLARE(storage, 5, sizeof(struct MyComponent));
static darken_t m = DARKEN_INIT(storage);
```

All three (`DARKEN_ALLOC`, `DARKEN_BIND`, `DARKEN_INIT`) expand to a `(darken_t){...}` compound literal, so they work anywhere a `darken_t` expression is legal — a declaration initializer, a plain assignment to reassign an existing variable, a function argument, and so on.

`ALLOC` is any `void *(*)(size_t-like)` allocator — `malloc`.

## Spawning an entity

```c
darken_entity_t e = DARKEN_SPAWN(&m);

if (!e) return;

e->update = enemy_walk_state;
e->destroy = enemy_on_death;
e->tag = ENEMY;
e->usr = WHATEVER;

DARKEN_DATA(struct MyComponent, data, e);
data->hp = 10;
```

## Update / lifecycle control — two modes

**STATE-MACHINE mode is the default.** Define `DARKEN_DIRECT` before including this header to opt into direct mode instead. Pick one per translation unit — it's a compile-time switch on `darken_state_t` itself.

### STATE-MACHINE mode (default)

```c
darken_state_t player_walk(struct Player *p)
{
    p->x++;

    if (should_stop(p))
        return player_stop;   // installs a new callback for NEXT frame

    if (should_die(p))
        return DARKEN_DELETE; // destroy() runs (if set), then the entity dies

    return DARKEN_CONTINUE;   // stay active, same callback
}
```

`darken_update()` reads the return value and drives the lifecycle for you:

| Return            | Effect                                                       |
| ----------------- | ------------------------------------------------------------ |
| `DARKEN_CONTINUE` | Stay active, keep the same `update`                          |
| `DARKEN_DELETE`   | Call `destroy` (if set), then delete the entity              |
| `DARKEN_PAUSE`    | Move straight to the paused zone                             |
| anything else     | Treated as a new `update` callback, installed for next frame |

The callback only ever receives the payload, never the entity handle — recover it with `DARKEN_ENTITY(data)` if you need to touch `usr`/`tag`.

You don't have to wrap the payload pointer in `void *` and cast it yourself: `darken_state_t` is an old-style (K&R) unprototyped function pointer, so a callback declared to take your concrete payload type directly —

```c
darken_state_t player_walk(struct Player *p) { ... }
```

— assigns straight to `entity->update` with no cast. This is exactly what the header comment means by *"the callee just reads however many leading arguments it declares."* Only declare `void *data` for callbacks that genuinely ignore the payload.

### DIRECT mode (`#define DARKEN_DIRECT`)

```c
void player_walk(darken_entity_t entity, struct Player *p)
{
    p->x++;
    if (should_stop(p)) entity->update = player_stop;
    if (should_die(p))  darken_entity_delete(entity);
}
```

Both the handle and the payload are passed; `darken_update()` calls `entity->update(entity, entity->data)` and ignores any return value. The callback is in full control: change state by assigning `entity->update` directly, and pause/resume/delete itself by calling `darken_entity_pause()/resume()/delete()` with the handle it was already given — no `DARKEN_ENTITY` lookup needed. A callback that only needs the handle can drop the second parameter entirely: `void player_walk(darken_entity_t entity)`.

## Pause, resume, delete

```c
darken_entity_pause(e);  // -> paused zone; no-op if already paused/not active
darken_entity_resume(e); // -> active zone; no-op if already active/not paused
darken_entity_delete(e); // destroy() runs only if e was ACTIVE; paused deletes skip it
```

`darken_reset(&m)` calls `destroy()` on every active entity and drops the whole pool back to the free zone — paused entities are swept away the same way a paused `darken_entity_delete()` treats them: silently, `destroy()` never runs for them.

## Iterating

```c
DARKEN_FOREACH(&m, {
    DARKEN_DATA(struct MyComponent, data, _entity);
    data->hp -= 1;
});
```

Visits active entities in **reverse** slot order (`size-1` down to `0`), which is what makes deleting the entity you're currently visiting safe. One subtlety: if deleting entity A swaps a *different*, not-yet-visited entity B into A's old (already-passed) slot, B is skipped for *this* pass and picked up again next frame — not a bug, just a one-frame ordering quirk of swap-and-pop under reverse iteration.

## API reference

| Symbol                                         | What it does                                                            |
| ---------------------------------------------- | ----------------------------------------------------------------------- |
| `DARKEN_ALLOC(alloc, capacity, payload_size)`  | Heap-backed `darken_t`                                                  |
| `DARKEN_FREE(free, ctx)`                       | Frees what `DARKEN_ALLOC` allocated                                     |
| `DARKEN_DECLARE(name, capacity, payload_size)` | Declares static storage (no `darken_t` yet)                             |
| `DARKEN_BIND(name)`                            | `darken_t` view over `DARKEN_DECLARE`d storage, for runtime (re)binding |
| `DARKEN_INIT(name)`                            | Same, as a compile-time constant initializer                            |
| `darken_init(ctx)`                             | Lays entities out over the storage block; call once per (re)bind        |
| `DARKEN_SPAWN(ctx)`                            | Takes a slot from the free zone, or `NULL` if full                      |
| `DARKEN_DATA(type, var, entity)`               | Declares `type *var` pointing at `entity`'s payload                     |
| `DARKEN_ENTITY(data)`                          | Recovers the `darken_entity_t` from a payload pointer                   |
| `darken_entity_pause/resume/delete(entity)`    | Zone transitions; see above                                             |
| `darken_update(ctx)`                           | Runs one frame over the active zone                                     |
| `darken_reset(ctx)`                            | Destroys all active entities, empties the pool                          |
| `DARKEN_FOREACH(ctx, code)`                    | Manual iteration over the active zone                                   |
| `DARKEN_ENTITY_IN_ACTIVE/FREE/PAUSED(entity)`  | Zone membership tests                                                   |
| `DARKEN_COUNT_ACTIVE/FREE/PAUSED(ctx)`         | Zone sizes                                                              |

## Complexity

| Operation                        | Cost                     |
| -------------------------------- | ------------------------ |
| `DARKEN_SPAWN`                   | O(1)                     |
| `darken_entity_pause` / `resume` | O(1) — two pointer swaps |
| `darken_entity_delete`           | O(1) — one pointer swap  |
| `darken_update`                  | O(active entities)       |
| `darken_reset`                   | O(active entities)       |

## Limits

`capacity`/`size`/`paused`/`stride` are `uint16_t`, so a single `darken_t` tops out at 65535 entities. Unlike Darksys, Darken has no reserved sentinel value to work around — every value up to 65535 is a usable capacity.

## Gotchas

- **No staleness detection.** Darken hands you raw pointers, not generation-checked handles. If you keep a `darken_entity_t` around after deleting it, and a later spawn reuses that exact slot, your old pointer now silently refers to the *new* occupant — same address, different entity. If you need to detect this, roll your own generation counter in `tag` or `usr` and check it yourself; Darken (unlike Darksys' `darksys_valid()`) has no built-in way to ask "is this still the entity I think it is?".
- Fields aren't auto-initialized on spawn (see above) — an entity with a garbage `update` pointer will crash the moment `darken_update()` reaches it.
- Toolchain and integer-type constraints (GNU C, `<stdint.h>`) are covered in [Requirements](#requirements), not repeated here.