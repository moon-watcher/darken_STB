# Darken

**Darken (DARKula ENgine) 2.0** is a small, fixed-capacity entity and data-system library written in C for **GCC/SGDK and the Motorola 68000**.

Darken is deliberately not a traditional heap-based ECS. Its manager uses caller-owned, contiguous storage and keeps a separate pointer array divided into three zones:

```text
manager->items[]
┌──────────────────────┬──────────────────────┬──────────────────────┐
│       ACTIVE         │         FREE         │        PAUSED        │
│ [0, active_count)    │ [active_count,       │ [paused_start,       │
│                      │  paused_start)       │  capacity)            │
└──────────────────────┴──────────────────────┴──────────────────────┘
                         ↑                    ↑
                    active_count         paused_start
```

The entity objects themselves are stored in a contiguous byte block. Moving an entity between zones moves only its pointer in `items[]`; the entity's physical address never changes.

That property is particularly useful when another subsystem keeps a raw pointer to `entity->data`. It is one of the core invariants of Darken: **manager reordering never relocates the entity object itself**.

The manager therefore has two distinct kinds of position:

1. the entity's **physical address** inside the caller-provided storage block; and
2. its **logical position** in `manager->items[]`, recorded by `entity->slot`.

Only the second one changes during creation, deletion, pause/resume, or reordering.

---

## Features

- Fixed-capacity entity manager.
- No allocation performed by Darken.
- Caller-owned storage.
- Contiguous entity memory.
- Precomputed entity pointers and fixed entity stride.
- Active/free/paused zones in one pointer array.
- O(1) entity creation when capacity is available.
- O(1) pause/resume/delete/swap operations.
- Backward active traversal.
- Explicit state-machine callbacks.
- Optional per-entity destructor.
- User-defined 16-bit entity tags.
- Separate packed data-pointer system (`de_system`).
- 1–5 parameter convenience macros for systems.
- Static storage declaration macros.
- GNU C statement expressions and `__attribute__` support for SGDK/GCC.
- No `malloc`, `free`, `calloc` or hidden heap allocation.

---

# 1. Requirements and portability

Darken is designed around:

- GCC/SGDK.
- Motorola 68000 / Mega Drive development.
- GNU C extensions.

The header uses:

- `__attribute__((aligned(4)))`
- GNU statement expressions: `({ ... })`

Therefore Darken is **not intended to be strictly ISO C portable**.

The `de_state` interface also represents state callback pointers and special control values through `void *`. This is intentional for the target environment and should be treated as a compiler/ABI-specific interface.

---

# 2. Single-header usage

Darken follows the STB-style single-header pattern.

In exactly one `.c` file:

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
```

In other source files:

```c
#include "darken.h"
```

Do not define `DARKEN_IMPLEMENTATION` in more than one translation unit.

---

# 3. Core concepts

Darken has two main pieces:

```text
                 ┌──────────────────┐
                 │   de_manager     │
                 │ entity lifecycle │
                 └────────┬─────────┘
                          │
             ┌────────────┴────────────┐
             │                         │
       entity pointers            entity storage
       manager->items[]           caller-owned bytes
             │                         │
             ▼                         ▼
       ┌───────────────┐       ┌──────────────────┐
       │ active/free/  │       │ entity 0         │
       │ paused zones  │       │ entity 1         │
       └───────────────┘       │ entity 2 ...     │
                               └──────────────────┘

                 ┌──────────────────┐
                 │    de_system     │
                 │ packed pointers  │
                 └──────────────────┘
```

An entity contains lifecycle state and a flexible payload:

```c
struct de_entity
{
    de_state state;
    de_state destructor;
    de_manager *manager;
    uint16_t slot;
    uint16_t tag;
    uint8_t data[];
};
```

`data[]` is the application-specific payload.

---

# 4. Entity memory layout

For a payload of `PAYLOAD` bytes, the stride is:

```c
DE_ENTITY_STRIDE(PAYLOAD)
```

which is equivalent to:

```text
align4(sizeof(struct de_entity) + PAYLOAD)
```

The storage therefore looks like:

```text
storage
   │
   ▼
