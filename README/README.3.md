# Darken — DARKula ENgine Entity System

A fixed-size entity pool for C, built for game engines where `malloc` in the frame loop is a design mistake.

* Single header. 
* No dependencies beyond `<stdint.h>`.
* GNU C
* Targeting the Motorola 68000
* Equally valid on any platform where memory is a scarce resource.

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
```

---

## Philosophy in three lines

1. **Memory never moves.** Once an entity is created, it lives at the same address for its entire lifetime. You can hold a pointer to `entity->data` across a hundred frames, pause it, resume it, delete others around it — the address stays valid.
2. **Everything is a pointer swap.** Spawn, pause, resume, and destroy are `O(1)`. No compaction, no byte reordering, just two pointers swapped in the internal `pool[]` array.
3. **The framework does not touch your data.** It does not initialize `update`, `destroy`, `tag`, `usr`, or the payload. Spawning means acquiring a slot; configuring it is your job. This is not a bug, it is a contract.

---

## The contract (read it twice)

**Rule 1: You initialize.**
`DARKEN_SPAWN()` returns a raw `darken_entity`. Its fields `update`, `destroy`, `tag`, and `usr` contain whatever the previous occupant of that slot left behind. If you do not overwrite them immediately — especially `update` — the next call to `darken_update()` will interpret the residual value as a command.

**Rule 2: `update == NULL` means "destroy me."**
`DARKEN_DELETE` is `((void *)0)`. A freshly spawned entity has `update == 0` until you assign a callback. If you forget, the entity self-destructs in the same frame. Silently. Correctly.

---

## The three zones

The `pool` array is logically split into three contiguous regions:

```
[ active ][ free ][ paused ]
0         size    paused     capacity
```

- **Active** — receive `update()` every frame.
- **Free** — empty slots. `DARKEN_SPAWN()` consumes from here.
- **Paused** — out of the loop, but intact. Their data pointers never become invalid.

Moving an entity between zones is swapping two pointers in `pool[]`. The physical entity does not even notice.

---

## Prototype-less callbacks (the C trick)

`darken_state` is declared as `void *(*)()` — no argument list, K&R-style. This is *deliberately* non-portable ISO C, but it works on GCC/Clang and the 68K. The upside: you can assign a function taking any pointer type directly, without a cast:

```c
void *bullet_update(Bullet *b) {
    b->x += b->vx;
    return b->x > 640.0f ? DARKEN_DELETE : DARKEN_LOOP;
}

// No cast, no warning:
entity->update = bullet_update;
```

The framework always invokes it as `entity->update(entity->data)`. If you need to reach the `darken_entity` handle from inside the callback, use `void *data` and `DARKEN_DATA_GET_ENTITY`.

---

## State chaining

A callback is not limited to returning `DARKEN_LOOP` or `DARKEN_DELETE`. It can return a pointer to another function. That function becomes the entity's `update` on the next frame. State machines without tables, without enums, without `switch`:

```c
void *enemy_attack(Enemy *e);
void *enemy_recover(Enemy *e);
void *enemy_idle(Enemy *e);

void *enemy_attack(Enemy *e) {
    // ... attack logic ...
    return enemy_recover;  // next frame enters here
}
```

---

## Quick start

### Dynamic memory

```c
darken particles = DARKEN_POOL_ALLOC(malloc, 256, sizeof(Particle));
darken_init(&particles);

// ... loop ...
free(particles.pool);
free(particles.storage);
```

### Static memory (no malloc)

```c
DARKEN_POOL_DECLARE(bullet_storage, MAX_BULLETS, sizeof(Bullet));
darken bullets = DARKEN_POOL_BIND(bullet_storage);
darken_init(&bullets);
```

### Global scope (initialization)

```c
DARKEN_POOL_DECLARE(g_storage, 128, sizeof(Particle));
darken g_particles = DARKEN_POOL_INIT(g_storage);

int main(void) {
    darken_init(&g_particles);
    // ...
}
```

---

## Full example

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdlib.h>

typedef struct { int y, vy; } Particle;

void *particle_update(Particle *p) {
    p->y += p->vy;
    return p->y > 480 ? DARKEN_DELETE : DARKEN_LOOP;
}

int main(void) {
    darken ctx = DARKEN_POOL_ALLOC(malloc, 256, sizeof(Particle));
    darken_init(&ctx);

    darken_entity e = DARKEN_SPAWN(&ctx);
    if (e) {
        e->update  = particle_update;
        e->destroy = DARKEN_DELETE;
        e->tag     = 0;
        e->usr     = 0;

        DARKEN_DATA(Particle, p, e);
        p->y  = 0;
        p->vy = 2;
    }

    while (1)
        darken_update(&ctx);
    
}
```

---

## API

### Types

