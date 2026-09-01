# Darken 2.0

**DARKula ENgine — Entity System**

`darken-2.0.0-dev`

Darken is a small, fixed-capacity C entity system designed for games with large numbers of short-lived objects, such as shoot-'em-ups, particle systems, enemies and projectiles.

Its main design goal is simple:

> **Entities never move in memory. Only the pointers that manage them move.**

This provides cheap spawning, deletion, pausing and resuming while keeping pointers to an entity's payload stable during its lifetime.

---

## Features

* Fixed-capacity entity pools
* No per-entity allocation
* Contiguous entity storage
* O(1) spawn
* O(1) delete
* O(1) pause/resume
* Stable entity addresses
* Callback-based entity state machines
* Static or dynamic storage
* Designed with 16-bit / Motorola 68000 targets in mind
* GNU C extensions for compact macros

---

# The Entity Pool

Darken separates entity memory from the pool used to manage it.

```text
storage:

+--------+--------+--------+--------+--------+
| Entity | Entity | Entity | Entity | Entity |
+--------+--------+--------+--------+--------+

pool:

+------+--------+------+--------+------+
|  ptr |   ptr  | ptr  |  ptr   | ptr  |
+------+--------+------+--------+------+
```

The entities in `storage` never move.

`pool[]` is reordered instead.

The pool is divided into three zones:

```text
[ ACTIVE ][ FREE ][ PAUSED ]
0        size    paused     capacity
```

Active entities are updated.

Free entities can be spawned.

Paused entities remain alive but are excluded from the update loop.

This is the fundamental mechanism behind Darken.

---

# Public Types

## `darken`

The entity manager/context:

```c
typedef struct darken
{
    darken_entity *pool;
    uint8_t *storage;
    uint16_t capacity;
    uint16_t size;
    uint16_t paused;
    uint16_t stride;
} darken;
```

`darken` owns the pool state but not necessarily the memory itself.

Memory can be supplied either dynamically or statically.

---

## `darken_entity`

An entity is an opaque pointer to:

```c
struct darken_entity
{
    uint16_t slot;
    darken *owner;

    darken_state update;
    darken_state destroy;

    uint32_t tag;
    uint16_t usr;

    uint8_t data[];
};
```

The `data[]` area is the user-defined payload.

For example:

```c
typedef struct
{
    int16_t x, y;
    int16_t vx, vy;
    uint16_t hp;
} Enemy;
```

A Darken enemy can simply use:

```c
Enemy *enemy = (Enemy *)entity->data;
```

---

# `darken_state`

```c
typedef void *(*darken_state)();
```

Entity behavior is represented by callbacks.

A callback receives the entity payload:

```c
darken_state enemy_update(void *data)
{
    Enemy *enemy = data;

    enemy->x += enemy->vx;
    enemy->y += enemy->vy;

    return DARKEN_LOOP;
}
```

Returning another callback effectively changes the entity's state:

```text
enter → attack → hurt → dying → delete
```

This makes Darken particularly convenient for game state machines.

---

# Control Values

Darken defines three special callback results:

```c
#define DARKEN_DELETE ((void *)0)
#define DARKEN_LOOP   ((void *)1)
#define DARKEN_PAUSE  ((void *)2)
```

### `DARKEN_LOOP`

Continue using the current callback.

```c
return DARKEN_LOOP;
```

### `DARKEN_PAUSE`

Move the entity to the paused zone.

```c
return DARKEN_PAUSE;
```

### `DARKEN_DELETE`

Destroy and recycle the entity.

```c
return DARKEN_DELETE;
```

---

# Pool Creation

Darken supports both dynamic and static pools.

## Dynamic

```c
darken enemies = DARKEN_POOL_ALLOC(
    malloc,
    64,
    sizeof(Enemy)
);
```

The pool and storage are allocated separately.

When finished:

```c
free(enemies.pool);
free(enemies.storage);
```

---

## Static