┌──────────────────────────────┐
│ entity 0                     │
│ state                        │
│ destructor                   │
│ manager                      │
│ slot                         │
│ tag                          │
│ data[PAYLOAD]                │
│ padding                      │
├──────────────────────────────┤
│ entity 1                     │
│ ...                          │
├──────────────────────────────┤
│ entity 2                     │
│ ...                          │
└──────────────────────────────┘
```

The stride is calculated once during `de_manager_init()` and reused to build the pointer table. Darken aligns this stride to a 4-byte boundary with `_DE_ALIGN4()`. The alignment is an implementation detail of the target-oriented storage layout: it keeps each entity start at a predictable boundary and avoids creating irregular entity strides.

On the Motorola 68000, word alignment is the important minimum requirement for word/long accesses. The 4-byte stride alignment is deliberately stronger and is used by Darken to keep entity boundaries regular.

---

# 5. Manager storage

The easiest way to reserve manager storage is:

```c
DE_MANAGER_STORAGE(g_storage, 64, sizeof(MyComponent));
```

Then initialize the manager:

```c
de_manager g_manager;

de_manager_init(
    &g_manager,
    DE_MANAGER_ARGS(g_storage)
);
```

`DE_MANAGER_STORAGE()` creates:

- an entity pointer array;
- a contiguous entity byte-storage array;
- the capacity value;
- the payload size value.

Conceptually:

```text
g_storage
├── entities[64]
├── data[64 * aligned_entity_stride]
├── capacity
└── payload_size
```

The entity data block is aligned to 4 bytes.

---

# 5.1 Manager zones and invariants

`manager->items[]` is maintained as three contiguous logical zones:

```text
index
  0                         active_count              paused_start          capacity
  │                              │                         │                    │
  ▼                              ▼                         ▼                    ▼
  ┌──────────────────────────────┬─────────────────────────┬────────────────────┐
  │            ACTIVE            │          FREE           │       PAUSED       │
  │  [0, active_count)           │ [active_count,          │ [paused_start,     │
  │                              │  paused_start)          │  capacity)         │
  └──────────────────────────────┴─────────────────────────┴────────────────────┘
```

### Active zone

The active zone contains entities processed by `de_manager_update()`. `DE_MANAGER_FOREACH` also visits only this zone, in descending index order.

New entities are taken from the first free slot and inserted at the active/free boundary.

### Free zone

The free zone contains pointer slots that are currently available to `de_manager_new()`. Its range is `[active_count, paused_start)`.

An important consequence is that **paused entities are never recycled as free slots**. This is what preserves the physical address of a paused entity and therefore the validity of pointers into its payload.

### Paused zone

The paused zone contains live entities that are deliberately excluded from normal updates. Its range is `[paused_start, capacity)`.

`de_manager_update()` does not execute paused entities, and `DE_MANAGER_FOREACH` does not visit them. `de_manager_new()` never allocates from this zone.

### Why `slot` exists

`entity->slot` is the entity's current index in `manager->items[]`. It is **not** an offset into the entity byte-storage block. When two entities are swapped, their physical memory stays where it was and only their `slot` values and pointer-array positions change.

This distinction is what allows Darken to reorder entities without invalidating `entity->data` pointers.

All manager counters are `uint16_t`, matching the fixed-capacity, 68000-oriented design.

---

# 6. Creating an entity

Create an entity with:

```c
de_entity e = de_manager_new(&g_manager);
```

If the manager is full:

```c
e == NULL
```

A newly created entity starts with:

```c
e->state      = DE_STATE_DELETE;
e->destructor = NULL;
e->tag        = 0;
```

The payload is **not initialized**.

For example:

```c
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
} PlayerData;

de_entity player = de_manager_new(&g_manager);

if (player)
{
    PlayerData *p = (PlayerData *)player->data;

    p->x = 100;
    p->y = 80;
    p->vx = 1;
    p->vy = 0;
}
```

If zeroed data is required, initialize it explicitly.

---

# 7. Entity state machine

A state callback has this form:

```c
void *state(void *data);
```

and is represented by:

```c
typedef void *(*de_state)(void *);
```

A state normally returns another state callback:

```text
┌──────────────┐
│ current state│
└──────┬───────┘
       │
       ▼
   callback(data)
       │
       ├───────────────► another state
       │
       ├───────────────► DE_STATE_LOOP
       │
       ├───────────────► DE_STATE_PAUSE
       │
       └───────────────► DE_STATE_DELETE
