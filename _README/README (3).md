# Darken (DARKula ENgine) — Entity System
A single-header, <!-- GNU-C,--> pool-based entity/lifecycle manager built for tight memory budgets<!-- and the Motorola 68000-->. 

`darken.h` is not a full game engine and not a full ECS — it is a small, extremely deliberate piece of infrastructure: a fixed-capacity pool of fixed-address "entities," each driven by a **think-function state machine**, managed through a **three-zone array** that gives O(1) spawn, delete, pause and resume with zero heap churn after startup.

This document explains how it works, what it's good at, where it bites, and walks through building a small shoot-'em-up (shmup) with it, one system at a time. Every code sample in this README was compiled and run against the real `darken.h` (GCC 13, `-std=gnu11 -Wall -Wextra -Wpedantic`) while writing this document, and the more surprising behavioral claims (deferred deletion, destroy-on-delete-while-paused, nested-iteration shadowing, cross-entity deletion during iteration) were confirmed with small instrumented test programs, not just read off the macro expansions.

## Table of Contents
- [Darken (DARKula ENgine) — Entity System](#darken-darkula-engine--entity-system)
  - [Table of Contents](#table-of-contents)
  - [What Darken Is (and Isn't)](#what-darken-is-and-isnt)
  - [Mental Model: One Array, Three Zones](#mental-model-one-array-three-zones)
  - [The Entity: Fixed Address, Moving Pointer](#the-entity-fixed-address-moving-pointer)
  - [The Think-Function State Machine](#the-think-function-state-machine)
  - [API Reference](#api-reference)
  - [Strengths](#strengths)
  - [Weaknesses and Limitations](#weaknesses-and-limitations)
  - [Pitfalls and Gotchas](#pitfalls-and-gotchas)
  - [Best-Practices Checklist](#best-practices-checklist)
  - [Building a Shmup, Step by Step](#building-a-shmup-step-by-step)
    - [1. Choosing a pool strategy](#1-choosing-a-pool-strategy)
    - [2. The player](#2-the-player)
    - [3. Player bullets](#3-player-bullets)
    - [4. Enemies and multi-state patterns](#4-enemies-and-multi-state-patterns)
    - [5. Enemy bullets](#5-enemy-bullets)
    - [6. Collisions across pools](#6-collisions-across-pools)
    - [7. Explosions via the destroy callback](#7-explosions-via-the-destroy-callback)
    - [8. A boss fight: phases, pause and resume](#8-a-boss-fight-phases-pause-and-resume)
    - [9. The frame loop, assembled](#9-the-frame-loop-assembled)

## What Darken Is (and Isn't)

Darken manages **collections of homogeneous game objects** ("entities") that each carry:

- a small, fixed-layout **header** (owner, slot, two callbacks, a `tag`, a `usr` field), and
- a **user-defined payload** of any size, decided once per pool at setup time.

It gives you four O(1) operations — spawn, delete, pause, resume — plus a safe iteration macro, and it does all of this **without ever calling `malloc` after initialization**. That last point is the whole reason it exists: the header targets GCC cross-compiling to 68000 (Amiga, Sega Genesis/Mega Drive, Atari ST-class hardware), where per-frame heap allocation is either unavailable, unpredictable, or simply too slow.

Darken deliberately does **not** do:

- rendering, input, audio, or asset loading,
- frame timing / delta-time,
- collision detection (though its `tag` field and `DARKEN_FOREACH` make it easy to build your own),
- multiple component types or cross-component queries (it's one pool of one payload type at a time — you get "ECS-adjacent," not an ECS),
- serialization/save-states,
- any kind of parent/child or scene-graph relationship (though, as shown below, the "entity addresses never move" guarantee makes it easy to build your own raw-pointer relationships — carefully).

In a shmup you'll typically run **several Darken pools side by side**: one for the player, one for player bullets, one for enemies, one for enemy bullets, one for particles, one for the boss. That multi-pool pattern is the idiomatic way to use Darken, and it's what this README builds.

## Mental Model: One Array, Three Zones

Each `darken` context owns an array of pointers, `pool[]`, of length `capacity`. At all times the array is partitioned into three contiguous zones:

```
[ active entities ][   free slots    ][ paused entities ]
0                  size               paused             capacity
```

- **Active** — updated every frame by `darken_update()`, visited by `DARKEN_FOREACH`. `DARKEN_SPAWN()` hands out slots from here.
- **Free** — unused pointer slots. This is where `DARKEN_SPAWN()` takes its next slot from, and where a deleted active entity's slot lands.
- **Paused** — parked outside the update loop. `darken_update()` and `DARKEN_FOREACH` never touch it, and `DARKEN_SPAWN()` never hands out slots from it. This is what makes pausing safe: a paused entity's slot (and its `data` pointer) is guaranteed untouched until you explicitly resume or delete it.

Every operation (spawn, delete, pause, resume) is implemented as one or two pointer **swaps** across a zone boundary, followed by moving that boundary. No block ever moves; only the handful of pointers at a boundary do. That's where the O(1) guarantee comes from.

## The Entity: Fixed Address, Moving Pointer

```c
struct darken_entity
{
    // Private
    uint16_t slot;
    darken *owner;

    // Lifecycle callbacks
    darken_state update;
    darken_state destroy;

    // User-defined fields
    uint32_t tag;
    uint16_t usr;

    // Payload
    uint8_t data[];
};
```

The struct itself lives at a **fixed address inside the caller-provided storage block** (`ctx->storage`), computed once in `darken_init()` and never touched again. What moves between zones is only the *pointer to it* inside `ctx->pool[]`.

This is a genuinely nice guarantee: **you can hold a raw `darken_entity` pointer, or a raw pointer into its `->data`, across many frames**, even while other entities are spawned, paused, resumed, or deleted around it — *as long as the entity you're pointing at is not itself deleted* (more on that caveat in [Pitfalls](#pitfalls-and-gotchas)). In a shmup this is exactly what you want for things like a homing missile holding a direct pointer to its target, or a turret sub-entity holding a pointer back to its parent ship's data, without needing an indirection table or generational handles.

One more thing worth knowing: `darken_init()` walks the pool assigning `pool[capacity-1] = storage`, `pool[capacity-2] = storage + stride`, and so on — slot number and memory address end up in **reverse** order at start-up. It's harmless (nothing depends on it), but if you ever inspect the pool in a debugger and expected slot 0 to be at the base address, it isn't.

## The Think-Function State Machine

Every entity's `update` (and, more narrowly, `destroy`) field is a `darken_state`:

```c
typedef void *(*darken_state)();
```

This is the classic "think function returns the next think function" pattern (the same shape as Doom/idTech's `think_t`, or a hand-rolled coroutine): your update function receives `entity->data` and returns one of:

| Return value                             | Effect                                           |
| ---------------------------------------- | ------------------------------------------------ |
| a pointer to another compatible function | transition to that state next tick               |
| `DARKEN_LOOP` (`1`)                      | keep running the *current* state again next tick |
| `DARKEN_PAUSE` (`2`)                     | move this entity to the paused zone              |
| `DARKEN_DELETE` (`0`, `NULL`)            | destroy this entity                              |

Darken tells these apart with a pointer-relational trick:
`_DARKEN_STATE_IS_ACTIVE(STATE)` is defined as `(STATE) > (darken_state)2`. On every real machine, actual function addresses are always well above `2`, so this works — but it is, strictly, comparing pointers that don't point into the same array, which ISO C does not define. (Compiling with `-Wpedantic` flags this exact line: *"ISO C forbids ordered comparisons of pointers to functions."* It's a deliberate, common retro/embedded trick, not an oversight — just not portable in the standard's eyes.)

You can also **assign `entity->update` directly**, from outside the entity's own callback, to request a pause or delete without waiting for its state function to run on its own:

```c
enemy_entity->update = DARKEN_PAUSE; // takes effect on the *next* darken_update() call
```

See [Pitfalls](#pitfalls-and-gotchas) for why "the next call" matters more
than it looks.

---

## API Reference

**Types**

| Symbol          | Meaning                                                                              |
| --------------- | ------------------------------------------------------------------------------------ |
| `darken`        | A pool/lifecycle context: `pool`, `storage`, `capacity`, `size`, `paused`, `stride`. |
| `darken_entity` | *A pointer type* (`struct darken_entity *`) — not the struct itself.                 |
| `darken_state`  | `void *(*)()` — an unprototyped think-function pointer.                              |

**Control values** (compared against `darken_state` fields/returns)

| Value           | Numeric    | Meaning                                        |
| --------------- | ---------- | ---------------------------------------------- |
| `DARKEN_DELETE` | `(void*)0` | Destroy the entity                             |
| `DARKEN_LOOP`   | `(void*)1` | Keep the current state, run it again next tick |
| `DARKEN_PAUSE`  | `(void*)2` | Move the entity to the paused zone             |

**Setup macros**

| Macro                                               | Use                                                                                                           |
| --------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| `DARKEN_POOL_ALLOC(alloc, capacity, payload_size)`  | Heap-backed pool; local declaration or runtime compound-literal assignment only                               |
| `DARKEN_POOL_DECLARE(name, capacity, payload_size)` | Declares a static/local struct holding `pool[]` + `data[]` inline, no heap                                    |
| `DARKEN_POOL_INIT(storage)`                         | Builds a `darken` from a `DARKEN_POOL_DECLARE`d struct; constant expression, safe as a **global** initializer |
| `DARKEN_POOL_BIND(storage)`                         | Same idea, but reads runtime field values; **local/runtime use only**                                         |

**Per-frame / per-pool functions**

| Function                          | Effect                                                                                                  |
| --------------------------------- | ------------------------------------------------------------------------------------------------------- |
| `void darken_init(darken *ctx)`   | Wires `pool[]` to `storage`, resets zones (`size=0`, `paused=capacity`)                                 |
| `void darken_update(darken *ctx)` | Runs every active entity's think function once; applies deferred pause/delete requests from *last* call |
| `void darken_reset(darken *ctx)`  | Destroys every active entity (calling `destroy`) and empties the pool back to all-free                  |

**Per-entity functions** (all guarded: no-op / return `0` if the entity isn't in the required zone)

| Function                  | Effect                                                                                                                                      |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `darken_entity_run(e)`    | Calls the current `update` callback once; **discards** any transition it returns                                                            |
| `darken_entity_update(e)` | Calls the current `update` callback once and **applies** the transition, exactly like a single manual `darken_update()` tick for one entity |
| `darken_entity_pause(e)`  | Immediately moves an **active** entity to the paused zone                                                                                   |
| `darken_entity_resume(e)` | Immediately moves a **paused** entity back to active                                                                                        |
| `darken_entity_delete(e)` | Immediately deletes an active-or-paused entity (see [Pitfalls](#pitfalls-and-gotchas) re: the destroy callback on paused entities)          |

**Macros for entity data**

| Macro                            | Effect                                                                                                                                               |
| -------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DARKEN_DATA(TYPE, VAR, ENTITY)` | `TYPE *VAR = (TYPE*)(ENTITY)->data;`                                                                                                                 |
| `DARKEN_DATA_GET_ENTITY(DATA)`   | Reconstructs the owning `darken_entity` from a `data` pointer (a "container_of," predating `offsetof`)                                               |
| `DARKEN_FOREACH(CTX, CODE)`      | Iterates the active zone from `size-1` down to `0`, binding `_entity` each iteration; safe to `darken_entity_delete`/`pause` the *current* `_entity` |
| `DARKEN_SPAWN(CTX)`              | Claims a free slot and grows the active zone; returns `0` if none are free                                                                           |

---

## Strengths

- **Deterministic, allocation-free steady state.** After setup, spawn / delete / pause / resume are all O(1) pointer swaps — no `malloc`, no fragmentation, no GC pauses. Exactly right for 68k-class hardware and for any game that wants predictable frame times.
- **Stable entity addresses.** An entity's struct address never changes for its entire lifetime, so raw pointers into `->data` remain valid across spawns/pauses/resumes of *other* entities — genuinely useful for missiles-that-track-a-target or parent/child references without an indirection layer.
- **The think-function pattern is compact and cheap.** One function pointer per entity encodes an entire per-entity state machine; transitioning states is just "return a different pointer." This is a proven pattern from classic engines (Doom-era "thinkers") and reads naturally for boss patterns and attack phases.
- **Safe, break/continue-friendly iteration.** `DARKEN_FOREACH` walks backwards specifically so the current entity can delete or pause itself mid-pass without disturbing not-yet-visited slots, and because it expands to a real `while` loop, ordinary `break`/`continue` work exactly as expected inside it.
- **Pausing is a first-class, cheap operation**, not "set an `active=false` flag and remember to check it everywhere." A paused entity is fully invisible to `darken_update()` and `DARKEN_FOREACH` while its data sits untouched, ready to resume.
- **Three deployment shapes for one API.** Dynamic (`DARKEN_POOL_ALLOC`), static/global with zero heap (`DARKEN_POOL_DECLARE` + `DARKEN_POOL_INIT`), or static/local for runtime rebinding (`DARKEN_POOL_DECLARE` + `DARKEN_POOL_BIND`) — the same entity API works unchanged across all three.
- **Tiny overhead per entity.** The header is `slot`(2) + `owner`(4) + `update`(4) + `destroy`(4) + `tag`(4) + `usr`(2) = 20 bytes on a 32-bit target, aligned to 4 — real headroom left for payload on constrained RAM.
- **`tag`/`usr` give you free, payload-independent metadata** for broad-phase filtering (e.g. "is this a `TAG_ENEMY_BULLET`?") without touching `data`.
- **Header-only, dependency-free** beyond `<stdint.h>`, with the implementation gated behind `DARKEN_IMPLEMENTATION` in the classic single-header-library style — easy to vendor into any build.

## Weaknesses and Limitations

- **Requires GNU C.** `DARKEN_SPAWN` uses a statement expression (`({ ... })`), and `DARKEN_POOL_DECLARE` uses `__attribute__((aligned(4)))`. This will not compile under strict ISO C or MSVC without GNU-dialect support — fully intentional given the stated GCC/68k target, but a real constraint if you ever want to build the same code with a different toolchain.
- **Fixed capacity, no growth.** A pool's size is fixed at `darken_init()` time; `DARKEN_SPAWN` silently returns `0` when full. There's no queuing, no backpressure, no automatic resize — capacity planning is entirely on you, and undersizing a bullet pool is one of the most common ways a shmup built on Darken will visibly misbehave (dropped shots) or crash (unchecked `NULL`).
- **`uint16_t` slot indices** cap any single pool at 65535 entities — not a practical limit for this genre, but worth knowing.
- **Not an ECS.** One pool = one payload type, one pair of callbacks. There is no cross-pool component query; if you want "all entities with both a Position and a Velocity component," you're building that yourself, most likely by just giving every relevant payload struct those fields directly.
- **No thread-safety whatsoever.** No atomics, no locks. Single-threaded use only (again, entirely appropriate for the target hardware, but worth stating for anyone tempted to update pools from multiple threads).
- **No serialization / save-state support.** Reasonable for a lean header, but if you need replays or save states you'll be writing that layer yourself, entity struct by entity struct.
- **No logging, assertions, or error reporting anywhere.** Misuse fails silently (a guarded public function like `darken_entity_pause()` just returns `0` and does nothing if the entity isn't in the expected zone). Good for not crashing on misuse; bad for noticing you *have* a bug.
- **No allocation-failure handling.** `DARKEN_POOL_ALLOC` doesn't check whether your `alloc` function returned `NULL`; a failed allocation silently produces a `darken` whose `pool`/`storage` are `NULL`, which will crash inside `darken_init()`.
- **`darken_state` is an unprototyped function pointer type** (`void *(*)()`), a K&R-era declaration that disables argument-type checking. Every assignment needs a cast, and a mismatched real signature is undefined behavior the compiler will not catch for you.
- **The "is this a real function pointer" test is technically undefined behavior.** `_DARKEN_STATE_IS_ACTIVE` orders two function pointers with `>`, which ISO C does not define (confirmed by `-Wpedantic`: *"ISO C forbids ordered comparisons of pointers to functions"*). It works everywhere in practice; it is not something the standard promises.

## Pitfalls and Gotchas

These go beyond "read the header comment" — each one below was reproduced with a small instrumented program against the real `darken.h` while writing this document, because the behavior is easy to get wrong just by reading the macros.

> **Deletion (and pause) requested via a return value is deferred by one full `darken_update()` cycle — not immediate.**
>
> Returning `DARKEN_DELETE` from a callback only sets `entity->update` to the sentinel value; the actual `destroy` call and removal from the active zone happen on the *next* call to `darken_update()`, when the dispatcher notices the sentinel. Measured directly:
>
> ```text
> before any update: size=1 update==DELETE? 0
> after update #1:   size=1 update==DELETE? 1   <- still "active" in the array!
> after update #2:   size=0                     <- only now actually removed
> ```
>
> The same is true for `DARKEN_PAUSE` returned from inside a callback — the function itself does stop being called right away, but the *physical* move into the paused zone also waits one extra tick. If you need pause/delete to take effect immediately (e.g. from a collision pass acting on some *other* entity), call `darken_entity_pause()`/`darken_entity_delete()` directly — those apply synchronously, with no deferral. And if any code walks a pool with a raw `DARKEN_FOREACH` (not through `darken_update()`) after entities may have requested their own death this frame, check `entity->update == DARKEN_DELETE` and skip — the header's `data` may still be logically "alive" but is one tick away from being torn down.

> **Deleting a *paused* entity skips its `destroy` callback entirely.**
>
> `darken_entity_delete()` on an active entity destroys it through the normal path (`destroy` callback runs, then the slot is recycled). On a *paused* entity it just swaps the slot straight into the free zone — no `destroy` call. Measured directly:
>
> ```text
> paused. destroy_calls=0
> deleted-while-paused.  destroy_calls=0   <- destroy callback never ran
> deleted-while-active.  destroy_calls=1   <- control case: it did run
> ```
>
> If your `destroy` callback does anything beyond spawning a visual effect — releasing an audio channel, decrementing an "in-flight" counter, freeing a secondary allocation — deleting a paused instance of that entity type will silently skip that cleanup. Either avoid deleting paused entities directly (resume them first if their destroy logic matters), or make sure whatever the destroy callback does isn't required for correctness when skipped.

> **Never delete or pause the entity currently executing its own `update` callback — return the control value instead.**
>
> Calling `darken_entity_delete(self)` synchronously from inside `self`'s own think function destroys and recycles the slot *immediately*, while that same call frame is still nested inside `darken_update()`'s dispatch for this entity. When your function then returns (say, `DARKEN_LOOP`, or a further transition), the dispatcher writes that return value into `entity->update` — on a struct that has *already* been destroyed and possibly recycled. Nothing in Darken clears `update`/`destroy` on spawn (that's explicitly the caller's job), so this can leave a freshly recycled slot carrying a leftover, stale function pointer from the entity that "deleted itself" incorrectly, invoked later against the *new* entity's completely different payload layout. Just `return DARKEN_DELETE;` (or `DARKEN_PAUSE`) — reserve `darken_entity_delete/pause()` for acting on entities *other* than the one whose callback you're currently inside (exactly how the collision examples above use it).

> **Deleting a *different*, not-yet-visited entity from inside a `DARKEN_FOREACH` pass over the *same* pool can visit one entity twice and skip another.**
>
> This is an inherent property of swap-and-shrink reverse iteration, not something specific to Darken, but it is easy to trip over in a shmup (splash damage, chain reactions). Deleting the entity *currently being visited* is always safe (see [5.6](#56-collisions-across-pools)); deleting some *other* member of the same pool as a side effect is not. Measured directly, deleting entity `id=1` while visiting `id=4` in a 5-entity pool:
>
> ```text
> while visiting id=4, deleting id=1 as a side effect
> while visiting id=4, deleting id=1 as a side effect   <- id=4 ran TWICE
> id=0 visited 1 time(s)
> id=1 visited 0 time(s)                                <- id=1 never got its own turn (deleted)
> id=2 visited 1 time(s)
> id=3 visited 1 time(s)
> id=4 visited 2 time(s)                                <- double-processed
> ```
>
> `_DARKEN_DESTROY` swaps the deleted entity's slot with whatever currently sits at the top of the active zone — which can be the very entity your `DARKEN_FOREACH` is presently on top of, relocating it into a slot your loop hasn't reached yet. If you need a "bomb that clears everything in a radius," or any effect that deletes several entities in the *same* pool as a side effect of processing one of them, collect the targets into a small local array during the pass and delete them **after** the `DARKEN_FOREACH` completes.

> **`DARKEN_FOREACH`'s loop variable is always named `_entity` (and `_pool`/`_index`) — nesting one inside another shadows it.**
>
> Perfectly legal C, but inside the inner loop's braces you cannot refer to the outer pool's current entity by the bare name `_entity` — it resolves to the *inner* loop's entity for as long as you're inside those braces (verified: it correctly reverts back once the inner loop's block closes, so nothing is corrupted — you simply lose access to the outer one *while nested*). Always capture the outer entity into your own uniquely named local **before** opening a nested `DARKEN_FOREACH` over a different pool, exactly as `bullet_entity`/`enemy_entity` do in [5.6](#56-collisions-across-pools).

> **Never assign `entity->update = DARKEN_LOOP` directly.**
>
> `DARKEN_LOOP` is only meaningful as a callback's *return value*, telling `darken_update()` "don't overwrite the current callback." `darken_update`'s dispatch only checks for "is this a real callback," "is this exactly `DARKEN_PAUSE`," or "is this exactly `DARKEN_DELETE`" — there is no branch for a bare `DARKEN_LOOP` sitting in the field. If it ever ends up there directly (rather than as a transient return value), the entity becomes a silent zombie: still inside the active zone, still visited every frame, but matching none of `darken_update`'s three branches, so it does nothing, forever, wasting a slot with no way to notice from the outside.

> **`darken_entity_run()` and `darken_entity_update()` are not interchangeable, despite the similar names.**
>
> `darken_entity_run()` calls the current callback once and **discards** whatever it returns — the entity's `update` field is left completely untouched, even if the callback returned `DARKEN_DELETE`. ` darken_entity_update()` calls it and **applies** the transition, exactly like one manual tick of `darken_update()` for a single entity. Reach for `_update()` unless you specifically want to execute a state's logic without letting it ever transition or delete — e.g. a "preview/peek" call from a debug UI that shouldn't actually advance the entity's lifecycle.

> **Always set `update`, `destroy`, `tag`, and `usr` explicitly on every spawn — never assume a recycled slot is zeroed.**
>
> `DARKEN_SPAWN()` hands back whatever memory a previous occupant of that slot left behind, header fields included; the header's own documentation is explicit about this. Skipping one "because it's usually zero anyway" is how a brand-new bullet ends up firing off a previous enemy's leftover `destroy` callback against a payload of the wrong type the moment it dies.

> **No generation counters — a saved `darken_entity` pointer can silently start referring to a different logical entity.**
>
> Because a deleted entity's *slot* gets reused by a later `DARKEN_SPAWN` (its *address* is stable, but its *identity* is not tracked at all), any raw pointer you cached across frames — a homing missile's target, a turret's parent — must be validated by your own means (an "alive" flag or a small generation counter inside your own payload) before you dereference it, rather than trusted just because it's non-`NULL`.

## Best-Practices Checklist

- [ ] Call `darken_init()` on every pool you construct, regardless of which setup macro you used.
- [ ] Call `darken_update()` on **every** pool, every frame — Darken keeps no registry of pools for you.
- [ ] Always check `DARKEN_SPAWN()`'s return value for `0` before using it.
- [ ] On every spawn, set `update`, `destroy`, `tag`, **and** `usr` explicitly — even if you don't need one of them, set it to `0`.
- [ ] To end your own entity's life, `return DARKEN_DELETE;` (or `DARKEN_PAUSE;`) — never call `darken_entity_delete()`/`_pause()` on the entity whose callback you're currently inside.
- [ ] Use `darken_entity_pause()`/`_resume()`/`_delete()` directly only from *outside* the target entity's own callback (collision passes, external systems, cutscene triggers) — that's also where they apply immediately, without the one-tick deferral of a returned control value.
- [ ] Never delete a second, not-yet-visited entity from the *same pool* you're currently walking with `DARKEN_FOREACH`; collect victims and delete them after the loop.
- [ ] Capture `_entity` (and its `DARKEN_DATA`) into your own named variable before opening a nested `DARKEN_FOREACH` over another pool.
- [ ] Never write `entity->update = DARKEN_LOOP;` — it's a return value, not a state to assign.
- [ ] Don't trust a cached `darken_entity`/`data` pointer without your own liveness check (flag or generation counter) — slots get recycled.
- [ ] Remember `darken_entity_delete()` skips `destroy` for paused entities; resume before deleting if that callback matters.
- [ ] Build with GCC or Clang in a GNU dialect (`-std=gnu11` or similar) — the statement-expression and `__attribute__` usage require it.

## Building a Shmup, Step by Step

We'll build up a small vertical shmup: a player ship, player bullets, enemies with a move/shoot cycle, enemy bullets, particle explosions, and a multi-phase boss. Every snippet below is part of one consistent, compiling example (`#define DARKEN_IMPLEMENTATION` once, in one `.c` file, before including `darken.h`).

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdlib.h>

#define SCREEN_W 224
#define SCREEN_H 256

enum {
    TAG_PLAYER = 1,
    TAG_PLAYER_BULLET,
    TAG_ENEMY,
    TAG_ENEMY_BULLET,
    TAG_BOSS,
    TAG_EXPLOSION,
};
```

The `tag` field is a cheap, payload-independent way to answer "what kind of thing is this" during broad-phase collision checks, without casting `data` first.

### 1. Choosing a pool strategy

Darken gives you three ways to stand up a `darken` context, matching three different memory strategies:

| Macro pair                                            | Storage                                | Where it can be used                                                                                            | Typical use                                                                        |
| ----------------------------------------------------- | -------------------------------------- | --------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| `DARKEN_POOL_ALLOC(alloc_fn, capacity, payload_size)` | Heap, via your allocator               | **Local/automatic variable at declaration**, or assigned at runtime with a compound-literal cast                | Pools whose size you decide at boot/level-load time                                |
| `DARKEN_POOL_DECLARE` + `DARKEN_POOL_INIT`            | Static, embedded in a generated struct | **Global/static initializer** (pure `sizeof` arithmetic, a compile-time constant expression)                    | Fixed pools sized once, no heap at all — the friendliest option for the 68k target |
| `DARKEN_POOL_DECLARE` + `DARKEN_POOL_BIND`            | Static, embedded in a generated struct | **Runtime only** (reads the declared struct's already-initialized fields, which is *not* a constant expression) | Locals, or reassigning/rebuilding a pool at runtime                                |

Two correctness notes that are easy to get wrong (both confirmed by
compiling them):

```c
// OK: DARKEN_POOL_ALLOC as a *declaration* initializer of a local variable.
void level_init(void) {
    darken bullets = DARKEN_POOL_ALLOC(malloc, 64, sizeof(bullet_t));
    darken_init(&bullets);
}

// OK: assigning a *global* later, at runtime, via a compound-literal cast.
darken bullets; // declared with no initializer at file scope
void level_init(void) {
    bullets = (darken)DARKEN_POOL_ALLOC(malloc, 64, sizeof(bullet_t));
    darken_init(&bullets);
}

// COMPILE ERROR: DARKEN_POOL_ALLOC calls malloc(), which is not a constant
// expression, so it cannot initialize a variable at file scope directly.
// darken bullets = DARKEN_POOL_ALLOC(malloc, 64, sizeof(bullet_t)); // <-- fails at global scope
```

```c
DARKEN_POOL_DECLARE(player_storage, 1, sizeof(player_t));

// OK at file scope: pure sizeof() arithmetic, a real constant expression.
darken players = DARKEN_POOL_INIT(player_storage);

// COMPILE ERROR if attempted at file scope: DARKEN_POOL_BIND reads
// player_storage's *runtime* field values (.capacity, .stride, .pool, .data),
// which gcc correctly rejects with "initializer element is not constant."
// darken players = DARKEN_POOL_BIND(player_storage); // <-- fails at global scope
```

All three still require an explicit call to `darken_init(&ctx)` afterward — none of the setup macros call it for you, and `.size`/`.paused` are left at `0` by the macros (harmless, since `darken_init()` overwrites them, but you must remember to call it).

### 2. The player

```c
typedef struct {
    int16_t x, y;
    uint8_t lives;
    uint8_t fire_cd;
} player_t;

DARKEN_POOL_DECLARE(player_storage, 1, sizeof(player_t));
darken players = DARKEN_POOL_INIT(player_storage);

void *player_update(player_t *p);

void spawn_player(int16_t x, int16_t y) {
    darken_entity e = DARKEN_SPAWN(&players);
    if (!e) return; // capacity is 1, but never skip this check anyway

    DARKEN_DATA(player_t, p, e);
    p->x = x; p->y = y; p->lives = 3; p->fire_cd = 0;

    e->update  = (darken_state)player_update;
    e->destroy = (darken_state)0;
    e->tag     = TAG_PLAYER;
    e->usr     = 0;
}

void *player_update(player_t *p) {
    if (input.left)  p->x -= 2;
    if (input.right) p->x += 2;
    if (input.up)    p->y -= 2;
    if (input.down)  p->y += 2;

    if (p->fire_cd) p->fire_cd--;
    if (input.fire && p->fire_cd == 0) {
        spawn_player_bullet(p->x, p->y - 8);
        p->fire_cd = 6;
    }

    return DARKEN_LOOP; // stay in player_update forever
}
```

`DARKEN_DATA(TYPE, VAR, ENTITY)` is just a typed cast of `->data` — a small readability macro, nothing more.

Two things worth flagging immediately, because they show up in every entity type from here on:

- `player_update` is declared `void *player_update(void *data)`, but `darken_state` is `void *(*)()` — an **unprototyped** function pointer type (old K&R style: "unspecified arguments," not "no arguments"). Every assignment to `->update`/`->destroy` needs an explicit `(darken_state)` cast, and the compiler will not check that your callback's real signature matches what Darken will actually call it with. Get the signature wrong and you get silent undefined behavior, not a compile error.
- Both `update` **and** `destroy` are set explicitly, even though we don't want a destroy callback here. `DARKEN_SPAWN()` never clears a recycled slot's fields — see [Pitfalls](#pitfalls-and-gotchas) for why skipping this is dangerous, not just untidy.

### 3. Player bullets

```c
typedef struct {
    int16_t x, y;
    int8_t  vy;
} bullet_t;

darken player_bullets;

void *bullet_update(bullet_t *b) {
    b->y += b->vy;
    if (b->y < -8 || b->y > SCREEN_H + 8)
        return DARKEN_DELETE; // off-screen: gone
    return DARKEN_LOOP;
}

void spawn_player_bullet(int16_t x, int16_t y) {
    darken_entity e = DARKEN_SPAWN(&player_bullets);
    if (!e) return; // pool full this frame -- the shot is simply dropped

    DARKEN_DATA(bullet_t, b, e);
    b->x = x; b->y = y; b->vy = -4;

    e->update  = (darken_state)bullet_update;
    e->destroy = (darken_state)0;
    e->tag     = TAG_PLAYER_BULLET;
    e->usr     = 0;
}
```

`DARKEN_SPAWN` returns `0` (not a valid entity address) when the pool has no free slots left. It is easy to forget this check in the heat of adding "just one more bullet type" — do not. A shmup with an under-sized bullet pool and no NULL check will crash the instant the screen gets busy.

### 4. Enemies and multi-state patterns

This is where the think-function state machine earns its keep: an enemy alternates between a "move" state and a "shoot" state simply by returning a different function pointer.

```c
typedef struct {
    int16_t x, y;
    int8_t  dx;
    uint8_t hp;
    uint8_t timer;
} enemy_t;

darken enemies;

void *enemy_shoot(enemy_t *en);

void *enemy_move(enemy_t *en) {
    en->x += en->dx;
    if (en->x < 8 || en->x > SCREEN_W - 8) en->dx = -en->dx;

    if (--en->timer == 0) {
        en->timer = 45;
        return (void *)enemy_shoot; // switch state
    }
    return DARKEN_LOOP; // keep moving
}

void *enemy_shoot(enemy_t *en) {
    spawn_enemy_bullet(en->x, en->y + 8);
    return (void *)enemy_move; // switch back
}

void *enemy_die(enemy_t *en) {
    spawn_explosion(en->x, en->y);
    return (void *)0;
}

void spawn_enemy(int16_t x, int16_t y) {
    darken_entity e = DARKEN_SPAWN(&enemies);
    if (!e) return;

    DARKEN_DATA(enemy_t, en, e);
    en->x = x; en->y = y; en->dx = 1; en->hp = 3; en->timer = 45;

    e->update  = (darken_state)enemy_move;
    e->destroy = (darken_state)enemy_die;
    e->tag     = TAG_ENEMY;
    e->usr     = 0;
}
```

Note that `enemy_shoot` is *forward-declared*, because `enemy_move` needs to name it before it exists — a normal, minor bit of C ceremony that comes with this pattern once you have more than one or two states.

### 5. Enemy bullets

Structurally identical to player bullets, in their own pool with their own tag (kept separate on purpose — see [5.6](#56-collisions-across-pools)):

```c
typedef struct { int16_t x, y; int8_t vy; } enemy_bullet_t;
darken enemy_bullets;

void *enemy_bullet_update(enemy_bullet_t *b) {
    b->y += b->vy;
    if (b->y > SCREEN_H + 8) return DARKEN_DELETE;
    return DARKEN_LOOP;
}

void spawn_enemy_bullet(int16_t x, int16_t y) {
    darken_entity e = DARKEN_SPAWN(&enemy_bullets);
    if (!e) return;
    DARKEN_DATA(enemy_bullet_t, b, e);
    b->x = x; b->y = y; b->vy = 3;
    e->update  = (darken_state)enemy_bullet_update;
    e->destroy = (darken_state)0;
    e->tag     = TAG_ENEMY_BULLET;
    e->usr     = 0;
}
```

Darken has no concept of "one bullet pool with a `side` field" versus "separate player/enemy bullet pools" — either design works, but separate pools per logical role (as used throughout this README) keep each pool's capacity, stride, and collision rules independent and easy to reason about, which matters once you're tuning bullet-hell density.

### 6. Collisions across pools

Darken doesn't do collision detection, but `DARKEN_FOREACH` plus stable `data` pointers make writing your own straightforward — **with one sharp edge**. `DARKEN_FOREACH`'s loop variable is always literally named `_entity` (and internally `_index`, `_pool`). Nesting a second `DARKEN_FOREACH` inside the first's body **shadows** those names for as long as you're inside the inner braces — perfectly legal C, but it means you lose access to the outer entity by name unless you captured it first:

```c
void check_player_bullets_vs_enemies(void) {
    DARKEN_FOREACH(&player_bullets, {
        darken_entity bullet_entity = _entity;      // capture BEFORE nesting
        DARKEN_DATA(bullet_t, b, bullet_entity);
        if (bullet_entity->update == DARKEN_DELETE) continue; // see note below

        DARKEN_FOREACH(&enemies, {
            darken_entity enemy_entity = _entity;   // this loop's OWN _entity
            DARKEN_DATA(enemy_t, en, enemy_entity);
            if (enemy_entity->update == DARKEN_DELETE) continue;

            if (colliding(b->x, b->y, en->x, en->y)) {
                darken_entity_delete(bullet_entity); // deletes THIS loop's current bullet
                if (--en->hp == 0)
                    darken_entity_delete(enemy_entity); // deletes THIS loop's current enemy
                break;
            }
        });
    });
}
```

This is safe for a subtle reason worth spelling out: **both deletions target the entity each `DARKEN_FOREACH` is *currently visiting in its own pass*.** Deleting "yourself" mid-visit is exactly what the swap-and-shrink scheme is designed for. What is *not* safe is deleting some *other*, not-yet-visited entity in the *same pool* you're currently iterating — see [Pitfalls](#pitfalls-and-gotchas) for a measured demonstration of what goes wrong.

The `if (... ->update == DARKEN_DELETE) continue;` guards exist because of the deferred-deletion behavior described in [Pitfalls](#pitfalls-and-gotchas): an entity that decided to die during this frame's `darken_update()` is still physically present in the active zone for one more tick, and a collision pass that runs after `darken_update()` in the same frame will still see it unless you check.

### 7. Explosions via the destroy callback

```c
typedef struct { int16_t x, y; uint8_t ttl, frame; } particle_t;
darken particles;

void *particle_update(particle_t *p) {
    p->frame++;
    if (--p->ttl == 0) return DARKEN_DELETE;
    return DARKEN_LOOP;
}

void spawn_explosion(int16_t x, int16_t y) {
    darken_entity e = DARKEN_SPAWN(&particles);
    if (!e) return;
    DARKEN_DATA(particle_t, p, e);
    p->x = x; p->y = y; p->ttl = 20; p->frame = 0;
    e->update  = (darken_state)particle_update;
    e->destroy = (darken_state)0;
    e->tag     = TAG_EXPLOSION;
    e->usr     = 0;
}
```

`enemy_die` (5.4) and `boss_die` (5.8) both call `spawn_explosion()` from inside a `destroy` callback — that's the intended use of the second callback slot: side effects that should happen exactly once, at the moment an *active* entity is actually torn down. ("Exactly once, when active" is doing real work in that sentence — see the paused/delete pitfall below.)

`DARKEN_DATA_GET_ENTITY` is the reverse operation: given only a `data` pointer (e.g. inside a helper shared by several entity types that never receives the `darken_entity` handle directly), it reconstructs the owning entity so you can act on it:

```c
typedef struct { int16_t x, y; uint8_t hp; } damageable_t; // shared leading layout

void apply_damage(damageable_t *d) {
    if (d->hp <= d->amount) {
        d->hp = 0;
        darken_entity self = DARKEN_DATA_GET_ENTITY(data);
        darken_entity_delete(self); // safe: apply_damage() is called from the
                                    // collision pass, never from inside this
                                    // entity's own currently-running update()
    } else
        d->hp -= d->amount;
}
```

That last comment matters — see ["Never delete/pause yourself from inside your own callback"](#pitfalls-and-gotchas) below for why the calling context is what makes this safe.

### 8. A boss fight: phases, pause and resume

A boss is just another pooled entity (capacity 1 is fine — pooling it keeps the code path identical to everything else). Its state machine walks through an intro, then hands off between attack phases based on remaining HP:

```c
typedef struct { int16_t x, y; uint8_t hp, phase; uint16_t timer; } boss_t;
darken bosses;

void *boss_intro(boss_t *bo);
void *boss_phase1(boss_t *bo);
void *boss_phase2(boss_t *bo);

void *boss_die(boss_t *bo) {
    spawn_explosion(bo->x, bo->y);
    spawn_explosion(bo->x - 12, bo->y - 6);
    spawn_explosion(bo->x + 12, bo->y + 6);
    return (void *)0;
}

void spawn_boss(void) {
    darken_entity e = DARKEN_SPAWN(&bosses);
    if (!e) return;
    DARKEN_DATA(boss_t, bo, e);
    bo->x = SCREEN_W / 2; bo->y = -40; bo->hp = 120; bo->phase = 0; bo->timer = 0;
    e->update  = (darken_state)boss_intro;
    e->destroy = (darken_state)boss_die;
    e->tag     = TAG_BOSS;
    e->usr     = 0;

    // Freeze the whole arena for the entrance: pause every currently-active
    // enemy and enemy bullet so nothing moves or shoots while the boss flies in.
    DARKEN_FOREACH(&enemies,       { darken_entity_pause(_entity); });
    DARKEN_FOREACH(&enemy_bullets, { darken_entity_pause(_entity); });
}

void *boss_intro(boss_t *bo) {
    bo->y += 1;
    if (bo->y >= 40) {
        DARKEN_FOREACH(&enemies,       { darken_entity_resume(_entity); });
        DARKEN_FOREACH(&enemy_bullets, { darken_entity_resume(_entity); });
        return (void *)boss_phase1;
    }
    return DARKEN_LOOP;
}

void *boss_phase1(boss_t *bo) {
    // ... movement + bullet pattern for phase 1 ...
    if (bo->hp < 60) return (void *)boss_phase2;
    return DARKEN_LOOP;
}

void *boss_phase2(boss_t *bo) {
    // ... faster / denser pattern for phase 2 ...
    return DARKEN_LOOP;
}
```

This is exactly the scenario `darken_entity_pause`/`darken_entity_resume` are for: pulling a batch of entities out of `darken_update()`/ `DARKEN_FOREACH` entirely, then putting them back later, with their state (HP, position, current think-function) completely intact because their memory address never moved. Unlike returning `DARKEN_PAUSE` from inside a callback, calling `darken_entity_pause()`/`darken_entity_resume()` directly like this **takes effect immediately** — no deferred tick (see [Pitfalls](#pitfalls-and-gotchas)).

### 9. The frame loop, assembled

```c
void game_init(void) {
    darken_init(&players);

    player_bullets = (darken)DARKEN_POOL_ALLOC(malloc, 64,  sizeof(bullet_t));
    enemies        = (darken)DARKEN_POOL_ALLOC(malloc, 32,  sizeof(enemy_t));
    enemy_bullets  = (darken)DARKEN_POOL_ALLOC(malloc, 128, sizeof(enemy_bullet_t));
    particles      = (darken)DARKEN_POOL_ALLOC(malloc, 64,  sizeof(particle_t));
    bosses         = (darken)DARKEN_POOL_ALLOC(malloc, 1,   sizeof(boss_t));

    darken_init(&player_bullets);
    darken_init(&enemies);
    darken_init(&enemy_bullets);
    darken_init(&particles);
    darken_init(&bosses);

    spawn_player(SCREEN_W / 2, SCREEN_H - 32);
}

typedef void (*draw_fn)(darken_entity);
void render_pool(darken *pool, draw_fn draw) { DARKEN_FOREACH(pool, { draw(_entity); }); }

void game_frame(void) {
    read_input();

    // 1) advance every pool's state machines
    darken_update(&players);
    darken_update(&player_bullets);
    darken_update(&enemies);
    darken_update(&enemy_bullets);
    darken_update(&bosses);
    darken_update(&particles);

    // 2) resolve collisions (may call darken_entity_delete directly)
    check_player_bullets_vs_enemies();
    check_player_vs_enemy_bullets();
    check_player_vs_enemies();

    // 3) draw back-to-front
    render_pool(&particles,      draw_particle);
    render_pool(&enemy_bullets,  draw_enemy_bullet);
    render_pool(&enemies,        draw_enemy);
    render_pool(&bosses,         draw_boss);
    render_pool(&player_bullets, draw_bullet);
    render_pool(&players,        draw_player);
}
```

Notice every pool needs its own explicit `darken_update()` call — Darken has no registry of "all pools that exist." Forgetting one is a silent bug: that pool's entities simply stop advancing (and stop dying), while still drawing in whatever their last state was.

---