```c
DARKEN_POOL_DECLARE(
    enemy_storage,
    64,
    sizeof(Enemy)
);

darken enemies = DARKEN_POOL_INIT(enemy_storage);
```

This is especially useful on systems where dynamic allocation is undesirable.

---

# `darken_init()`

```c
void darken_init(darken *);
```

Initializes the pool and associates every storage slot with an entity.

After initialization:

```text
size   = 0
paused = capacity
```

All entities are initially available for spawning.

---

# `DARKEN_SPAWN()`

```c
darken_entity entity = DARKEN_SPAWN(&enemies);
```

Returns an available entity or `0` when the pool is full.

Typical usage:

```c
darken_entity entity = DARKEN_SPAWN(&enemies);

if (entity)
{
    Enemy *enemy = (Enemy *)entity->data;

    enemy->x = 100;
    enemy->y = 20;
    enemy->vx = 0;
    enemy->vy = 2;
    enemy->hp = 3;

    entity->tag = TAG_ENEMY;
    entity->usr = 0;
    entity->update = enemy_update;
    entity->destroy = enemy_destroy;
}
```

### Important

Darken does **not** clear an entity when it is spawned.

A recycled entity may contain data left by its previous occupant.

The caller must initialize everything it needs:

```c
data
update
destroy
tag
usr
```

This is intentional and avoids unnecessary initialization overhead.

---

# `darken_update()`

```c
void darken_update(darken *);
```

Updates every active entity.

Conceptually:

```text
for each active entity:
    execute update callback
    process returned state
```

Paused entities are not visited.

This makes the amount of update work proportional to the number of active entities rather than the pool capacity.

---

# `darken_entity_run()`

```c
uint16_t darken_entity_run(darken_entity);
```

Executes the entity's current update callback immediately.

Useful when an individual entity needs to be driven manually rather than through `darken_update()`.

---

# `darken_entity_update()`

```c
uint16_t darken_entity_update(darken_entity);
```

Runs the entity's callback and processes its returned state.

This is essentially the single-entity equivalent of the lifecycle processing performed by `darken_update()`.

---

# `darken_entity_pause()`

```c
uint16_t darken_entity_pause(darken_entity);
```

Moves an active entity into the paused zone.

The entity remains alive and its payload remains in the same memory address.

Useful for things such as:

```text
boss dialogue
temporary enemy freeze
scripted sequences
disabled objects
```

---

# `darken_entity_resume()`

```c
uint16_t darken_entity_resume(darken_entity);
```

Moves a paused entity back into the active zone.

The entity itself does not move in memory.

---

# `darken_entity_delete()`

```c
uint16_t darken_entity_delete(darken_entity);
```

Deletes an entity and returns its storage slot to the free zone.

Deletion is O(1).

There is no array shifting.

This is particularly valuable for shmups:

```text
bullet spawn
bullet update
bullet leaves screen
bullet delete
bullet slot reused
```

without repeated heap allocation.

---

# `darken_reset()`

```c
void darken_reset(darken *);
```

Resets the entire context.

Active entities are destroyed and the pool returns to:

```text
[ FREE FREE FREE FREE FREE ]
```

The allocated memory itself is retained.

This is useful for restarting levels or waves.

---

# `DARKEN_FOREACH`

```c
DARKEN_FOREACH(CTX, CODE)
```

Iterates over active entities.

Example:

```c
DARKEN_FOREACH(&enemies,
{
    Enemy *enemy = (Enemy *)_entity->data;

    draw_enemy(enemy);
});
```

Only active entities are visited.

The iteration order is **not stable** because Darken uses swaps when moving entities between zones.

Do not use `entity->slot` as a persistent entity ID.

---

# `DARKEN_DATA`

Convenience macro for obtaining a typed payload:

```c
DARKEN_DATA(Enemy, enemy, entity);
```

Equivalent to:

```c
Enemy *enemy = (Enemy *)entity->data;
```

---

# `DARKEN_DATA_GET_ENTITY`