| Type            | What it is                                                                                        |
| --------------- | ------------------------------------------------------------------------------------------------- |
| `darken`        | Pool context. Read-only public fields: `pool`, `storage`, `capacity`, `size`, `paused`, `stride`. |
| `darken_entity` | Opaque pointer to an entity. Public fields: `update`, `destroy`, `tag`, `usr`, `data[]`.          |
| `darken_state`  | Callback type. Deliberately without argument prototype.                                           |

### Control values

Returned by `update`/`destroy` or assigned directly:

| Value                      | Effect                                           |
| -------------------------- | ------------------------------------------------ |
| `DARKEN_DELETE` (`0`)      | Destroy the entity.                              |
| `DARKEN_LOOP` (`1`)        | Keep calling the same `update` next frame.       |
| `DARKEN_PAUSE` (`2`)       | Move to the paused zone.                         |
| *any other `darken_state`* | That function pointer becomes the next `update`. |

**Note:** `DARKEN_PAUSE` and `DARKEN_DELETE` returned from `update()` are applied on the **following** `darken_update()` call, not the current one. For immediate effect, use `darken_entity_pause()` / `darken_entity_delete()`.

### Pool setup

| Macro                                     | Use                                                                    |
| ----------------------------------------- | ---------------------------------------------------------------------- |
| `DARKEN_POOL_ALLOC(alloc, cap, payload)`  | Dynamic memory. Free with `free(ctx.pool)` and `free(ctx.storage)`.    |
| `DARKEN_POOL_DECLARE(name, cap, payload)` | Declares embedded storage.                                      |
| `DARKEN_POOL_BIND(name)`                  | Initializer from a `DECLARE`. Valid in locals and runtime contexts.    |
| `DARKEN_POOL_INIT(name)`                  | Same as `BIND`, but computed via `sizeof`, valid at global/file scope. |

### Context lifecycle

```c
void darken_init(darken *ctx);   // Once, after building the pool.
void darken_update(darken *ctx); // One frame: runs all active updates.
void darken_reset(darken *ctx);  // Destroys all active entities and resets the pool.
```

### Entities

```c
darken_entity e = DARKEN_SPAWN(&ctx);   // NULL if the pool is full.
```

### Iteration

```c
DARKEN_FOREACH(&ctx, {
    DARKEN_DATA(Enemy, data, _entity);
    if (data->hp <= 0)
        darken_entity_delete(_entity);
});
```

It is safe to pause or delete entities inside the block. Iteration walks backward; every active entity is visited exactly once.

### Manual entity control

| Function                  | Effect                                                 | Returns                       |
| ------------------------- | ------------------------------------------------------ | ----------------------------- |
| `darken_entity_run(e)`    | Calls `update` **without** applying the transition.    | `1` if active, `0` otherwise. |
| `darken_entity_update(e)` | Calls `update` **applying** the transition.            | `1` / `0`                     |
| `darken_entity_pause(e)`  | Moves to paused zone, immediately.                     | `1` if was active.            |
| `darken_entity_resume(e)` | Moves to active zone, immediately.                     | `1` if was paused.            |
| `darken_entity_delete(e)` | Runs `destroy` if set and frees the slot, immediately. | `1` if was in use.            |

### Payload access

```c
DARKEN_DATA(Type, var, entity);   // Type *var = entity->data;
DARKEN_DATA_GET_ENTITY(data_ptr); // Recover the darken_entity from a payload pointer.
```

---

## What it does not do (and why)

- **It does not initialize anything.** Avoids the cost of `memset` on every spawn. In a 60 fps engine with hundreds of entities, that is gold.
- **It is not thread-safe.** Built for a single game loop thread.
- **It does not compact memory.** Addresses are stable; the trade-off is that iteration order is not creation order.
- **It is not pure ISO C.** Uses `__attribute__((aligned(4)))`, statement expressions in `DARKEN_SPAWN`, and prototype-less callbacks. Requires GCC or Clang.
- **It does not mix types in the same pool.** One `darken` manages one `stride`. Enemies and bullets? Two contexts.

---

## Final warnings

1. **Always** assign `update`, `destroy`, `tag`, and `usr` after `DARKEN_SPAWN()`. Otherwise you inherit garbage from the previous slot occupant.
2. **Always** check if `DARKEN_SPAWN()` returned `NULL`. The pool has a fixed size.
3. Do not confuse `darken_entity_run()` with `darken_entity_update()`. The former ignores the return value; the latter applies the state transition.
4. `DARKEN_FOREACH` does not iterate in creation order. Do not rely on it.
5. On 64-bit platforms, `DARKEN_DATA_GET_ENTITY` uses 32-bit arithmetic for the offset (`uint32_t`). This is correct for the intended target (68K), but be aware if your payloads exceed 32-bit offset limits.

---

## License

Public domain / Unlicense. Copy, modify, sell, burn. The code is yours.