```

Example:

```c
static void *player_update(void *data)
{
    PlayerData *p = data;

    p->x += p->vx;

    return player_update;
}
```

Assign it:

```c
player->state = player_update;
```

Then:

```c
de_manager_update(&g_manager);
```

will execute it.

A state callback receives only `entity->data`; it does not receive the `de_entity *` itself. The callback's return value becomes the entity's next state unless it returns `DE_STATE_LOOP`. `DE_STATE_LOOP` explicitly means **keep the current state function**.

`DE_STATE_PAUSE` and `DE_STATE_DELETE` are not performed immediately by the state callback. They are stored as the entity's state and are acted upon by a subsequent `de_manager_update()`. This separation is important because the manager is traversing its active pointer array while the callback is running.

If an entity has no active state, `de_entity_exec()` does not call anything and returns `DE_STATE_DELETE` (which is `NULL`); `de_entity_update()` instead preserves and returns the entity's current non-active state. In normal manager operation, a newly created entity starts with `DE_STATE_DELETE`, so it must be assigned an active state before it can update.

---

# 8. State control values

Darken defines three special values:

```c
DE_STATE_DELETE
DE_STATE_LOOP
DE_STATE_PAUSE
```

Their meaning is:

| Value | Meaning |
|---|---|
| `DE_STATE_DELETE` | Request deletion |
| `DE_STATE_LOOP` | Keep the current state |
| `DE_STATE_PAUSE` | Request pausing |

A normal state callback returns another `de_state` function.

Example:

```c
static void *alive_state(void *data)
{
    PlayerData *p = data;

    p->x += p->vx;

    if (p->x < 0)
        return DE_STATE_DELETE;

    return DE_STATE_LOOP;
}
```

`DE_STATE_LOOP` does not replace `entity->state`.

A returned `DE_STATE_PAUSE` or `DE_STATE_DELETE` is stored as the entity's pending control state and is processed on the **next** `de_manager_update()`.

---

# 9. Manager update

The manager updates active entities backwards:

```text
active zone

index:     0     1     2     3     4
          ┌─────┬─────┬─────┬─────┬─────┐
          │ E0  │ E1  │ E2  │ E3  │ E4  │
          └─────┴─────┴─────┴─────┴─────┘
                                      ▲
                                   first
                                  visited
```

The order is:

```text
E4 → E3 → E2 → E1 → E0
```

This is important when using:

```c
de_entity_move_front()
de_entity_move_back()
```

The manager's update loop is designed so that pausing or deleting an entity does not require re-reading `active_count` during the traversal.

The traversal starts with a snapshot of the active-zone size:

```c
uint16_t i = manager->active_count;
while (i--)
{
    /* process items[i] */
}
```

This is deliberate. Both `de_entity_pause()` and `de_entity_delete()` shrink the active zone from its right edge. During a backwards traversal, that right edge consists of indices that have already been visited. Consequently, an entity moved out of the active zone cannot cause the loop to revisit an unprocessed entity or require a live `active_count` reload.

The state-processing sequence is:

```text
active entity
      │
      ▼
 execute state(data)
      │
      ├── DE_STATE_LOOP   → keep current state
      ├── another state   → store new state
      ├── DE_STATE_PAUSE  → store pause request
      └── DE_STATE_DELETE → store deletion request
                                │
                                ▼
                       next manager update
                                │
                         pause/delete
```

---

# 10. Update ordering controls

## `de_entity_move_front()`

Moves an active entity to the highest active index.

Because iteration is backwards, this causes the entity to run earlier in the next traversal.

```text
Before:
[E0 E1 E2 E3]

move_front(E1):

[E0 E2 E3 E1]
             ↑
          runs first
```

## `de_entity_move_back()`

Moves an active entity to index 0.

Because iteration is backwards, this causes the entity to run later in the next traversal.

```text
Before:
[E0 E1 E2 E3]

move_back(E3):

[E3 E0 E1 E2]
 ↑
runs last
```

These operations swap pointers only.

---

# 11. Pausing and resuming

Pausing does not destroy the entity.

The pointer is moved from the active zone into the paused zone:

```text
Before:

[ ACTIVE ][ FREE ][ PAUSED ]

             ↓ pause

[ ACTIVE ][ FREE ][ PAUSED ]
                          ↑
                    entity pointer
```

More precisely, the three boundaries change while the entity pointer is moved.

The entity's actual memory address remains unchanged.

That means this is safe:

```c
MyComponent *component = (MyComponent *)entity->data;

de_entity_pause(entity);

/* component still points to the same entity payload */
```

A paused entity:

- is not updated;
- is not visited by `DE_MANAGER_FOREACH`;
- is not reused by `de_manager_new`;
- retains its storage address.

Resume it with:

```c
de_entity_resume(entity);
```

Pause and resume are constant-time pointer-array operations. Pausing shrinks the active zone from the right, fills the entity's old active slot with the entity that occupied the active-zone edge, then grows the paused zone from the left and places the paused entity at the new paused boundary. Resume performs the inverse movement: it removes the entity from the paused boundary, advances `paused_start`, and inserts the resumed entity at the active/free boundary.

The entity's byte storage is never copied. Only `manager->items[]` and the affected entities' `slot` fields change.

---

# 12. Why the three-zone design matters

The manager does not compact entity objects themselves.

Instead:

```text
entity storage
┌───────┬───────┬───────┬───────┐
│  E0   │  E1   │  E2   │  E3   │
└───────┴───────┴───────┴───────┘
  ^       ^       ^       ^
  │       │       │       │
items[] pointers can change
```

This distinction is important.

When an entity changes zone:

```text
manager->items[]
```

changes, but:

```text
entity
entity->data
```

does not move.

Therefore external pointers into entity payloads remain valid across pause/resume/reordering.

---

# 13. Deletion

Delete explicitly:

```c
de_entity_delete(entity);
```

or return:

```c
return DE_STATE_DELETE;
```

from the entity's state.

If a destructor is present:

```c
entity->destructor = my_destructor;
```

it is called before the entity is removed.

The destructor's return value is ignored. It cannot cancel deletion.

Deletion first checks whether the entity is already in the free zone. If it is live, its destructor is called and the entity is then removed from whichever live zone contains it. The implementation swaps it with the element adjacent to the free zone and expands the free zone by one slot.

For an active entity, the active zone shrinks from the right. For a paused entity, the paused zone shrinks from the left. In both cases the operation is O(1) and the entity's physical storage is not moved.

After deletion, the old `de_entity *` must be treated as invalid for normal use: its storage may be handed to `de_manager_new()` again.

Example:

```c
static void *enemy_destroy(void *data)
{
    EnemyData *enemy = data;

    release_enemy_resources(enemy);

    return DE_STATE_DELETE;
}
```

The returned value is irrelevant to the deletion operation; returning `DE_STATE_DELETE` is merely conventional if the function is shared with `de_state` code.

---

# 14. Tags

Every entity has a 16-bit user-defined tag:

```c
entity->tag
```

Example:

```c
#define TAG_PLAYER  1
#define TAG_ENEMY   2
#define TAG_BULLET  3

entity->tag = TAG_PLAYER;
```

Tags are completely application-defined.

Darken does not assign semantics to them.

---

# 15. Iterating entities

Use:

```c
DE_MANAGER_FOREACH(&g_manager,
{
    if (ENTITY->tag == TAG_ENEMY)
        update_enemy(ENTITY);
});
```

Inside the block:

- `INDEX` is the current manager index.
- `ITEMS` is the manager's pointer array.
- `ENTITY` is the current entity.

Iteration is backwards and covers only the active zone.

The current entity may safely be:

- deleted;
- paused;
- moved.

Mutating a different entity while the loop is running is not guaranteed safe because it can change the pointer array being traversed.

---

# 16. Systems

`de_system` is a separate, very small packed pointer pool.

It is useful when a processing step needs to maintain a list of related pointers without allocating memory.

A system with:

```text
capacity_groups = 32
params          = 3
```

contains space for:

```text
32 groups × 3 pointers = 96 pointer slots
```

The logical layout is:

```text
group 0: [A0][B0][C0]
group 1: [A1][B1][C1]
group 2: [A2][B2][C2]
...
```

Internally:

```text
pool[]
┌────┬────┬────┬────┬────┬────┬────┬────┬────┐
│ A0 │ B0 │ C0 │ A1 │ B1 │ C1 │ A2 │ B2 │ C2 │ ...
└────┴────┴────┴────┴────┴────┴────┴────┴────┘
```

`size` and `capacity` are measured in pointer slots, while `params` defines the group width. Thus, if `params == 3`, a system containing four groups has `size == 12`, not `size == 4`.

`end` points one element past the last used pointer. `DE_SYSTEM_ADD` writes new pointers at `end`, then advances both `size` and `end` by the number of pointers added. The pool therefore remains densely packed from `pool` through `end`.

The system does not own the pointed-to objects. It stores only pointers. The lifetime of the entities/components represented by those pointers remains the caller's responsibility.

---

# 17. System storage

Declare static system storage:

```c
DE_SYSTEM_STORAGE(g_physics_storage, 32, 3);
```

Initialize:

```c
de_system physics;