```c
DARKEN_DATA_GET_ENTITY(data)
```

Recovers the owning entity from its payload pointer.

This is useful because callbacks receive `entity->data` rather than the entity itself:

```c
darken_state enemy_update(void *data)
{
    darken_entity entity = DARKEN_DATA_GET_ENTITY(data);

    Enemy *enemy = data;

    ...
}
```

This is a deliberately low-level facility and depends on the target's pointer/layout assumptions.

---

# Example: Shmup Enemy

A complete enemy can be extremely small:

```c
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint16_t hp;
} Enemy;

darken_state enemy_update(void *data)
{
    Enemy *e = data;

    e->x += e->vx;
    e->y += e->vy;

    if (e->hp == 0)
        return DARKEN_DELETE;

    if (e->y > SCREEN_HEIGHT)
        return DARKEN_DELETE;

    return DARKEN_LOOP;
}
```

Spawning:

```c
void spawn_enemy(int16_t x, int16_t y)
{
    darken_entity entity = DARKEN_SPAWN(&enemies);

    if (!entity)
        return;

    Enemy *e = (Enemy *)entity->data;

    e->x = x;
    e->y = y;
    e->vx = 0;
    e->vy = 2;
    e->hp = 3;

    entity->tag = TAG_ENEMY;
    entity->usr = 0;
    entity->update = enemy_update;
    entity->destroy = NULL;
}
```

The same pattern works for bullets:

```text
spawn → update → delete → reuse
```

and particles:

```text
spawn → update → lifetime expires → delete → reuse
```

---

# Important Design Characteristics

## Stable addresses

This is one of Darken's main strengths.

If:

```c
Enemy *enemy = (Enemy *)entity->data;
```

is saved, reordering the pool does not move the payload.

Only the pointer in `pool[]` changes.

---

## O(1) lifecycle operations

Spawn, delete, pause and resume are designed around swapping pointers rather than moving entity data.

This is ideal for high-churn objects such as bullets and particles.

---

## Fixed memory usage

A pool with:

```c
DARKEN_POOL_DECLARE(bullets, 256, sizeof(Bullet));
```

has exactly 256 possible bullet entities.

There is no hidden allocation or automatic growth.

This makes memory requirements predictable.

---

# Important Limitations

### GNU C

Darken uses GNU extensions such as:

```c
__attribute__
({ ... })
```

It is therefore not intended to be strict ISO C.

### Fixed capacity

Pools do not grow automatically.

`DARKEN_SPAWN()` returns `0` when no slot is available.

### Unstable ordering

Pool positions change when entities are swapped.

`slot` is an internal position, not an entity ID.

### Recycled memory

Spawned entities are not automatically cleared.

Always initialize recycled entities explicitly.

### Pointer lifetime

A payload pointer remains stable while its entity is alive, but after deletion the same memory may be reused by another entity.

Stable address does **not** mean stable identity.

### 16-bit fields

`capacity`, `size`, `paused`, `slot` and `stride` are `uint16_t`, reflecting Darken's 16-bit-oriented design.

---

# Design Trade-off

Darken intentionally chooses:

```text
fixed capacity
      +
homogeneous pools
      +
stable entity addresses
      +
O(1) lifecycle operations
```

instead of:

```text
dynamic heterogeneous ECS
      +
stable ordering
      +
automatic allocation
```

For a shmup, the first model is often exactly what is wanted.

A typical game might use:

```text
players     → Player pool
enemies     → Enemy pool
bullets     → Bullet pool
particles   → Particle pool
```

Each pool is simple, predictable and independently sized.

---

# In Short

Darken is built around one central idea:

> **Move pointers, never entities.**

That gives you:

* predictable memory
* cheap object recycling
* stable payload addresses
* fast deletion
* simple state machines
* no per-entity allocation

while accepting:

* fixed capacities
* unstable ordering
* explicit initialization
* low-level compiler/ABI assumptions

For a small, performance-oriented game — especially a shmup — this is a very strong and focused model.
