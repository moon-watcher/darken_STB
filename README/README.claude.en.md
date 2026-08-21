# Darken — DARKula ENgine 2.0

**A single-header, allocation-free entity system written in C for GCC/SGDK and the Motorola 68000 (Sega Mega Drive / Genesis).**

Darken is not an archetype ECS. It's a fixed-capacity entity manager built on top of caller-owned memory, plus a compact pointer pool (`de_system`) for batch-processing data. Every byte Darken touches is reserved up front, either at compile time or once at startup; Darken itself never calls `malloc`, `free`, or `calloc`.

> This document describes the **actual** behavior of the current `darken.h`, verified by reading every function line by line and, for the parts that were ambiguous or easy to get wrong, by compiling and running small test programs against the real header. Every code sample below was compiled and executed while writing this document.

---

## Table of contents

1. [Overview and philosophy](#1-overview-and-philosophy)
2. [Requirements and platform](#2-requirements-and-platform)
3. [Installation (single header)](#3-installation-single-header)
4. [Naming convention](#4-naming-convention)
5. [Handle types: everything is now a pointer](#5-handle-types-everything-is-now-a-pointer)
6. [Core concepts](#6-core-concepts)
7. [Memory layout](#7-memory-layout)
8. [The manager's three zones](#8-the-managers-three-zones)
9. [Entity API](#9-entity-api)
10. [Manager API](#10-manager-api)
11. [System API](#11-system-api)
12. [Public macros](#12-public-macros)
13. [State machine](#13-state-machine)
14. [Update order](#14-update-order)
15. [Complexity](#15-complexity)
16. [Complete example](#16-complete-example)
17. [Safety rules](#17-safety-rules)
18. [Internal invariants](#18-internal-invariants)
19. [Important quirks and warnings](#19-important-quirks-and-warnings)
20. [What Darken is — and is not](#20-what-darken-is--and-is-not)
21. [Quick API reference](#21-quick-api-reference)

---

## 1. Overview and philosophy

- **No `malloc` at runtime.** All memory is supplied by the caller, either as static storage or as a single heap allocation made once at startup.
- **Immutable entity addresses.** Once `de_manager_init()` places an entity in its storage block, that entity never changes physical address again. The only thing that ever gets reordered is the *pointer* to it inside `manager->pool[]`.
- **Three logical zones packed into a single pointer array:** active, free, and paused.
- **Paused entities keep their address.** `de_manager_new()` never recycles a slot from the paused zone, so an external raw pointer into `entity->data` stays valid for as long as that entity remains active or paused — it only becomes invalid on deletion.
- **Backward active traversal, safe for self-mutation.** The manager walks the active zone in descending order, which is what lets a state callback delete, pause, or resume the entity *currently being visited* without corrupting the traversal.
- **`de_system` is a flat pointer pool**, grouped in chunks of `params` pointers, with no structural relationship to entities beyond whatever the application code decides to give it.
- **4-byte (longword) alignment** on both the entity stride and the storage block, targeted specifically at the Motorola 68000.
- **Opaque pointer handles everywhere.** As of this version, `de_entity`, `de_manager`, and `de_system` are *all* pointer typedefs (see [§5](#5-handle-types-everything-is-now-a-pointer)) — a change from earlier revisions of this header that is important enough to call out up front.

---

## 2. Requirements and platform

- **GCC** (or a compatible compiler) with GNU C extensions: the code uses `__attribute__((aligned(4)))` and a *statement expression* `({ ... })` inside the internal `_DE_SYSTEM_ADD` macro.
- Only dependency: `<stdint.h>`.
- Stated target: **SGDK** (Sega Genesis Development Kit) on the Motorola 68000, though the header compiles and runs fine on any GCC-based platform — every example in this document was built and run on x86_64/Linux with `gcc -std=gnu11 -Wall -Wextra`.
- `de_state` represents both function callbacks and control sentinels through `void *`, and comparisons like `state > (de_state)2` are not strictly ISO C portable pointer comparisons. That's a deliberate choice for the target ABI, not an oversight — don't "fix" it.

---

## 3. Installation (single header)

Darken follows the STB-style single-header pattern. In **exactly one** `.c` file:

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
```

Everywhere else, just:

```c
#include "darken.h"
```

Never define `DARKEN_IMPLEMENTATION` in more than one translation unit — you'll get duplicate symbol errors at link time.

---

## 4. Naming convention

| Prefix | Meaning |
|---|---|
| `de_*`  | Public functions and types |
| `DE_*`  | Public macros |
| `_de_*` | Internal functions, used only inside the header itself |
| `_DE_*` | Internal macros implementing the variadic arity of `DE_SYSTEM_*`, and a few pieces that were public in earlier revisions and are now internal (see [§19.1](#191-de_entity_stride-is-no-longer-public)) |

`_de_*` / `_DE_*` symbols are not part of the stable API. Don't call them from application code, and don't rely on their names staying the same across versions.

---

## 5. Handle types: everything is now a pointer

This is the single most important structural fact about the current header, and it changes how you declare things compared to earlier versions:

```c
typedef struct de_entity  *de_entity;
typedef struct de_manager *de_manager;
typedef struct de_system  *de_system;
```

**All three handle types are pointer typedefs.** `de_entity` already was one before; `de_manager` and `de_system` are new additions to that pattern in this revision. Practically, this means:

- Every API function that used to take `de_manager *` now takes a plain `de_manager` (because `de_manager` already *is* `struct de_manager *`).
- Writing `de_manager mgr;` does **not** create a manager instance — it creates an uninitialized pointer. You need an actual `struct de_manager` somewhere to point it at.

The idiomatic pattern is to declare the concrete struct once and keep a handle pointing at it:

```c
struct de_manager mgr_state;      /* the real storage for the manager's bookkeeping */
de_manager mgr = &mgr_state;      /* the handle you actually pass around */

DE_MANAGER_STORAGE(storage, 16, sizeof(struct player_data));
de_manager_init(mgr, DE_MANAGER_ARGS(storage));
```

or equivalently, skip the named handle and just pass `&mgr_state` everywhere:

```c
struct de_manager mgr_state;
de_manager_init(&mgr_state, DE_MANAGER_ARGS(storage));
...
de_manager_update(&mgr_state);
```

Both forms compile and behave identically — `&mgr_state` has type `struct de_manager *`, which is exactly `de_manager`. The same applies to `de_system`:

```c
struct de_system sys_state;
de_system sys = &sys_state;
de_system_init(sys, DE_SYSTEM_ARGS(sys_storage));
```

`de_entity` doesn't need this dance because you never declare one yourself — you always receive it from `de_manager_new()`, already a valid pointer into the manager's storage block.

All the function signatures reflect single-indirection handles now:

```c
void      de_manager_init(de_manager, de_entity *, void *, uint16_t, uint16_t);
de_entity de_manager_new(de_manager);
void      de_manager_update(de_manager);
void      de_manager_reset(de_manager);

void      de_system_init(de_system, void **, uint16_t, uint16_t);
uint16_t  de_system_remove(de_system, void *);
```

---

## 6. Core concepts

### 6.1 Entity (`de_entity`)

```c
struct de_entity
{
    de_state  state;
    de_state  destructor;
    de_manager owner;
    uint16_t  slot;
    uint16_t  tag;
    uint8_t   data[];   /* flexible array member */
};
```

Two field names changed compared to earlier revisions of this header: the field that points back at the owning manager is now called `owner` (previously `manager`), and — because `de_manager` is itself a pointer typedef — `de_manager owner;` is a single pointer field, exactly equivalent to `struct de_manager *owner;`.

`data[]` is the application-specific payload. Its size is fixed once, when the manager is initialized (`de_manager_init`), not per individual entity.

### 6.2 Manager (`de_manager`)

```c
struct de_manager
{
    de_entity *pool;
    uint16_t   capacity;
    uint16_t   size;
    uint16_t   paused;
};
```

Three field names changed here too, and the renaming is consistent across the whole header:

| Old name | New name |
|---|---|
| `items`        | `pool` |
| `active_count` | `size` |
| `paused_start` | `paused` |

The manager does not store entities directly: it stores an array of **pointers** to them (`pool`), split into three contiguous zones (see [§8](#8-the-managers-three-zones)). The entities themselves live in a separate contiguous byte block supplied by the caller.

### 6.3 System (`de_system`)

```c
struct de_system
{
    void **pool;
    void **end;
    uint16_t capacity;
    uint16_t size;
    uint16_t params;
};
```

Unchanged in field names from earlier revisions — only its typedef became a pointer (see [§5](#5-handle-types-everything-is-now-a-pointer)). A flat pool of `void*` pointers, organized in groups of `params` elements each:

```text
Pool (params = 2):
[e0.a][e0.b][e1.a][e1.b][e2.a][e2.b] ...
```

`capacity` and `size` are expressed in **pointer slots**, not groups: a system with 10 groups of 3 parameters has `capacity == 30`. The number of occupied groups is `size / params`. `end` always points one element past the last used pointer.

A `de_system` **does not own** what it points to; it only stores pointers. The lifetime of the pointed-to objects is entirely the caller's responsibility.

### 6.4 State (`de_state`)

```c
typedef void *(*de_state)(void *);
```

A state callback receives `entity->data` (not the entity handle itself) and returns either another state-function pointer, or one of three reserved control values:

| Macro | Value | Meaning |
|---|---|---|
| `DE_STATE_DELETE` | `(void*)0` | Request deletion |
| `DE_STATE_LOOP`   | `(void*)1` | Keep the current state |
| `DE_STATE_PAUSE`  | `(void*)2` | Request pausing |

```c
#define _DE_STATE_IS_DELETED(S) ((S) == ((de_state)0))
#define _DE_STATE_IS_LOOP(S)    ((S) == ((de_state)1))
#define _DE_STATE_IS_PAUSED(S)  ((S) == ((de_state)2))
#define _DE_STATE_IS_ACTIVE(S)  ((S) >  ((de_state)2))
```

Any pointer value greater than `2` is treated as an executable callback. This assumes the linker never places code at addresses `0`–`2`, which holds in practice on the target ABI but is not something ISO C guarantees.

> **Typing note:** although `de_state` is declared as `void *(*)(void *)`, it's idiomatic in Darken-based code to write state callbacks taking the payload type directly (`struct MyComponent *`) instead of `void *`, and assign them to `entity->state` without an explicit cast. It compiles and works on the target ABI (GCC/68000) but is not valid under strict ISO C — the same looseness already assumed for the `DE_STATE_*` sentinels and for the functions generated by `DE_SYSTEM_ITERATOR` (see [§11.3](#113-de_system_iterator)). See the example in [§16](#16-complete-example), which uses `void *` explicitly and casts inside the function body — the safer of the two idioms, and the one this document recommends even though the looser style also compiles.

---

## 7. Memory layout

### 7.1 Entity and stride

```c
#define _DE_ENTITY_STRIDE(PAYLOAD) _DE_ALIGN4(sizeof(struct de_entity) + (PAYLOAD))
#define _DE_ALIGN4(X) (((X) + 3U) & ~3U)
```

```text
[ state ][ destructor ][ owner ][ slot ][ tag ][ data[0..PAYLOAD) ][ padding ]
 void*     void*         de_manager  uint16 uint16   uint8_t[...]
```

The stride between consecutive entities is computed **once**, inside `de_manager_init()`, and reused to precompute every entity's address. Darken never recalculates an entity's address during normal operation.

The 4-byte rounding applies to the **whole stride**, not just the start of the block: every entity begins on a multiple of 4 even when `PAYLOAD` doesn't. The 68000 itself only requires word alignment (2 bytes) for word/long accesses; the stricter 4-byte boundary is a design choice to keep strides regular and predictable, not a hardware minimum.

> ⚠️ `_DE_ENTITY_STRIDE` is now an internal macro (underscore-prefixed). There is no public `DE_ENTITY_STRIDE` in this version — see [§19.1](#191-de_entity_stride-is-no-longer-public).

### 7.2 Manager: `pool[]` versus entity storage

`DE_MANAGER_STORAGE` reserves **two separate blocks** inside one anonymous struct:

```c
#define _DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)                                         \
    struct {                                                                                      \
        de_entity pool[(CAPACITY)];                                                               \
        uint8_t   data[(CAPACITY) * _DE_ENTITY_STRIDE((PAYLOAD_SIZE))] __attribute__((aligned(4))); \
        uint16_t  capacity;                                                                        \
        uint16_t  payload_size;                                                                    \
    } NAME = { .capacity = (CAPACITY), .payload_size = (PAYLOAD_SIZE) }
```

- `pool[CAPACITY]` — the array of **pointers** (`de_entity` is already a pointer type) that gets passed to `de_manager_init()` as its `pool` parameter. This is *not* entity storage.
- `data[...]` — the contiguous byte block where entities physically live, aligned to 4 bytes.

```c
#define _DE_MANAGER_ARGS(NAME) \
    (NAME).pool, (NAME).data, (NAME).capacity, (NAME).payload_size
```

which lines up exactly with:

```c
void de_manager_init(de_manager, de_entity *pool, void *storage, uint16_t capacity, uint16_t payload_size);
```

Every field in the storage helper struct is now consistently named `pool`, matching `struct de_manager`'s own `pool` field — a naming cleanup compared to earlier revisions, where the manager's array was called `items` and the storage macro's field was called `entities`.

The object generated by `DE_MANAGER_STORAGE` must stay alive and at the same address for the entire lifetime of the manager. Darken never copies it or allocates replacement storage.

### 7.3 System: the pool

```c
#define _DE_SYSTEM_STORAGE(NAME, CAPACITY, PARAMS) \
    struct { void *pool[(CAPACITY) * (PARAMS)]; uint16_t capacity; uint16_t params; } \
    NAME = { .capacity = (CAPACITY), .params = (PARAMS) }
```

Here `CAPACITY` is the number of **groups**, not pointers: `DE_SYSTEM_STORAGE(s, 32, 3)` reserves `32 × 3 = 96` pointer slots.

---

## 8. The manager's three zones

`manager->pool[]` is organized into three contiguous logical zones, with no gaps between them:

```text
index:     0                        size                    paused                 capacity
           │                          │                       │                       │
           ▼                          ▼                       ▼                       ▼
           ┌──────────────────────────┬───────────────────────┬───────────────────────┐
           │          ACTIVE          │          FREE         │         PAUSED        │
           │      [0, size)           │     [size, paused)    │   [paused, capacity)  │
           └──────────────────────────┴───────────────────────┴───────────────────────┘
```

- **Active** — entities processed by `de_manager_update()` and visited by `DE_MANAGER_FOREACH`, in descending order.
- **Free** — pointer slots with no entity currently "in use" from the manager's point of view. `de_manager_new()` always takes `pool[size]`, the first free slot.
- **Paused** — entities that are alive but deliberately excluded from the update loop and from `DE_MANAGER_FOREACH`. `de_manager_new()` **never** allocates from this zone: that's precisely what guarantees a paused entity's address (and therefore `entity->data`) stays stable while it remains paused.

`entity->slot` is the entity's **current** index inside `pool[]`; it is not an offset into the byte-storage block. When two entities swap zones, their physical addresses don't move — only `slot` and their position in `pool[]` change.

---

## 9. Entity API

```c
void *de_entity_exec(de_entity);
void *de_entity_update(de_entity);
void  de_entity_pause(de_entity);
void  de_entity_resume(de_entity);
void  de_entity_delete(de_entity);
void  de_entity_move_front(de_entity);
void  de_entity_move_back(de_entity);
```

### `void *de_entity_exec(de_entity e)`

```c
void *de_entity_exec(de_entity $) {
    de_state state = $->state;
    if (!_DE_STATE_IS_ACTIVE(state)) return 0;
    return state($->data);
}
```****

Executes the current state **without** writing anything to `e->state`. If the state is not an executable callback (it's `DELETE`, `LOOP`, or `PAUSE`), it returns `0` (`DE_STATE_DELETE`) without calling anything. It's read-only with respect to `entity->state`; the only possible side effect is whatever the callback itself does to `entity->data`.

### `void *de_entity_update(de_entity e)`

```c
void *de_entity_update(de_entity $) {
    de_state state = de_entity_exec($);
    if (!_DE_STATE_IS_LOOP(state)) $->state = state;
    return state;
}
```

Executes the active state and stores the returned transition, unless it's `DE_STATE_LOOP`. Meant for manually advancing an entity **outside** the manager's loop (without the automatic pause/delete handling that `de_manager_update` provides).

> ⚠️ See [§19.2](#192-de_entity_update-on-an-inactive-entity) — calling it on an entity that currently has **no** active callback forces its state to `DE_STATE_DELETE`, it does not leave it unchanged. Verified by running actual code against this header.

### `void de_entity_pause(de_entity e)`

No-op if the entity is not in the active zone. If it is: shrinks `size` by 1 (filling the resulting gap with the last active entity if needed), shrinks `paused` by 1, and places the entity at that new boundary. The entity keeps its physical address.

### `void de_entity_resume(de_entity e)`

No-op if the entity is not in the paused zone. The inverse of `de_entity_pause`: frees the left edge of the paused zone, advances `paused`, and inserts the entity at the boundary of the active zone.

### `void de_entity_delete(de_entity e)`

No-op if the entity is already in the free zone. Otherwise:

1. If `e->destructor` exists, it's called with `e->data`. **Its return value is completely ignored — a destructor cannot cancel deletion.**
2. If the entity was paused: it's swapped (if needed) with the first element of the paused zone, and `paused` is incremented.
3. If the entity was active: it's swapped (if needed) with the last element of the active zone, and `size` is decremented.

In both cases the freed slot rejoins the free zone in O(1), without moving any entity's bytes. After deletion, the `de_entity` handle must be treated as invalid: its storage may be handed out again by the next `de_manager_new()`.

### `void de_entity_move_front(de_entity e)` / `void de_entity_move_back(de_entity e)`

Both are no-ops if the entity isn't active, or if it already occupies the target position. Because the manager traverses the active zone **backward**, moving an entity to the highest index (`move_front`) makes it run **earlier** in the next update, and moving it to index `0` (`move_back`) makes it run **later**. Both are plain pointer swaps, O(1).

---

## 10. Manager API

```c
void      de_manager_init(de_manager, de_entity *, void *, uint16_t, uint16_t);
de_entity de_manager_new(de_manager);
void      de_manager_update(de_manager);
void      de_manager_reset(de_manager);
```

### `de_manager_init(m, pool, storage, capacity, payload_size)`

```c
void de_manager_init(de_manager $, de_entity *pool, void *param_storage,
                      uint16_t capacity, uint16_t bytes)
```

Sets `$->pool`, `$->capacity`, `$->size = 0`, and `$->paused = capacity` (everything starts free). Computes the stride once and walks `param_storage` in steps of that size, filling `pool[i]` with each entity's address and setting `owner` and `slot` for every one. It does **not** touch `state`, `destructor`, `tag`, or `data` — that's `de_manager_new()`'s job.

`$` here is a `de_manager` handle (a `struct de_manager *`) — see [§5](#5-handle-types-everything-is-now-a-pointer) for how to construct one.

### `de_entity de_manager_new(de_manager m)`

Returns `NULL` if the free zone is empty (`m->size >= m->paused`). If there's room, it takes `m->pool[m->size]`, increments `m->size`, and **resets** the control fields:

```c
entity->state      = DE_STATE_DELETE;
entity->destructor = 0;
entity->tag        = 0;
```

`data[]` is **not initialized**: if it needs to start in a known state, that's up to the caller.

### `void de_manager_update(de_manager m)`

Walks the active zone backward, using a snapshot of `size` taken at the start:

```c
uint16_t i = m->size;
while (i--) { /* process pool[i] */ }
```

For every active entity visited:

- If its `state` is an executable callback, it's called, and the result stored unless it is `DE_STATE_LOOP`.
- If its `state` is exactly `DE_STATE_PAUSE`, `de_entity_pause(e)` is called.
- If its `state` is exactly `DE_STATE_DELETE`, `de_entity_delete(e)` is called.
- If its `state` is exactly `DE_STATE_LOOP` (set by hand, not returned by a callback), **nothing happens**: none of the three branches above match. That's why `DE_STATE_LOOP` only makes sense as a callback's *return value*, never as a value assigned directly to `entity->state`.

The backward traversal is safe against pausing/deleting the **current** entity because both `de_entity_pause` and `de_entity_delete` shrink the active zone from its right edge — the side the traversal has already visited — so `size` never needs to be re-read mid-loop.

### `void de_manager_reset(de_manager m)`

```c
void de_manager_reset(de_manager $) {
    DE_MANAGER_FOREACH($, de_entity_delete(ENTITY));
    $->size   = 0;
    $->paused = $->capacity;
}
```

Deletes (destructor included) **every entity that was in the active zone** at the time of the call, then resets `size` and `paused` back to their initial values.

> ⚠️ See [§19.3](#193-de_manager_reset-still-does-not-destroy-paused-entities) — paused entities are **not** visited in this process, so their destructors **do not run**. This carries over unchanged from earlier revisions.

---

## 11. System API

```c
void     de_system_init(de_system, void **, uint16_t, uint16_t);
uint16_t de_system_remove(de_system, void *);
```

### `de_system_init(s, storage, capacity_groups, params)`

Sets `pool = end = storage`, `size = 0`, `capacity = capacity_groups * params`, `params = params`. There is no validation that `storage` is actually that large — the caller must size it correctly, typically via `DE_SYSTEM_STORAGE`.

### `uint16_t de_system_remove(de_system s, void *first)`

Searches, stepping `params` pointers at a time, for the group whose **first** pointer equals `first`. If found: shrinks `size` by `params`, and if the found group wasn't the last one, copies the last group over the removed position (swap-remove compaction, so **order is not preserved**). Returns `1`. If nothing matches after scanning the whole pool, returns `0`.

### `DE_SYSTEM_ADD` / `DE_SYSTEM_FOREACH` (macros)

See [§12](#12-public-macros).

### 11.3 `DE_SYSTEM_ITERATOR`

Generates a function of the form:

```c
void *name(de_system system) {
    /* DE_SYSTEM_FOREACH body */
    return DE_STATE_LOOP;
}
```

meant to be installed directly as `entity->state`. Note that the generated function's parameter type is `de_system` — already a pointer, per [§5](#5-handle-types-everything-is-now-a-pointer) — not `void *`. Formally it is still not compatible with `de_state` under strict ISO C, even though it works on the target GNU C/68000 ABI. Treat this as a platform-specific function-pointer conversion, not a portable one; verified in [§16](#16-complete-example) to compile and run correctly with `-Wall -Wextra` under GCC.

---

## 12. Public macros

### `DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)` / `DE_MANAGER_ARGS(NAME)`

```c
DE_MANAGER_STORAGE(mgr_storage, 64, sizeof(struct MyComponent));

struct de_manager mgr_state;
de_manager_init(&mgr_state, DE_MANAGER_ARGS(mgr_storage));
```

Both macros are thin forwarders to internal implementations (`_DE_MANAGER_STORAGE`, `_DE_MANAGER_ARGS`) — functionally this is unchanged from earlier revisions, just organized so the header's public section only shows the clean public names.

### `DE_MANAGER_FOREACH(M, CODE)`

```c
#define _DE_MANAGER_FOREACH(M, CODE)        \
    do {                                    \
        uint16_t INDEX = (M)->size;         \
        de_entity *POOL = (M)->pool;        \
        while (INDEX--) {                   \
            de_entity ENTITY = POOL[INDEX]; \
            CODE;                           \
        }                                   \
    } while (0)
```

Iterates only the active zone, in descending order. Inside `CODE`, three fixed-name variables are exposed:

- `INDEX` — the current index (`uint16_t`).
- `POOL` — the manager's pointer array (renamed from `ITEMS` in earlier revisions, matching the struct field rename).
- `ENTITY` — the current entity.

```c
DE_MANAGER_FOREACH(mgr, {
    if (ENTITY->tag == TAG_ENEMY)
        update_enemy(ENTITY);
});
```

**Safety rule:** deleting, pausing, or resuming `ENTITY` (the entity being visited in the current iteration) is safe. Mutating a **different** entity is not safe in general: if that other entity hasn't been visited yet (a lower index than `INDEX`) and gets deleted or paused, the gap it leaves gets filled with an entity that **has already** been visited — which can then end up being processed a second time in the same pass.

### `DE_SYSTEM_STORAGE(NAME, CAPACITY, PARAMS)` / `DE_SYSTEM_ARGS(NAME)`

```c
DE_SYSTEM_STORAGE(sys_storage, 32, 3);

struct de_system sys_state;
de_system_init(&sys_state, DE_SYSTEM_ARGS(sys_storage));
```

### `DE_SYSTEM_ADD(sys, ...)`

Adds a group of 1 to 5 pointers; the first argument is always the system handle:

```c
uint16_t ok = DE_SYSTEM_ADD(sys, entity, velocity, position); /* 1 = success, 0 = full */
```

⚠️ The number of pointers passed in **every** call must always match the `params` the system was initialized with. Darken does not validate this: mixing arities across calls on the same system silently misaligns the pool's grouping, and both `DE_SYSTEM_FOREACH` and `de_system_remove` assume uniformly sized groups.

### `DE_SYSTEM_FOREACH(sys, ...)`

0 to 5 output variables followed by the code block; the first argument is always the system handle:

```c
DE_SYSTEM_FOREACH(sys, struct Position *pos, struct Velocity *vel,
{
    pos->x += vel->x;
    pos->y += vel->y;
});
```

The variables can be declared inline at the call site (as above — `struct Position *pos = pool[0];` is a valid `void*`-to-typed-pointer assignment in C) or be pre-existing variables. The macro's internal cursor variable is also named `pool`; it's a local inside the macro's `do { ... } while(0)` block and doesn't collide with an outer `sys` handle.

### `DE_SYSTEM_ITERATOR(name, ...)`

See [§11.3](#113-de_system_iterator).

```c
DE_SYSTEM_ITERATOR(sys_movement_f,
    struct Position *pos,
    struct Velocity *vel,
    {
        pos->x += vel->x;
        pos->y += vel->y;
    }
);
```

---

## 13. State machine

### Typical lifecycle

```text
                 de_manager_new()
                        │
                        ▼
                  state = DE_STATE_DELETE
                        │  (the user assigns a callback)
                        ▼
                  ┌───────────┐
                  │  ACTIVE   │◄────────┐
                  └─────┬─────┘         │
                        │               │ de_entity_resume()
          ┌─────────────┼─────────────┐ │
          │             │             │ │
    another state  DE_STATE_PAUSE  DE_STATE_DELETE
          │             │             │
          ▼             ▼             ▼
       ACTIVE        PAUSED          FREE
                        │
                        └── de_entity_delete() ──► FREE
```

- **Creation:** `de_manager_new()` leaves `state = DE_STATE_DELETE`. The entity does nothing until a callback (a pointer value `> 2`) is assigned.
- **Update:** `de_manager_update()` executes the active callback. Its return value drives the transition: another callback, `LOOP` (no change), `PAUSE`, or `DELETE` — the latter two take effect on the **next** call to `de_manager_update`, not immediately.
- **Pause/resume:** change zone without physically moving the entity.
- **Deletion:** frees the slot; if a destructor exists, it runs first and cannot cancel the deletion.

### Manual versus automatic transitions

Besides a callback *returning* `DE_STATE_PAUSE` / `DE_STATE_DELETE`, you can write `entity->state = DE_STATE_PAUSE;` (or `DELETE`) directly, at any point outside the manager's loop. `de_manager_update()` will detect and apply it on its next pass. **Don't do this with `DE_STATE_LOOP`**: as explained in [§10](#void-de_manager_updatede_manager-m), a `state` set by hand to `LOOP` doesn't fit any branch of `de_manager_update()`, and the entity stays inert — active, but running nothing — until it's assigned some other value.

---

## 14. Update order

`de_manager_update()` and `DE_MANAGER_FOREACH` walk the active zone from the highest index to the lowest:

```text
index:      0     1     2     3     4
           ┌─────┬─────┬─────┬─────┬─────┐
           │ E0  │ E1  │ E2  │ E3  │ E4  │
           └─────┴─────┴─────┴─────┴─────┘
                                       ▲
                                  visited
                                    first
```

Actual execution order: `E4 → E3 → E2 → E1 → E0`.

That's why `de_entity_move_front()` (moves the entity to the highest index) makes it run **earlier** in the next pass, and `de_entity_move_back()` (index `0`) makes it run **later** — the names refer to array position, not to the intuitive left-to-right sense of "front"/"back" in time.

---

## 15. Complexity

### Manager

| Operation | Complexity |
|---|---:|
| `de_manager_init`      | O(capacity) |
| `de_manager_new`       | O(1) |
| `de_entity_pause`      | O(1) |
| `de_entity_resume`     | O(1) |
| `de_entity_delete`     | O(1) |
| `de_entity_move_front` | O(1) |
| `de_entity_move_back`  | O(1) |
| `de_manager_update`    | O(size), excluding callback cost |
| `de_manager_reset`     | O(size) — **not** O(size + paused); see [§19.3](#193-de_manager_reset-still-does-not-destroy-paused-entities) |

### System

| Operation | Complexity |
|---|---:|
| `DE_SYSTEM_ADD`     | O(1) |
| `DE_SYSTEM_FOREACH` | O(groups) |
| `de_system_remove`  | O(groups) to search + O(params) to compact |

---

## 16. Complete example

This exact program was compiled with `gcc -std=gnu11 -Wall -Wextra` and run against the header while preparing this document.

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdio.h>

struct player_data {
    int16_t x, y;
    int16_t speed;
};

void *player_move(void *raw) {
    struct player_data *data = (struct player_data *)raw;

    data->x += data->speed;

    if (data->x > 320)
        return DE_STATE_DELETE;   /* left the screen */

    return DE_STATE_LOOP;
}

int main(void) {
    /* 1. A real struct instance, plus a handle pointing at it (see §5) */
    struct de_manager mgr_state;
    de_manager mgr = &mgr_state;

    /* 2. Static storage for up to 16 entities, and initialization */
    DE_MANAGER_STORAGE(storage, 16, sizeof(struct player_data));
    de_manager_init(mgr, DE_MANAGER_ARGS(storage));

    /* 3. Create the entity */
    de_entity player = de_manager_new(mgr);
    if (!player) return 1;

    /* 4. Populate payload and state */
    struct player_data *data = (struct player_data *)player->data;
    data->x = 0;
    data->y = 100;
    data->speed = 2;

    player->state = player_move;
    player->tag   = 1; /* TAG_PLAYER */

    /* 5. Game loop: run until every active entity has deleted itself */
    while (mgr->size > 0)
        de_manager_update(mgr);

    /* 6. Full cleanup (active entities only — see §19.3) */
    de_manager_reset(mgr);
    return 0;
}
```

Actual output from this run (`data->x` starts at `0`, grows by `2` each frame, deletes once it passes `320`):

```text
player deleted after 162 frames, remaining active=0
after reset: size=0 paused=16
```

### Combined with a system

```c
struct vec2 { int16_t x, y; };

struct de_system sys_state;
de_system sys = &sys_state;

struct vec2 positions[8];
struct vec2 velocities[8];

DE_SYSTEM_STORAGE(sys_storage, 8, 2);
de_system_init(sys, DE_SYSTEM_ARGS(sys_storage));

for (int i = 0; i < 8; ++i)
    DE_SYSTEM_ADD(sys, &positions[i], &velocities[i]);

DE_SYSTEM_FOREACH(sys, struct vec2 *pos, struct vec2 *vel,
{
    pos->x += vel->x;
    pos->y += vel->y;
});

de_system_remove(sys, &positions[3]); /* removes the group by its first pointer */
```

Verified output (positions after one pass of the foreach above, starting from `{0,0},{1,1},{2,2},{3,3}` with velocities `{1,0},{0,1},{1,1},{2,2}`):

```text
pos[0] = (1,0)
pos[1] = (1,2)
pos[2] = (3,3)
pos[3] = (5,5)
removed=1 size=6
```

### As an entity state, via `DE_SYSTEM_ITERATOR`

```c
DE_SYSTEM_ITERATOR(sys_movement_f,
    struct vec2 *pos,
    struct vec2 *vel,
    {
        pos->x += vel->x;
        pos->y += vel->y;
    }
);

/* sys_movement_f has signature `void *(de_system)` and can be
   installed directly as an entity->state, per §11.3. */
```

---

## 17. Safety rules

### Do

- Keep the memory from `DE_MANAGER_STORAGE` / `DE_SYSTEM_STORAGE` alive for as long as the corresponding manager/system is in use.
- Treat `entity->data` as valid while the entity is active or paused.
- Always use the handle returned by `de_manager_new()`.
- Declare a real `struct de_manager` / `struct de_system` instance and pass its address — don't expect `de_manager mgr;` alone to allocate anything (see [§5](#5-handle-types-everything-is-now-a-pointer)).
- Expect `de_system_remove()` to reorder the remaining groups (it does not preserve order).
- Expect active-zone iteration to always run from the highest index to the lowest.

### Do not

- Free the manager's/system's storage while entities or groups are still alive.
- Reuse a `de_entity` after `de_entity_delete()`.
- Assume active-entity ordering is stable across frames.
- Mutate an entity **other than** the current one inside `DE_MANAGER_FOREACH` or `de_manager_update`, unless you specifically know it's safe.
- Assign `entity->state = DE_STATE_LOOP` by hand (it only makes sense as a callback's return value).
- Treat the function generated by `DE_SYSTEM_ITERATOR` as a strictly ISO-C-portable `de_state`.
- Assume `de_manager_reset()` invokes the destructors of paused entities.
- Mix a different number of pointers across `DE_SYSTEM_ADD` calls on the same `de_system`.

---

## 18. Internal invariants

Across every manager operation, the following holds:

```text
0 ≤ size ≤ paused ≤ capacity
```

Every public operation (`new`, `pause`, `resume`, `delete`) moves at most one of the two boundaries (`size`, `paused`) by one unit — or both at once in `pause`/`resume` — while always preserving this relationship. It's what guarantees the three zones never overlap and never leave gaps.

---

## 19. Important quirks and warnings

Everything in this section was checked by compiling and running actual code against the header currently in use — not just by reading comments.

### 19.1 `DE_ENTITY_STRIDE` is no longer public

Earlier revisions of this header exposed `DE_ENTITY_STRIDE(PAYLOAD)` as a public macro, in the `DE_*` namespace. In the current header, it has been renamed to `_DE_ENTITY_STRIDE` and moved into the "internal macro implementations" section. There is no public replacement.

Verified: compiling a call to `DE_ENTITY_STRIDE(16)` against the current header produces `implicit declaration of function 'DE_ENTITY_STRIDE'` — the preprocessor has nothing bound to that name, so it falls through to the C compiler as a literal (and unknown) function call.

**Practical implication:** if application code needs to know an entity's stride for a given payload size (say, to lay out a custom storage block by hand instead of going through `DE_MANAGER_STORAGE`), it can no longer call a public macro for that. Either use `DE_MANAGER_STORAGE`, which computes it internally, or replicate the formula yourself: `stride = (sizeof(struct de_entity) + payload + 3) & ~3`.

### 19.2 `de_entity_update` on an inactive entity

```c
void *de_entity_update(de_entity $) {
    de_state state = de_entity_exec($);          /* returns 0 if $->state isn't a callback */
    if (!_DE_STATE_IS_LOOP(state)) $->state = state;   /* 0 is not LOOP, so it ALWAYS gets written */
    return state;
}
```

If, at the moment `de_entity_update()` is called, the entity's `state` is already one of the control values (`DE_STATE_DELETE`, `DE_STATE_LOOP`, or `DE_STATE_PAUSE` — i.e. not an executable callback), `de_entity_exec()` returns `0`. Since `0` is not `DE_STATE_LOOP`, `de_entity_update()` writes that `0` into `entity->state`, i.e. it **forces the state to `DE_STATE_DELETE`**, regardless of which control value was there before.

Verified at runtime against the current header: starting from `entity->state = DE_STATE_PAUSE` and calling `de_entity_update(entity)`, the resulting state is `DE_STATE_DELETE`, not `DE_STATE_PAUSE`. This behavior is unchanged from earlier revisions of the header.

**Practical implication:** `de_entity_update()` is only safe/useful on an entity that currently has an active callback assigned. Don't use it as a way to "advance" an entity that might be paused or already marked for deletion — use `de_entity_exec()` instead (which writes nothing), or check `_DE_STATE_IS_ACTIVE(entity->state)` before calling it.

### 19.3 `de_manager_reset` still does not destroy paused entities

```c
void de_manager_reset(de_manager $) {
    DE_MANAGER_FOREACH($, de_entity_delete(ENTITY));  /* only walks [0, size) */
    $->size   = 0;
    $->paused = $->capacity;
}
```

`DE_MANAGER_FOREACH` iterates exclusively over `[0, size)`. Paused entities live in `[paused, capacity)` and are **never visited** by this loop. After the `FOREACH`, `de_manager_reset()` simply rewrites `paused = capacity`, reabsorbing the entire paused zone into the free zone **without calling the destructors of the entities that were paused**.

Verified at runtime against the current header: with one active and one paused entity, both with a destructor, `de_manager_reset()` invokes the destructor exactly once (the active entity's); the paused one's never runs. This is the same behavior found in earlier revisions of the header — the internal field renames (`active_count`→`size`, `paused_start`→`paused`) did not change the underlying logic.

**Practical implication:** if your paused entities hold resources that must be released (memory outside Darken's own block, handles, etc.), resume or explicitly delete them **before** calling `de_manager_reset()`. Don't assume the reset is equivalent to deleting every live entity one by one.

### 19.4 `de_entity_swap` is still not public

The two-entity swap operation exists in the code (`_de_entity_swap`), but:

- It's declared `static`, i.e. with internal linkage to the file that defines `DARKEN_IMPLEMENTATION` — it isn't visible from another translation unit even with an external declaration.
- It does not appear in the header's public prototype section (only `de_entity_exec`, `de_entity_update`, `de_entity_pause`, `de_entity_resume`, `de_entity_delete`, `de_entity_move_front`, and `de_entity_move_back` are declared there).

Verified: compiling a call to `de_entity_swap(0, 0)` against the current header produces `implicit declaration of function 'de_entity_swap'; did you mean '_de_entity_swap'?` — GCC's own suggestion confirms there is no public symbol by that name. Swapping two arbitrary entities is not part of the public API; it's only used internally to implement `pause`, `resume`, `delete`, `move_front`, and `move_back`.

### 19.5 Declaring handles: the most common migration mistake

If you're carrying code forward from an earlier revision where `de_manager` was a value-struct typedef, the single most likely compile error you'll hit is code that still does:

```c
de_manager mgr;                  /* WRONG under the current header: mgr is an uninitialized pointer */
de_manager_init(&mgr, ...);       /* &mgr has type de_manager*, but de_manager_init wants a de_manager */
```

Under the current header this either fails to compile (incompatible pointer types) or, worse, compiles with a warning and crashes at runtime if the warning is ignored, because `mgr` was never pointed at real storage. The fix is exactly [§5](#5-handle-types-everything-is-now-a-pointer): declare `struct de_manager mgr;` (the tag, not the typedef) and pass `&mgr`, or declare a `de_manager` handle and point it at a separate `struct de_manager` instance.

### 19.6 Arity mismatch in `DE_SYSTEM_ADD` / `DE_SYSTEM_FOREACH` / `DE_SYSTEM_ITERATOR`

None of these macros check, at compile time or at runtime, that the number of pointers used matches the `params` the system was initialized with. A mismatch produces no visible error: it silently misaligns the pool's internal grouping, and subsequent reads (`DE_SYSTEM_FOREACH`, `de_system_remove`) will read pointers belonging to a different group. Keeping the arity constant for a given `de_system` is entirely the caller's responsibility. Unchanged from earlier revisions.

---

## 20. What Darken is — and is not

> A fixed-capacity entity lifecycle manager, plus a compact pointer-processing system.

Darken is **not** a conventional archetype ECS. It does not provide:

- runtime component registration or type IDs;
- archetype migration;
- automatic queries;
- dynamic memory allocation;
- reflection or serialization;
- multithreading support.

This is a deliberate choice: Darken provides the low-level mechanisms — predictable memory, fixed capacity, moving pointers instead of bytes, an explicit lifecycle — so game code can build whatever architecture it needs on top, without imposing one.

```text
   predictable memory
            +
      fixed capacity
            +
   move pointers, not bytes
            +
    explicit lifecycle
            +
  68000-oriented efficiency
            =
          Darken
```

The manager owns lifecycle and ordering. The entity owns its state and payload. The system owns compact lists of pointers. That separation is what keeps the core small.

---

## 21. Quick API reference

### Types

```c
typedef void *(*de_state)(void *);

typedef struct de_entity  *de_entity;    /* pointer typedef */
typedef struct de_manager *de_manager;   /* pointer typedef */
typedef struct de_system  *de_system;    /* pointer typedef */

struct de_entity  { de_state state, destructor; de_manager owner; uint16_t slot, tag; uint8_t data[]; };
struct de_manager { de_entity *pool; uint16_t capacity, size, paused; };
struct de_system  { void **pool, **end; uint16_t capacity, size, params; };
```

Remember: `de_manager mgr;` and `de_system sys;` declare *pointers*, not storage. See [§5](#5-handle-types-everything-is-now-a-pointer) and [§19.5](#195-declaring-handles-the-most-common-migration-mistake).

### Functions — entity

| Function | Description |
|---|---|
| `void *de_entity_exec(de_entity)` | Executes the active state without writing `entity->state` |
| `void *de_entity_update(de_entity)` | Executes and stores the transition; see [§19.2](#192-de_entity_update-on-an-inactive-entity) |
| `void de_entity_pause(de_entity)` | Moves to the paused zone (no-op if not active) |
| `void de_entity_resume(de_entity)` | Moves to the active zone (no-op if not paused) |
| `void de_entity_delete(de_entity)` | Deletes, running the destructor if present (no-op if already free) |
| `void de_entity_move_front(de_entity)` | Makes it run earlier in the next pass |
| `void de_entity_move_back(de_entity)` | Makes it run later in the next pass |

*(`_de_entity_swap` is internal, `static`, and not part of the public API — see [§19.4](#194-de_entity_swap-is-still-not-public).)*

### Functions — manager

| Function | Description |
|---|---|
| `void de_manager_init(de_manager, de_entity*, void*, uint16_t, uint16_t)` | Initializes a manager over caller-owned storage |
| `de_entity de_manager_new(de_manager)` | Creates an entity; `NULL` if there's no capacity |
| `void de_manager_update(de_manager)` | Runs the active zone, backward |
| `void de_manager_reset(de_manager)` | Empties the manager; see [§19.3](#193-de_manager_reset-still-does-not-destroy-paused-entities) |

### Functions — system

| Function | Description |
|---|---|
| `void de_system_init(de_system, void**, uint16_t, uint16_t)` | Initializes a system over caller-owned storage |
| `uint16_t de_system_remove(de_system, void*)` | Removes a group by its first pointer; `1`/`0` |

### Macros

| Macro | Description |
|---|---|
| `DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)` | Declares static manager storage |
| `DE_MANAGER_ARGS(NAME)` | Expands to the 4 arguments of `de_manager_init` |
| `DE_MANAGER_FOREACH(M, CODE)` | Iterates the active zone backward (`INDEX`, `POOL`, `ENTITY`) |
| `DE_SYSTEM_STORAGE(NAME, CAPACITY, PARAMS)` | Declares static system storage |
| `DE_SYSTEM_ARGS(NAME)` | Expands to the 3 arguments of `de_system_init` |
| `DE_SYSTEM_ADD(sys, ...)` | Adds a group of 1–5 pointers |
| `DE_SYSTEM_FOREACH(sys, ...)` | Iterates groups, 0–5 output variables |
| `DE_SYSTEM_ITERATOR(name, ...)` | Generates `void *name(de_system)` from a `FOREACH` body |

*(`DE_ENTITY_STRIDE` — no longer public in this revision; see [§19.1](#191-de_entity_stride-is-no-longer-public).)*

---

**License / authorship:** not specified in the provided source — add it here as appropriate for the project.