de_system_init(
    &physics,
    DE_SYSTEM_ARGS(g_physics_storage)
);
```

This reserves:

```text
32 groups
× 3 pointers
= 96 pointers
```

No heap allocation is performed.

---

# 18. Adding system groups

Use:

```c
DE_SYSTEM_ADD(&physics, entity, velocity, position);
```

The first argument is the system pointer.

The remaining arguments are the pointers stored in one group.

Up to five data pointers are supported:

```c
DE_SYSTEM_ADD(&system, A);
DE_SYSTEM_ADD(&system, A, B);
DE_SYSTEM_ADD(&system, A, B, C);
DE_SYSTEM_ADD(&system, A, B, C, D);
DE_SYSTEM_ADD(&system, A, B, C, D, E);
```

The macro returns:

```text
1 = success
0 = system full
```

---

# 19. Iterating systems

Example:

```c
DE_SYSTEM_FOREACH(&physics, entity, velocity, position,
{
    update_physics(entity, velocity, position);
});
```

The macro walks one complete group at a time.

For a 3-parameter system:

```text
items
  │
  ▼
[A0 B0 C0] → [A1 B1 C1] → [A2 B2 C2] → ...
   group 0      group 1      group 2
```

The variables supplied to the macro are assigned from the current group's pointers.

---

# 20. Removing system groups

Remove a group by matching its first pointer:

```c
de_system_remove(&physics, entity);
```

The function returns:

```text
1 = removed
0 = not found
```

The system remains packed.

If the removed group is not the last group, the last group is copied into the removed group's position:

```text
Before:

[A][B][C] [D][E][F] [G][H][I]
             ^ remove

After:

[A][B][C] [G][H][I]
```

This is an unordered removal.

Do not depend on stable system-group ordering after removal.

---

# 21. System iterator generator

`DE_SYSTEM_ITERATOR` generates a function that executes a system foreach pattern and returns:

```c
DE_STATE_LOOP
```

Example:

```c
DE_SYSTEM_ITERATOR(
    physics_update,
    entity,
    velocity,
    position,
    {
        update_physics(entity, velocity, position);
    }
);
```

The generated function has the conceptual form:

```c
void *physics_update(de_system *system)
{
    /* foreach body */
    return DE_STATE_LOOP;
}
```

Important: this function takes `de_system *`, while `de_state` takes `void *`. Therefore the generated function pointer is **not type-compatible with `de_state` under strict ISO C**.

On the intended GCC/SGDK target the macro can be useful where the ABI and calling convention are known, but it should not be treated as a portable function-pointer conversion.

---

# 21.1 Macro argument limits and generated code

The convenience macros deliberately support a small fixed number of parameters:

| Macro | Supported data variables/pointers |
|---|---:|
| `DE_SYSTEM_ADD` | 1–5 |
| `DE_SYSTEM_FOREACH` | 0–5 |
| `DE_SYSTEM_ITERATOR` | 0–5 |

The `_DE_*` macros used to count arguments and select implementations are internal details. They exist solely to provide the public variadic API and should not be called directly.

The generated system code is intentionally simple: it walks `pool[]` in groups of `params` pointers and assigns the requested convenience variables from the current group.

---

# 22. Complete small example

```c
#include "darken.h"

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
} Player;

