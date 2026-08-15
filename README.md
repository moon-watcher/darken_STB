# Darken

**Darken (DARKula ENgine) 2.0 Entity System — Sega Genesis / Motorola 68000**

`darken.h` is a single-header C entity/manager library designed for the
Sega Genesis, with a focus on predictable memory usage, contiguous storage,
constant-time entity operations and inexpensive per-frame updates on the
Motorola 68000.

The library is intentionally small: a manager owns a fixed-capacity set of
entities, each entity has a state function and optional payload, and an
optional system layer can process entity-related data in batches.

---

## Table of contents

- [Basic usage](#basic-usage)
- [Naming convention](#naming-convention)
- [Architecture](#architecture)
- [Entity](#entity)
- [Entity lifecycle](#entity-lifecycle)
- [Special states](#special-states)
- [Manager](#manager)
- [Manager storage](#manager-storage)
- [Pause and resume](#pause-and-resume)
- [Deletion](#deletion)
- [Iteration](#iteration)
- [Filtered apply](#filtered-apply)
- [Systems](#systems-darkensystems)
- [Memory layout and alignment](#memory-layout-and-alignment)
- [Performance](#performance)
- [Limits and caveats](#limits-and-caveats)
- [API summary](#api-summary)
- [License](#license)

---

# Basic usage

## Implementation

`darken.h` follows the single-header pattern.

In **one** `.c` file:

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
```

In other translation units:

```c
#include "darken.h"
```

Only one translation unit must define `DARKEN_IMPLEMENTATION`.

## Manager with static storage

The normal static-storage setup is:

```c
de_manager mgr;

DE_MANAGER_STORAGE(
    mgr_storage,
    CAPACITY,
    sizeof(MiPayload)
);

de_manager_init(
    &mgr,
    DE_MANAGER_ARGS(mgr_storage)
);
```

## Creating an entity

```c
de_entity e = de_manager_new(&mgr);

MiPayload *p = (MiPayload *)e->data;

e->state = (de_state)mi_funcion_update;
```

`de_manager_new()` does **not** zero the payload. Initialize the payload
explicitly when the entity is created.

---

# Naming convention

The current source is consistent about the public namespace:

```text
de_*  -> public types and functions
DE_*  -> public macros
_de_* -> internal functions
_DE_* -> internal macros
```

For example, `DE_ENTITY_STRIDE()` is a public macro, while `de_manager_init()` is a public function. The internal alignment helper is `_DE_ALIGN4()`.

Darken deliberately separates its namespaces:

| Category | Convention | Example |
|---|---|---|
| Public types/functions | `de_*` | `de_entity`, `de_manager_update()` |
| Public macros/constants | `DE_*` | `DE_STATE_LOOP`, `DE_MANAGER_STORAGE()` |
| Internal functions | `_de_*` | `_de_entity_swap()` |
| Internal macros | `_DE_*` | `_DE_ALIGN4`, `_DE_MANAGER_ITERATE` |

This is intentional.

`DE_*` is the public macro namespace, while `de_*` is the public
function/type namespace. They should not be treated as competing spellings
of the same kind of symbol.

---

# Architecture

Darken is built around three related concepts:

```text
                    +------------------+
                    |     Manager      |
                    |                  |
                    |  entity pointers |
                    +--------+---------+
                             |
             +---------------+---------------+
             |               |               |
             v               v               v
          Entity          Entity          Entity
          +data           +data           +data
             |
             v
       optional System layer
       for batch processing
```

The core design has four important characteristics.

## Fixed capacity

A manager has a known capacity and normally operates on caller-provided
storage.

This avoids runtime allocation and makes memory consumption predictable.

## Contiguous entity storage

The actual entity slots live consecutively in one storage buffer:

```text
[entity + payload][entity + payload][entity + payload]...
```

A separate pointer array provides direct indexed access to those slots.

## State-driven execution

An entity's `state` is a function pointer. Updating an entity means executing
that function and interpreting its return value.

## Active/paused partition

The manager divides its entity pointer array into two regions:

```text
[ paused entities ][ active entities ]
                   ^
              pause_index
```

Only the active region is processed by the normal manager update.

---

# Entity

The core entity contains:

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

Conceptually:

```text
+-------------------------+
| state                   |
+-------------------------+
| destructor              |
+-------------------------+
| manager                 |
+-------------------------+
| slot                    |
+-------------------------+
| tag                     |
+-------------------------+
| user payload            |
| ...                     |
+-------------------------+
```

The flexible `data[]` member allows application-specific payload data to be
stored immediately after the entity header.

For example:

```c
struct MiPayload
{
    int16_t x;
    int16_t y;
};

de_entity e = de_manager_new(&mgr);
MiPayload *p = (MiPayload *)e->data;

p->x = 10;
p->y = 20;
```

---

# Entity lifecycle

A newly created entity starts in a deliberately simple state:

```text
[new]
 state = DE_STATE_DELETE
 slot  = assigned
 data  = not initialized
```

It then follows this lifecycle:

```text
                     +------------------+
                     |       new        |
                     | state = delete  |
                     +--------+---------+
                              |
                              v
                  +-----------------------+
                  |        active         |
                  | state > PAUSE        |
                  +-----------+-----------+
                              |
                    +---------+---------+
                    |                   |
                 PAUSE               DELETE
                    |                   |
                    v                   v
             +-------------+     deleted/removed
             |   paused    |
             | state=PAUSE |
             +------+------+
                    |
                  RESUME
                    |
                    v
                 active
```

An active state can return another state function to transition to it.

Returning `DE_STATE_LOOP` means:

> Keep the current state exactly as it is.

Returning `DE_STATE_DELETE` means:

> Mark the entity for deletion.

That deletion is **deferred** when it happens through the state returned by
`de_entity_update()` / `de_manager_update()`.

---

# Special states

Darken reserves three state values:

```c
DE_STATE_DELETE   /* 0 */
DE_STATE_LOOP     /* 1 */
DE_STATE_PAUSE    /* 2 */
```

Values greater than `DE_STATE_PAUSE` represent active state-function
pointers.

## `DE_STATE_DELETE`

The entity is marked for deletion.

If a destructor exists, the destructor receives an opportunity to abort the
deletion by returning a different state.

## `DE_STATE_LOOP`

This is a special "do nothing to the state pointer" value.

When an update function returns `DE_STATE_LOOP`, the current `e->state` is
left untouched.

This avoids unnecessarily writing the state pointer every frame.

## `DE_STATE_PAUSE`

The entity becomes paused.

It is moved into the paused partition and skipped by the normal manager
update.

## Active state

Any state value greater than `DE_STATE_PAUSE` is treated as an active state
function.

The state function receives the entity payload pointer:

```c
void *state(void *data);
```

and returns the next state/control value.

---

# Manager

The manager is:

```c
struct de_manager
{
    de_entity *items;
    uint16_t size;
    uint16_t capacity;
    uint16_t pause_index;
};
```

Its pointer array is divided into two partitions:

```text
index 0
   |
   v
+-------------------+-----------------------+
|      PAUSED       |        ACTIVE         |
+-------------------+-----------------------+
                    ^
               pause_index
                                      ^
                                      |
                                     size
```

The active entities are therefore:

```text
items[pause_index ... size - 1]
```

The paused entities are:

```text
items[0 ... pause_index - 1]
```

The relative order inside either partition is **not guaranteed** after
swaps.

---

# Manager initialization

The public initialization function is:

```c
void de_manager_init(
    de_manager *,
    de_entity *,
    void *,
    uint16_t,
    uint16_t
);
```

The arguments are:

1. manager;
2. entity pointer array;
3. contiguous entity storage;
4. capacity;
5. payload size.

Initialization:

- stores the pointer array;
- stores capacity;
- resets `size` to zero;
- resets `pause_index` to zero;
- calculates the entity stride;
- precomputes the address of every entity slot.

The address calculation is performed only during initialization, not on every
runtime update.

---

# Manager storage

Static storage is declared with:

```c
DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)
```

Example:

```c
DE_MANAGER_STORAGE(
    mgr_storage,
    32,
    sizeof(struct MiPayload)
);

de_manager mgr;

de_manager_init(
    &mgr,
    DE_MANAGER_ARGS(mgr_storage)
);
```

The generated storage contains:

```text
mgr_storage
├── entities[]
├── data[]
├── capacity
└── payload_size
```

`entities[]` contains pointers to the entity slots.

`data[]` contains the actual contiguous entity storage.

`DE_MANAGER_ARGS()` supplies the four values required by
`de_manager_init()`.

---

# Entity creation

```c
de_entity de_manager_new(de_manager *);
```

A successful call:

- obtains a free slot;
- assigns its manager;
- assigns its slot index;
- initializes its state to `DE_STATE_DELETE`;
- clears its destructor;
- clears its tag;
- increments manager size.

The payload is **not zero-filled**.

If there is no free slot, `de_manager_new()` returns `NULL`.

Therefore the intended pattern is:

```c
de_entity e = de_manager_new(&mgr);

if (e)
{
    MyPayload *p = (MyPayload *)e->data;

    /* initialize p */

    e->state = (de_state)my_state;
}
```

---

# Pause and resume

The manager uses `pause_index` to maintain the active/paused partition.

## `de_entity_pause(e)`

Moves `e` to the paused partition:

```text
[ paused ... ][ e ][ active ... ]
             ^
```

The operation is O(1): it swaps entity pointers and adjusts
`pause_index`.

## `de_entity_resume(e)`

Moves `e` to the active partition:

```text
[ paused ... ][ active ... ][ e ]
```

Again, this is O(1).

## `de_manager_pause(m)`

Pauses all entities:

```c
m->pause_index = m->size;
```

O(1).

## `de_manager_resume(m)`

Activates all entities:

```c
m->pause_index = 0;
```

O(1).

### Ordering

Because partition changes use pointer swaps, **relative ordering within the
paused or active partitions is not guaranteed**.

Do not rely on entity order unless your own code establishes that ordering
again.

---

# Update

```c
void de_manager_update(de_manager *);
```

The manager updates only active entities.

Conceptually:

```text
for each active entity:
    execute its state
    interpret returned state
```

The update traversal is designed to tolerate entity deletion without shifting
the complete pointer array.

A state function can:

```c
return DE_STATE_LOOP;
```

to remain in the current state,

```c
return DE_STATE_PAUSE;
```

to pause,

```c
return DE_STATE_DELETE;
```

to request deletion,

or return another state function to transition.

---

# Deletion

Darken deliberately supports two deletion paths.

## Deferred deletion through state

An entity can return:

```c
DE_STATE_DELETE
```

from its update.

The entity is then **marked for deletion**, but physical deletion occurs on
the **next update/frame**.

This is useful when the state function wants to request destruction without
modifying the manager immediately during its own update.

```text
frame N:
    state() → DE_STATE_DELETE
              |
              v
          marked delete

frame N+1:
    manager processes deletion
              |
              v
          entity removed
```

## Immediate deletion

```c
de_entity_delete(e);
```

forces the delete state and performs physical removal immediately.

Use this when immediate compaction is explicitly desired.

## Destructor

If:

```c
e->destructor != NULL
```

the destructor is called before deletion.

The destructor can **abort deletion** by returning a state other than
`DE_STATE_DELETE`.

This provides a final state-transition hook before an entity disappears.

---

# Entity movement

The public API also provides:

```c
de_entity_move_front(e);
de_entity_move_back(e);
```

These operations use pointer swaps rather than shifting the whole array.

They are therefore O(1).

As with pause/resume, swapping means that entity ordering should not be
treated as stable unless the application explicitly maintains an ordering
policy.

---

# Iteration

Two public iteration macros are provided:

```c
DE_MANAGER_ITERATE(m, { ... });
DE_MANAGER_ITERATE_ALL(m, { ... });
```

## Active entities

```c
DE_MANAGER_ITERATE(m, {
    /* active entities only */
});
```

Only the active partition is visited.

## All entities

```c
DE_MANAGER_ITERATE_ALL(m, {
    /* paused + active */
});
```

Both partitions are visited.

## Variables provided inside the block

The macros automatically provide:

```c
de_entity ENTITY = entidad_actual;
uint16_t INDEX = indice_en_el_array;
```

Example:

```c
DE_MANAGER_ITERATE(&mgr, {
    if (ENTITY->tag == PLAYER_TAG)
    {
        update_player(ENTITY);
    }
});
```

The iteration implementation is designed around the manager's compact pointer
array and its active/paused partition.

---

# Filtered apply

Darken provides:

```c
DE_MANAGER_APPLY(m, FILTER, ACTION);
DE_MANAGER_APPLY_ALL(m, FILTER, ACTION);
```

The important property of `APPLY` is that filtering and modification are
separated.

First:

```text
manager
   |
   v
filter every candidate
   |
   v
temporary target array
```

Then:

```text
target array
   |
   v
execute ACTION
```

Therefore an action such as:

```c
de_entity_delete
```

can safely modify the manager because filtering has already finished.

Example:

```c
DE_MANAGER_APPLY(
    &mgr,
    ENTITY->tag == DEAD_TAG,
    de_entity_delete
);
```

`DE_MANAGER_APPLY_ALL` includes paused entities.

## VLA warning

The temporary target array is a **VLA on the stack**.

This matters on the actual Genesis hardware, where stack space is limited.

As a practical rule from the current API documentation:

> Do not use `DE_MANAGER_APPLY_ALL` with managers larger than roughly
> 200 entities on real hardware unless you have explicitly verified your
> stack budget.

For large managers, prefer direct iteration or a custom external target
buffer.

---

# Systems (`DARKEN_SYSTEMS`)

The system layer is an optional higher-level facility for processing data in
batches.

It is designed to sit on top of the core entity/manager functionality.

A system stores groups of pointers with a fixed number of parameters per
group.

For example, with:

```text
params = 3
```

the logical layout is:

```text
[group 0] [A0] [B0] [C0]
[group 1] [A1] [B1] [C1]
[group 2] [A2] [B2] [C2]
...
```

This makes it possible to process related data sequentially.

---

# System storage

Static system storage is declared with:

```c
DE_SYSTEM_STORAGE(NAME, CAPACITY, PARAMS)
```

and passed to initialization with:

```c
DE_SYSTEM_ARGS(NAME)
```

Example:

```c
de_system sys;

DE_SYSTEM_STORAGE(
    sys_storage,
    32,
    3
);

de_system_init(
    &sys,
    DE_SYSTEM_ARGS(sys_storage)
);
```

---

# `DE_SYSTEM_STORAGE` + `de_system_init()`

The API documentation also exposes:

```c
DE_SYSTEM_STORAGE(NAME, CAPACITY, PARAMS) + de_system_init()
```

for initializing a system with stack storage.

Use this form when the lifetime and stack requirements of the generated
storage are appropriate.

For larger or longer-lived systems, explicit `DE_SYSTEM_STORAGE` makes the
storage object and its lifetime clearer.

---

# `DE_SYSTEM_ADD`

```c
DE_SYSTEM_ADD(sys, args...);
```

Adds one group of pointers to the system.

It supports the parameter forms provided by the current implementation.

Return value:

```text
1 → group added
0 → system full
```

Example conceptually:

```c
DE_SYSTEM_ADD(
    sys,
    entity,
    position,
    velocity
);
```

The group is stored contiguously in the system's flat pointer pool.

---

# `DE_SYSTEM_FOREACH`

```c
DE_SYSTEM_FOREACH(sys, args..., code)
```

Iterates directly over the groups in a system without generating a separate
state function.

This is appropriate for code that wants to process a system immediately.

The macro supports the parameter forms implemented by the library.

---

# `DE_SYSTEM_ITERATOR`

```c
DE_SYSTEM_ITERATOR(name, args..., code)
```

generates a function:

```c
void *name(de_system *)
```

that iterates over the system and returns:

```c
DE_STATE_LOOP
```

This makes a system iterator suitable for use as an entity state.

Conceptually:

```text
entity state
     |
     v
system iterator
     |
     v
process all system groups
     |
     v
DE_STATE_LOOP
```

This provides a simple bridge between the entity state-machine model and
batch-oriented system processing.

---

# System removal

The system API also provides:

```c
de_system_remove(de_system *, void *);
```

A group is identified by its first pointer.

The removal process searches the flat pool, then compacts the final group into
the removed position when necessary.

The search is O(n); moving a group is O(params).

---

# Memory layout and alignment

The target is the Motorola 68000.

The storage design therefore pays particular attention to alignment.

## Entity stride

Entity storage uses a stride rounded to 4 bytes:

```text
sizeof(de_entity) + payload_size
                 ↓
             round to 4
```

The internal helper is:

```c
_DE_ALIGN4(...)
```

This guarantees that the beginning of every consecutive entity slot has the
required alignment.

## Storage alignment

The manager storage buffer is also explicitly aligned to 4 bytes.

The intent is that 32-bit values/pointers can be accessed at aligned
addresses.

## If `de_entity` changes

If the entity structure is modified, verify its resulting size and alignment.

The storage model assumes that the entity stride remains correctly aligned.
If the entity layout changes, adjust the alignment logic if necessary.

---

# Performance

The implementation is designed for the 7.6 MHz Motorola 68000 used by the
Sega Genesis.

The supplied API documentation gives these typical figures:

| Operation / workload | Typical result |
|---|---:|
| Active entity update | ~27–45 µs per entity per frame |
| Practical active-entity budget | ~600 entities/frame without drop |
| `de_entity_swap` | ~50 µs |

The exact cost depends on the payload and the state function being executed.

These figures should be treated as **typical reference values**, not strict
hardware guarantees.

The important design choices behind the performance target are:

- fixed-capacity storage;
- contiguous entity slots;
- precomputed entity pointers;
- O(1) swap-based partitioning;
- no runtime allocation in the static-storage path;
- compact 16-bit manager metadata;
- sequential iteration.

---

# Complexity

The intended complexity of the core operations is:

| Operation | Complexity |
|---|---:|
| `de_manager_init()` | O(capacity), once |
| `de_manager_new()` | O(1) |
| `de_entity_pause()` | O(1) |
| `de_entity_resume()` | O(1) |
| `de_entity_move_front()` | O(1) |
| `de_entity_move_back()` | O(1) |
| `de_entity_delete()` | O(1) + destructor |
| `de_manager_pause()` | O(1) |
| `de_manager_resume()` | O(1) |
| `de_manager_update()` | O(active entities) |
| `de_manager_reset()` | O(entities) |
| `de_system_remove()` | O(n) search + O(params) compaction |
| `DE_MANAGER_APPLY()` | O(n) filtering + O(matches) actions |

The constant factors matter considerably on the 68000, which is why the
implementation favors swaps, sequential traversal and precomputed addresses.

---

# Limits and caveats

## Capacity

Manager capacity is represented by `uint16_t`:

```text
maximum representable value: 65535
```

In practice, available Genesis RAM is the meaningful constraint long before
this theoretical limit.

## Payload initialization

Entity payloads are **not automatically zeroed when a slot is reused**.

Always initialize `e->data` explicitly.

## Ordering

Pause/resume, deletion and entity movement use swaps.

Do not depend on stable entity ordering unless you explicitly maintain it.

## Deferred deletion

Returning `DE_STATE_DELETE` from a state does not mean that the entity is
physically removed immediately.

It is a deferred deletion request processed by the manager.

Use:

```c
de_entity_delete(e);
```

when immediate removal is required.

## Destructor cancellation

A destructor can abort deletion by returning a state other than
`DE_STATE_DELETE`.

Code relying on unconditional destruction should account for this behavior.

## `APPLY` stack usage

`DE_MANAGER_APPLY` and especially `DE_MANAGER_APPLY_ALL` use a VLA.

On real Genesis hardware, stack space is limited.

Avoid large apply operations unless their stack consumption has been measured.

## Thread safety

Darken is not thread-safe.

That is generally irrelevant to its intended single-threaded Genesis runtime,
but it is still an architectural limitation.

## Static storage lifetime

When using `DE_MANAGER_STORAGE` or stack-based creation macros, the storage
must remain alive for as long as the manager uses it.

Do not return a manager whose backing storage was allocated in a scope that has
already ended.

---

# API summary

## Core types

```c
de_state
de_state_f
de_entity
de_manager
de_system
```

## Entity operations

```c
de_entity_exec()
de_entity_update()
de_entity_pause()
de_entity_resume()
de_entity_delete()
de_entity_move_front()
de_entity_move_back()
```

## Manager operations

```c
de_manager_init()
de_manager_new()
de_manager_update()
de_manager_pause()
de_manager_resume()
de_manager_reset()
```

## Manager macros

```c
DE_MANAGER_STORAGE()
DE_MANAGER_ARGS()

DE_MANAGER_ITERATE()
DE_MANAGER_ITERATE_ALL()

DE_MANAGER_APPLY()
DE_MANAGER_APPLY_ALL()
```

## State constants

```c
DE_STATE_DELETE
DE_STATE_LOOP
DE_STATE_PAUSE
```

## System operations

```c
de_system_init()
de_system_remove()
```

## System macros

```c
DE_SYSTEM_STORAGE()
DE_SYSTEM_ARGS()
DE_SYSTEM_ADD()
DE_SYSTEM_FOREACH()
DE_SYSTEM_ITERATOR()
```

## Internal namespace

Internal helpers use:

```text
_de_*
_DE_*
```

and should not be used by application code.

---

# License

Public domain.

Use, modify and break it to your liking.
