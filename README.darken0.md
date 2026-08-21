# Darken (DARKula ENgine) 2.0 — Entity System

`darken.h` is a **single-header library** (stb-style, declarations and implementation in the same file, implementation enabled via `DARKEN_IMPLEMENTATION`) that implements an **entity system** for games written in C.

Key design points:

- **No dynamic memory**: all memory (the pointer pool + the entities' data) is provided by the caller, typically statically, via the `DE_MANAGER_STORAGE` macro.
- **Fixed capacity**: each `de_manager` manages a maximum number of entities defined up front.
- **Stable addresses**: an entity's memory address never moves once allocated. Only the array of pointers (`manager->pool[]`) gets reordered. This is why it's safe to keep a raw pointer into `entity->data` even while the entity is paused, resumed, or reordered.
- Designed for resource-constrained targets (the code's own comments mention GCC + Motorola 68000): 4-byte alignment and 16-bit fields.
- Uses GNU C extensions (`__attribute__`), so it requires GCC/Clang — not strict standard C.

## Core concept: Entity + Manager

- **Entity (`de_entity`)**: a container for user data (`data[]`, variable size) plus a "state machine" built from function pointers.
- **Manager (`de_manager`)**: an array (`pool`) of pointers to entities, divided into three contiguous zones that get reordered by swapping pointers (never by moving memory):

```
[  active  ][   free   ][  paused  ]
0           size        paused      capacity
```

| Zone | Range | Description |
|---|---|---|
| **Active** | `[0, size)` | Updated every frame (`de_manager_update`) and iterated with `DE_MANAGER_FOREACH`. Freely created/deleted. |
| **Free** | `[size, paused)` | Slots with no entity assigned. This is where `de_manager_new()` pulls the next available entity from. |
| **Paused** | `[paused, capacity)` | Outside the update loop. `de_manager_new()` never reuses these slots, so their `data` pointers stay valid and untouched until explicitly resumed or deleted. |

## An entity's state machine

```c
typedef void *(*de_state)(void *);
```

A "state" is a function that receives `entity->data` and returns either **another state function** (the entity stays active and that becomes its next state), or one of these **control values**:

| Value | Meaning |
|---|---|
| `DE_STATE_DELETE` (`0`) | Requests the entity be deleted. |
| `DE_STATE_LOOP` (`1`) | "Stay as you are": doesn't overwrite `entity->state`, so the function doesn't need to return itself. |
| `DE_STATE_PAUSE` (`2`) | Requests the entity be paused. |

`_DE_STATE_IS_ACTIVE` simply checks that the pointer is `> 2`, i.e. a real function address (a pointer-tagging trick: no valid function lives at addresses 0, 1, or 2).

⚠️ **Important — one-frame delay**: when a state function returns `PAUSE` or `DELETE`, the manager only stores that value in `entity->state` that frame. The entity is physically moved out of its zone (actually paused/deleted) on the **next** call to `de_manager_update`, once that control value is detected.

## Typical entity lifecycle

1. `de_manager_new(mgr)` → the entity enters the active zone with `state = DE_STATE_DELETE` (the default).
2. The caller **must** assign `entity->state` (and optionally `destructor`, `tag`, and fill in `data`) before the next call to `de_manager_update`.
3. If it doesn't, since the state isn't active, this is interpreted as a delete request and the entity **self-deletes** on the next update. It's a safety net against half-initialized entities.
4. Every `de_manager_update` runs each active entity's state function and applies transitions (new state / pause / delete).
5. An entity can also be paused, resumed, or deleted directly (without going through a state function's return value) by calling `de_entity_pause`, `de_entity_resume`, or `de_entity_delete`; these take effect **immediately**, with no one-frame delay.

## Structures

```c
struct de_entity {
    de_state  state;      // current state/behavior (or a control value)
    de_state  destructor; // optional callback on deletion
    de_manager owner;     // owning manager
    uint16_t  slot;       // current index in owner->pool[]
    uint16_t  tag;        // free-form field for the caller to classify entity type
    uint8_t   data[];     // user payload, variable size
};

struct de_manager {
    de_entity *pool;      // array of pointers (what actually gets reordered)
    uint16_t   capacity;  // total pool size
    uint16_t   size;      // number of active entities (boundary of the active zone)
    uint16_t   paused;    // boundary between the free zone and the paused zone
};
```

`capacity`/`size`/`paused` are `uint16_t` → up to 65535 entities per manager.

## Query macros

| Macro | Checks |
|---|---|
| `_DE_STATE_IS_DELETED(s)` | `s == DE_STATE_DELETE` |
| `_DE_STATE_IS_LOOP(s)` | `s == DE_STATE_LOOP` |
| `_DE_STATE_IS_PAUSED(s)` | `s == DE_STATE_PAUSE` |
| `_DE_STATE_IS_ACTIVE(s)` | `s` is a real function pointer |
| `_DE_ENTITY_IS_ACTIVE(e)` | `e` is in the active zone |
| `_DE_ENTITY_IS_PAUSED(e)` | `e` is in the paused zone |
| `_DE_ENTITY_IS_FREE(e)` | `e` is neither active nor paused (a free slot) |

## Declaration macros (user-facing API)

| Macro | Purpose |
|---|---|
| `DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)` | Declares a variable `NAME` with **all storage embedded**: a `pool[CAPACITY]` array plus a byte buffer for the entities' data (4-byte aligned), no `malloc`. This is the recommended way to allocate a manager. |
| `DE_MANAGER_ARGS(NAME)` | Expands to the 4 arguments (`pool, data, capacity, payload_size`) that `de_manager_init` needs, taken from a variable created with `DE_MANAGER_STORAGE`. Typical use: `de_manager_init(&mgr, DE_MANAGER_ARGS(storage));` |
| `DE_MANAGER_FOREACH(manager, CODE)` | Iterates the **active** entities (back to front, safe against deletions performed inside `CODE`). Inside `CODE`, the variable `ENTITY` is available. |

## Functions — Entity

| Function | What it does |
|---|---|
| `de_entity_exec(e)` | Runs `e`'s current state (passing it `e->data`) and returns the result, **without** applying the transition. Returns `0` if `e` isn't active. |
| `de_entity_update(e)` | Like `de_entity_exec`, but also updates `e->state` with the result (unless it's `LOOP`). Meant for manually "ticking" a single entity outside the normal update loop; should only be called on active entities. |
| `de_entity_pause(e)` | Moves `e` from the active zone to the paused zone, immediately. No-op (returns `0`) if `e` wasn't active. |
| `de_entity_resume(e)` | Moves `e` from the paused zone back to the active zone, immediately. No-op if `e` wasn't paused. |
| `de_entity_delete(e)` | Calls `destructor` (if it's an active pointer) and frees `e`'s slot (works whether `e` was active or paused). No-op if `e` was already free. |
| `de_entity_move_front(e)` | Reorders `e` within the active zone to the slot processed **first** by `de_manager_update`/`DE_MANAGER_FOREACH`. Useful for controlling execution/draw order. |
| `de_entity_move_back(e)` | Reorders `e` within the active zone to the slot processed **last**. |

## Functions — Manager

| Function | What it does |
|---|---|
| `de_manager_init(mgr, pool, storage, capacity, bytes)` | Initializes the manager: slices `storage` into aligned chunks of `header + bytes` and links them into `pool[]`. Afterward, all entities start out free (`size = 0`, `paused = capacity`). Typically called as `de_manager_init(&mgr, DE_MANAGER_ARGS(storage_var));`. |
| `de_manager_new(mgr)` | Claims an entity from the free zone and moves it into the active zone with `state = DE_STATE_DELETE`, `destructor = 0`, `tag = 0`. Returns `NULL` if no free slots remain. **The caller must set `state` (and optionally `data`/`destructor`/`tag`) before the next `de_manager_update`.** |
| `de_manager_update(mgr)` | The main "tick": runs each active entity's state, applies whatever new state it returns, and processes any pause/delete requests pending from the previous call. Call this once per frame. |
| `de_manager_reset(mgr)` | Deletes (destructors included) **all active entities** and empties the manager (`size = 0`, `paused = capacity`). |

## Notes and gotchas

- **Delete-by-default**: if you don't assign an active state after `de_manager_new` before the next `de_manager_update`, the entity deletes itself.
- **One-frame delay** for pauses/deletes requested *from inside* a state function (via `PAUSE`/`DELETE` return values). If you need it to take effect right away, call `de_entity_pause`/`de_entity_delete` directly.
- **`de_manager_reset` does not call the destructor of paused entities** (it only walks the active zone via `DE_MANAGER_FOREACH`); if you have paused entities holding resources, resume them before resetting.
- Pointers into `entity->data` stay stable as long as the entity exists (active or paused); they stop being valid once the entity is deleted, since its slot may get reused.
- The size of the free zone doesn't change with `pause`/`resume` (only slots move between the active and paused zones); it does change with `new`/`delete`.
- Requires compiling with GCC/Clang because of `__attribute__((aligned(4)))`.
- To pull in the implementation, `#define DARKEN_IMPLEMENTATION` before a single `#include "darken.h"` in one `.c` file of your project.