static void *player_update(void *data)
{
    Player *p = data;

    p->x += p->vx;
    p->y += p->vy;

    return DE_STATE_LOOP;
}

static void *enemy_update(void *data)
{
    Player *p = data;

    p->x -= p->vx;

    if (p->x < 0)
        return DE_STATE_DELETE;

    return DE_STATE_LOOP;
}

DE_MANAGER_STORAGE(
    g_entity_storage,
    64,
    sizeof(Player)
);

static de_manager g_manager;

void game_init(void)
{
    de_manager_init(
        &g_manager,
        DE_MANAGER_ARGS(g_entity_storage)
    );

    de_entity player = de_manager_new(&g_manager);

    if (player)
    {
        Player *p = (Player *)player->data;

        p->x = 100;
        p->y = 80;
        p->vx = 1;
        p->vy = 0;

        player->tag = 1;
        player->state = player_update;
    }
}

void game_update(void)
{
    de_manager_update(&g_manager);
}
```

---

# 23. Complete system example

```c
typedef struct
{
    int16_t x;
    int16_t y;
} Position;

typedef struct
{
    int16_t x;
    int16_t y;
} Velocity;

DE_SYSTEM_STORAGE(
    g_movement_storage,
    64,
    3
);

static de_system g_movement;

void movement_init(void)
{
    de_system_init(
        &g_movement,
        DE_SYSTEM_ARGS(g_movement_storage)
    );
}

void movement_add(
    de_entity entity,
    Position *position,
    Velocity *velocity
)
{
    DE_SYSTEM_ADD(
        &g_movement,
        entity,
        position,
        velocity
    );
}

void movement_update(void)
{
    DE_SYSTEM_FOREACH(
        &g_movement,
        entity,
        position,
        velocity,
        {
            (void)entity;

            position->x += velocity->x;
            position->y += velocity->y;
        }
    );
}
```

---

# 24. Entity manager lifecycle

A typical lifecycle is:

```text
                 de_manager_new()
                        │
                        ▼
                  ┌───────────┐
                  │   ACTIVE  │
                  └─────┬─────┘
                        │
             ┌──────────┼──────────┐
             │          │          │
          new state   PAUSE      DELETE
             │          │          │
             ▼          ▼          ▼
          ACTIVE      PAUSED      FREE
             ▲          │
             │          │ resume
             └──────────┘
```

A state callback can therefore act as a lightweight state machine.

---

# 25. Complexity

For the manager:

| Operation | Complexity |
|---|---:|
| `de_manager_new` | O(1) |
| `de_entity_swap` | O(1) |
| `de_entity_pause` | O(1) |
| `de_entity_resume` | O(1) |
| `de_entity_delete` | O(1) |
| `de_entity_move_front` | O(1) |
| `de_entity_move_back` | O(1) |
| `de_manager_update` | O(active entities) |
| `de_manager_reset` | O(active + paused entities) |

For systems:

| Operation | Complexity |
|---|---:|
| `DE_SYSTEM_ADD` | O(1) |
| `DE_SYSTEM_FOREACH` | O(groups) |
| `de_system_remove` | O(groups) |
| removal compaction | O(params) |

---

# 26. Memory model

Darken deliberately separates pointer metadata from entity storage:

```text
             de_manager
                 │
        ┌────────┴────────┐
        │                 │
     items[]           entity bytes
        │                 │
        ▼                 ▼
  ┌───────────┐    ┌───────────────┐
  │ E2        │───►│ E0 bytes      │
  │ E0        │───►│ E1 bytes      │
  │ E4        │───►│ E2 bytes      │
  │ ...       │    │ ...           │
  └───────────┘    └───────────────┘
```

The pointer ordering can change without moving the entity data.

This is the key design difference between:

```text
reordering entities
```

and:

```text
moving entity memory
```

Darken only does the first.

---

# 27. 68000-oriented design

Darken's design favors the target CPU in several ways:

- fixed-capacity storage;
- no allocator calls during gameplay;
- 16-bit counters;
- contiguous entity storage;
- precomputed entity pointers;
- pointer swaps instead of structure copies;
- simple loops;
- packed system arrays;
- 4-byte entity stride alignment.

The 4-byte alignment is applied to the **entity stride**, not merely to the beginning of the storage block. This means every entity begins at a predictable 4-byte boundary even when the payload size itself is not a multiple of four.

The implementation does not claim that every operation is universally optimal for every compiler configuration. The intended target is GCC/SGDK on the Motorola 68000.

---

# 28. API reference

## Types

```c
typedef void *(*de_state)(void *);

typedef struct de_entity *de_entity;
typedef struct de_manager de_manager;
typedef struct de_system de_system;
```

## Entity

```c
struct de_entity
{
    de_state state;
    de_state destructor;
    de_manager *manager;
    uint16_t slot;
    uint16_t tag;
    uint8_t data[];
};
```

## Manager

```c
struct de_manager
{
    de_entity *items;
    uint16_t capacity;
    uint16_t active_count;
    uint16_t paused_start;
};
```

## System

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

## Entity functions

```c
void *de_entity_exec(de_entity);
void *de_entity_update(de_entity);
void de_entity_swap(de_entity, de_entity);
void de_entity_pause(de_entity);
void de_entity_resume(de_entity);
void de_entity_delete(de_entity);
void de_entity_move_front(de_entity);
void de_entity_move_back(de_entity);
```

## Manager functions

```c
void de_manager_init(
    de_manager *,
    de_entity *,
    void *,
    uint16_t,
    uint16_t
);

de_entity de_manager_new(de_manager *);
void de_manager_update(de_manager *);
void de_manager_reset(de_manager *);
```

## System functions

```c
void de_system_init(
    de_system *,
    void **,
    uint16_t,
    uint16_t
);

uint16_t de_system_remove(
    de_system *,
    void *
);
```

## Macros

```c
DE_ENTITY_STRIDE(PAYLOAD)

DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)
DE_MANAGER_ARGS(NAME)
DE_MANAGER_FOREACH(M, CODE)

DE_SYSTEM_STORAGE(NAME, CAPACITY, PARAMS)
DE_SYSTEM_ARGS(NAME)
DE_SYSTEM_ADD(...)
DE_SYSTEM_FOREACH(...)
DE_SYSTEM_ITERATOR(...)
```

---

# 29. Important safety rules

### Do

- Keep manager storage alive for as long as the manager is used.
- Keep system storage alive for as long as the system is used.
- Treat `entity->data` as valid until the entity is deleted.
- Use the entity pointer returned by `de_manager_new`.
- Expect system removal to reorder groups.
- Expect manager iteration to run backwards.

### Do not

- Free manager storage while entities are alive.
- Reuse an entity pointer after `de_entity_delete`.
- Assume active entity ordering is stable.
- Modify a different manager entity while a `DE_MANAGER_FOREACH` loop is running unless the mutation is known to be safe.
- Assume `de_system_remove()` preserves order.
- Treat `DE_SYSTEM_ITERATOR` as a strictly portable `de_state` function pointer.

---

# 30. What Darken is — and is not

Darken is best understood as:

> A fixed-capacity entity lifecycle manager plus a compact pointer-processing system.

It is **not** a conventional archetype ECS.

It does not provide:

- runtime component registration;
- component type IDs;
- archetype migration;
- automatic queries;
- dynamic memory allocation;
- reflection;
- serialization;
- multithreading.

This is intentional.

The library provides the low-level mechanisms needed to build those higher-level systems without imposing their architecture.

---

# 31. Design philosophy

The core philosophy is:

```text
          predictable memory
                  +
          fixed capacity
                  +
        simple pointer movement
                  +
          explicit lifecycle
                  +
        target-specific efficiency
                  =
             Darken
```

The manager owns lifecycle and ordering.

The entity owns its state and payload.

The system owns packed lists of pointers.

That separation keeps the core small while allowing the game code to decide how entities and systems interact.

---

# 32. Version notes

This documentation describes the Darken 2.0 API contained in `darken.h`.

The implementation intentionally uses the `de_*` / `DE_*` naming scheme:

```text
Public functions/types  de_*
Public macros           DE_*
Internal functions      _de_*
Internal macros         _DE_*
```

For the authoritative behavior, always treat the implementation in `darken.h` as the source of truth.
