# Darken 2.0

**DARKula ENgine — Entity System**

`darken-2.0.0-dev`

A small, low-level C entity system designed around:

* fixed-address entities
* pointer-based entity pools
* contiguous payload storage
* O(1) entity allocation
* O(1) deletion
* O(1) pause/resume
* explicit lifecycle callbacks
* zero per-entity heap allocation
* predictable memory usage
* 16-bit-oriented data structures
* 4-byte payload alignment
* compatibility with constrained and retro-oriented environments

Darken is deliberately **not a general-purpose ECS**.

It is a compact entity manager intended for games and other systems where a large number of short-lived objects must be created, updated, paused, destroyed, and recycled efficiently.

The examples in this document progressively build a small **shoot-'em-up (shmup)** using Darken.

---

# Table of Contents

1. [Design Philosophy](#design-philosophy)
2. [The Core Idea](#the-core-idea)
3. [Memory Model](#memory-model)
4. [The Three-Zone Pool](#the-three-zone-pool)
5. [Entity Lifetime](#entity-lifetime)
6. [Stable Entity Addresses](#stable-entity-addresses)
7. [The `darken` Context](#the-darken-context)
8. [The Entity Structure](#the-entity-structure)
9. [Payloads](#payloads)
10. [State Callbacks](#state-callbacks)
11. [Control Values](#control-values)
12. [Creating a Pool](#creating-a-pool)
13. [Dynamic Allocation](#dynamic-allocation)
14. [Static Allocation](#static-allocation)
15. [Initialization](#initialization)
16. [Spawning Entities](#spawning-entities)
17. [Updating Entities](#updating-entities)
18. [Deleting Entities](#deleting-entities)
19. [Pausing Entities](#pausing-entities)
20. [Resuming Entities](#resuming-entities)
21. [Resetting a Context](#resetting-a-context)
22. [Iterating Entities](#iterating-entities)
23. [Recovering an Entity from Its Payload](#recovering-an-entity-from-its-payload)
24. [Building a Shmup](#building-a-shmup)
25. [Player Entity](#player-entity)
26. [Enemy Entity](#enemy-entity)
27. [Bullets](#bullets)
28. [Enemy Spawning](#enemy-spawning)
29. [Collision Handling](#collision-handling)
30. [Pausing a Game](#pausing-a-game)
31. [Wave Transitions](#wave-transitions)
32. [Why Stable Payload Pointers Matter](#why-stable-payload-pointers-matter)
33. [Complexity](#complexity)
34. [Strengths](#strengths)
35. [Limitations](#limitations)
36. [Pitfalls](#pitfalls)
37. [Important Implementation Issues](#important-implementation-issues)
38. [Safe Usage Rules](#safe-usage-rules)
39. [When Darken Is a Good Fit](#when-darken-is-a-good-fit)
40. [When Darken Is Not a Good Fit](#when-darken-is-not-a-good-fit)
41. [Complete Shmup Skeleton](#complete-shmup-skeleton)
42. [Summary](#summary)

---

# Design Philosophy

Darken is based on a very simple premise:

> **Entities should not move. Pointers to entities should remain valid. Only their position in the management pool should change.**

Instead of storing entities directly in an array that is continuously compacted, Darken separates:

```text
ENTITY MEMORY
    |
    +-- entity 0
    +-- entity 1
    +-- entity 2
    +-- entity 3
    +-- ...
```

from:

```text
POOL OF POINTERS
    |
    +-- pointer -> entity 3
    +-- pointer -> entity 0
    +-- pointer -> entity 7
    +-- ...
```

The entity's physical address never changes.

The pool pointer array is what gets reordered.

This distinction is the central architectural decision behind Darken.

---

# The Core Idea

A Darken context manages two separate resources:

```text
+--------------+
| darken       |
|              |
| pool  -------+----> array of entity pointers
| storage -----+----> contiguous entity memory
| capacity     |
| size         |
| paused       |
| stride       |
+--------------+
```

The entity pointers are rearranged whenever an entity changes lifecycle state.

The entities themselves are not moved.

For example:

```text
storage:

+--------+--------+--------+--------+--------+
| Entity | Entity | Entity | Entity | Entity |
+--------+--------+--------+--------+--------+
    ^         ^         ^         ^         ^
    |         |         |         |         |
   E0        E1        E2        E3        E4
```

The pool may contain:

```text
pool:

+----+----+----+----+----+
| E3 | E0 | E4 | E1 | E2 |
+----+----+----+----+----+
```

The order of the pointers is irrelevant to the physical addresses.

---

# Memory Model

Darken requires two memory areas.

## 1. Pointer pool

The pointer pool contains:

```c
darken_entity *
```

entries.

For a capacity of `N`:

```text
N × sizeof(darken_entity)
```

bytes are required.

---

## 2. Entity storage

The entity storage contains:

```text
N × entity_stride
```

bytes.

The stride is:

```c
_DARKEN_ALIGN4(sizeof(struct darken_entity) + PAYLOAD)
```

Conceptually:

```text
+-----------------------------+
| darken_entity header        |
+-----------------------------+
| user payload                |
+-----------------------------+
| alignment padding           |
+-----------------------------+
```

Every entity occupies the same stride.

This allows Darken to calculate an entity's address using:

```text
base + index × stride
```

without allocating each entity independently.

---

# The Three-Zone Pool

The most important data structure in Darken is the pool.

At all times, the pool is conceptually divided into three zones:

```text
0                   size              paused          capacity
|--------------------|------------------|----------------|
|      ACTIVE        |       FREE       |     PAUSED     |
|                    |                  |                |
|    updated every   |   available for  | excluded from  |
|       frame        |      spawn       | update loop    |
|--------------------|------------------|----------------|
```

This is the fundamental invariant.

## Active zone

```text
[0, size)
```

Contains entities currently participating in the update loop.

For example:

```text
size = 4
paused = 7
capacity = 10

[ E0 ][ E3 ][ E8 ][ E2 ][ -- ][ -- ][ -- ][ P1 ][ P2 ][ P3 ]
  <----------- active -----------> <free> <---- paused ---->
```

Active entities are visited by:

```c
darken_update(&ctx);
```

and:

```c
DARKEN_FOREACH(&ctx, ...)
```

---

## Free zone

```text
[size, paused)
```

These pointer slots are currently unused.

This is where:

```c
DARKEN_SPAWN(ctx)
```

gets the next entity.

No heap allocation occurs during spawning.

---

## Paused zone

```text
[paused, capacity)
```

Paused entities are removed from the update loop.

They remain owned by the context, but Darken does not update them.

This is particularly useful for:

* paused gameplay
* off-screen objects
* temporarily disabled objects
* suspended enemies
* scripted sequences
* object freezing

---

# Entity Lifetime

An entity can conceptually move through:

```text
             +----------+
             |   FREE   |
             +----------+
                  |
                SPAWN
                  |
                  v
             +----------+
             |  ACTIVE  |
             +----------+
              |        |
          PAUSE        DELETE
              |        |
              v        v
          +--------+  FREE
          | PAUSED |
          +--------+
              |
            RESUME
              |
              v
           ACTIVE
```

Deletion always returns the entity's pool slot to the free zone.

The important detail is that **the entity's memory is reused**.

Darken does not repeatedly allocate and free individual entities.

---

# Stable Entity Addresses

This is one of Darken's strongest properties.

Suppose:

```c
darken_entity enemy = DARKEN_SPAWN(&ctx);
```

and:

```c
Enemy *data = (Enemy *)enemy->data;
```

Now the entity is paused:

```c
darken_entity_pause(enemy);
```

or its pool position changes because another entity is deleted.

The pointer:

```c
data
```

still points to the same physical memory.

Why?

Because Darken swaps **pointers inside `pool[]`**, not entity objects inside `storage`.

This makes it possible to keep raw pointers to payload data.

For example:

```c
Enemy *boss_data = NULL;

darken_entity boss = DARKEN_SPAWN(&enemies);

if (boss)
{
    boss_data = (Enemy *)boss->data;
}
```

Later:

```c
darken_entity_pause(boss);
```

`boss_data` still refers to the same payload.

This is a major difference from an implementation based on moving/compacting entity objects.

---

# The `darken` Context

The main context is:

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

Each member has a specific role.

| Member     | Meaning                                     |
| ---------- | ------------------------------------------- |
| `pool`     | Pointer array used for lifecycle management |
| `storage`  | Base address of contiguous entity memory    |
| `capacity` | Maximum number of entities                  |
| `size`     | Number of active entities                   |
| `paused`   | Beginning of the paused zone                |
| `stride`   | Bytes occupied by each entity               |

The intended invariant is:

```text
0 <= size <= paused <= capacity
```

This invariant is critical.

---

# The Entity Structure

The entity header is:

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

It contains three conceptual sections.

## Private management data

```c
uint16_t slot;
darken *owner;
```

These fields allow Darken to know:

* which context owns the entity
* where its pointer currently resides in the pool

---

## Lifecycle callbacks

```c
darken_state update;
darken_state destroy;
```

`update` determines what happens during the next update.

`destroy` is optionally called before an entity is removed.

---

## User metadata

```c
uint32_t tag;
uint16_t usr;
```

These are deliberately generic.

A game could use `tag` for:

```text
PLAYER
ENEMY
BULLET
BOSS
POWERUP
PARTICLE
```

and `usr` for:

```text
sprite index
team
weapon ID
animation ID
small state value
```

For example:

```c
#define TAG_PLAYER  1
#define TAG_ENEMY   2
#define TAG_BULLET  3
#define TAG_POWERUP 4
```

---

# Payloads

The flexible array:

```c
uint8_t data[];
```

is the entity's user-defined payload.

For example:

```c
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint16_t hp;
} Enemy;
```

A pool can then be created with:

```c
sizeof(Enemy)
```

as its payload size.

Every entity in that pool has an `Enemy` payload.

This makes a Darken context effectively a homogeneous entity pool.

---

# Homogeneous Pools

Darken is not designed for:

```text
one pool containing arbitrary payload sizes
```

Instead, a pool represents one payload type.

For example:

```text
player_pool  -> Player
enemy_pool   -> Enemy
bullet_pool  -> Bullet
particle_pool -> Particle
```

This is a deliberate trade-off.

It makes memory layout predictable and extremely cheap.

A shmup might therefore have:

```c
darken players;
darken enemies;
darken bullets;
darken particles;
```

rather than one giant heterogeneous entity database.

---

# State Callbacks

The callback type is:

```c
typedef void *(*darken_state)();
```

An update callback receives:

```c
entity->data
```

and returns either:

* another callback
* `DARKEN_LOOP`
* `DARKEN_PAUSE`
* `DARKEN_DELETE`

A normal update function might be:

```c
darken_state enemy_update(void *data)
{
    Enemy *enemy = data;

    enemy->x += enemy->vx;
    enemy->y += enemy->vy;

    return enemy_update;
}
```

The returned callback becomes the entity's next state.

This makes state machines very natural.

For example:

```text
enemy_spawn
     |
     v
enemy_enter
     |
     v
enemy_attack
     |
     v
enemy_dying
     |
     v
DARKEN_DELETE
```

No separate state enum is required.

---

# Control Values

Darken defines:

```c
#define DARKEN_DELETE ((void *)0)
#define DARKEN_LOOP   ((void *)1)
#define DARKEN_PAUSE  ((void *)2)
```

These values are interpreted specially.

## `DARKEN_DELETE`

The entity should be destroyed.

Example:

```c
return DARKEN_DELETE;
```

---

## `DARKEN_LOOP`

Keep the same callback.

Example:

```c
return DARKEN_LOOP;
```

This is useful when the callback itself is already stored in:

```c
entity->update
```

---

## `DARKEN_PAUSE`

Move the entity into the paused zone.

Example:

```c
return DARKEN_PAUSE;
```

The entity stops participating in normal updates.

---

# A Basic Enemy

Consider:

```c
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint16_t hp;
} Enemy;
```

The update callback:

```c
darken_state enemy_update(void *data)
{
    Enemy *e = data;

    e->x += e->vx;
    e->y += e->vy;

    if (e->hp == 0)
        return DARKEN_DELETE;

    return DARKEN_LOOP;
}
```

The entity can then be spawned and initialized:

```c
darken_entity entity = DARKEN_SPAWN(&enemies);

if (entity)
{
    Enemy *enemy = (Enemy *)entity->data;

    enemy->x = 200;
    enemy->y = -20;
    enemy->vx = 0;
    enemy->vy = 2;
    enemy->hp = 3;

    entity->tag = TAG_ENEMY;
    entity->usr = 0;
    entity->update = enemy_update;
    entity->destroy = NULL;
}
```

Notice the initialization.

Darken intentionally does **not** clear:

```c
update
destroy
tag
usr
```

when an entity is spawned.

The caller must initialize them.

This is important because the memory is recycled.

---

# Creating a Pool

Darken provides three related macros.

---

## Dynamic pool

```c
darken enemies = DARKEN_POOL_ALLOC(
    malloc,
    128,
    sizeof(Enemy)
);
```

This creates:

```text
pool    -> heap allocation
storage -> heap allocation
```

The two allocations must eventually be released:

```c
free(enemies.pool);
free(enemies.storage);
```

The advantage is runtime-sized allocation.

The disadvantage is that the pool uses the heap.

For a fixed-size game, static allocation is often preferable.

---

# Static Pool

A statically backed pool can be declared with:

```c
DARKEN_POOL_DECLARE(enemy_storage, 128, sizeof(Enemy));
```

Then:

```c
darken enemies = DARKEN_POOL_BIND(enemy_storage);
```

and:

```c
darken_init(&enemies);
```

No per-entity allocation occurs.

This is particularly attractive for embedded and retro targets.

---

# Compile-Time Initialization

For static/global contexts:

```c
DARKEN_POOL_DECLARE(enemy_storage, 128, sizeof(Enemy));

darken enemies = DARKEN_POOL_INIT(enemy_storage);
```

Then:

```c
void game_init(void)
{
    darken_init(&enemies);
}
```

This separates:

```text
storage declaration
```

from:

```text
runtime initialization
```

---

# Initialization

Initialization is:

```c
darken_init(&enemies);
```

Internally, Darken assigns every storage address to a pool slot.

Conceptually:

```text
pool:

0 -> storage + 0 * stride
1 -> storage + 1 * stride
2 -> storage + 2 * stride
3 -> storage + 3 * stride
...
```

It also establishes:

```text
size   = 0
paused = capacity
```

Therefore:

```text
[ FREE FREE FREE FREE FREE ]
```

Initially the active and paused zones are empty.

---

# Spawning Entities

Spawning is intentionally cheap:

```c
darken_entity entity = DARKEN_SPAWN(&enemies);
```

The macro essentially does:

```c
ctx->pool[ctx->size++]
```

provided:

```c
ctx->size < ctx->paused
```

Otherwise:

```c
DARKEN_SPAWN()
```

returns `0`.

A typical pattern is:

```c
darken_entity entity = DARKEN_SPAWN(&enemies);

if (!entity)
{
    /* Pool exhausted. */
    return;
}
```

---

# Pool Exhaustion

Suppose:

```c
capacity = 100;
size = 100;
paused = 100;
```

There are no free slots:

```text
[ ACTIVE x100 ][ FREE x0 ][ PAUSED x0 ]
```

Therefore:

```c
DARKEN_SPAWN(&ctx)
```

returns null.

This is intentional.

Darken does not:

* allocate more memory
* grow the pool
* silently discard another entity
* invoke a fallback allocator

The capacity is explicit and deterministic.

For a shmup this is useful because maximum resource consumption can be known ahead of time.

---

# Updating Entities

The main update function is:

```c
darken_update(&ctx);
```

It iterates through the active zone.

Conceptually:

```c
for each active entity
{
    execute update callback;

    interpret returned state;
}
```

Paused entities are not visited.

Free slots are not visited.

Therefore the amount of update work depends on:

```text
number of active entities
```

rather than:

```text
total capacity
```

This is an important performance characteristic.

---

# A Shmup Game Loop

A minimal game loop might look like:

```c
while (game_running)
{
    read_input();

    update_player();

    darken_update(&enemies);
    darken_update(&bullets);
    darken_update(&particles);

    resolve_collisions();

    render();
}
```

The entity system does not own the entire game loop.

It only manages entity lifecycle and callbacks.

This keeps Darken small and unopinionated.

---

# Deleting Entities

An entity can request deletion by returning:

```c
DARKEN_DELETE
```

For example:

```c
darken_state bullet_update(void *data)
{
    Bullet *b = data;

    b->x += b->vx;
    b->y += b->vy;

    if (b->x < 0 || b->x >= SCREEN_WIDTH ||
        b->y < 0 || b->y >= SCREEN_HEIGHT)
    {
        return DARKEN_DELETE;
    }

    return DARKEN_LOOP;
}
```

The next lifecycle stage removes it from the active zone.

If a destroy callback is active, it is called before the entity is recycled.

---

# Destroy Callbacks

A destroy callback can perform cleanup:

```c
darken_state enemy_destroy(void *data)
{
    Enemy *enemy = data;

    spawn_explosion(enemy->x, enemy->y);

    return DARKEN_LOOP;
}
```

The callback receives only:

```c
entity->data
```

rather than the entity itself.

If the destroy callback needs the entity, it can recover it using the payload-to-entity mechanism discussed later.

---

# Manual Deletion

The API also provides:

```c
darken_entity_delete(entity);
```

This is useful when the game itself decides that an entity must disappear.

For example:

```c
if (enemy->hp == 0)
{
    darken_entity_delete(entity);
}
```

The entity must belong to the context and must be in use.

---

# Pausing

An active entity can be paused:

```c
darken_entity_pause(entity);
```

or its update callback can return:

```c
DARKEN_PAUSE
```

The conceptual result is:

```text
Before:

[ ACTIVE ][ FREE ][ PAUSED ]


After:

[ ACTIVE ][ FREE ][ PAUSED ][ entity ]
```

The entity is excluded from normal updates.

---

# Why Pausing Is Different from Deletion

Deletion means:

```text
entity becomes reusable
```

Pausing means:

```text
entity remains alive but stops updating
```

This distinction is useful in a shmup.

For example:

```text
boss_intro
    |
    +-- enemies pause
    |
    +-- boss animation runs
    |
    +-- enemies resume
```

No entity data has to be recreated.

---

# Resuming

A paused entity can be resumed:

```c
darken_entity_resume(entity);
```

It is moved back into the active region.

Its payload remains unchanged.

For example:

```c
Enemy *boss = ...;

/* Game paused for a scripted event. */

darken_entity_pause(boss);

/* Later... */

darken_entity_resume(boss);
```

The same `Enemy` payload remains associated with the same entity.

---

# Resetting a Context

The entire context can be reset with:

```c
darken_reset(&enemies);
```

This destroys active entities and restores the pool to its initial state.

After reset:

```text
size   = 0
paused = capacity
```

The storage itself is not freed.

Therefore:

```c
darken_reset(&enemies);
```

is suitable for:

* restarting a level
* restarting a wave
* game-over cleanup
* loading another stage

without reallocating the pool.

---

# Iteration

Darken provides:

```c
DARKEN_FOREACH(CTX, CODE)
```

For example:

```c
DARKEN_FOREACH(&enemies,
{
    Enemy *enemy = (Enemy *)_entity->data;

    render_enemy(enemy);
});
```

The macro walks the active region.

It intentionally does not visit:

```text
free entities
paused entities
```

---

# Why Iteration Goes Backwards

The implementation starts at:

```c
ctx->size
```

and decrements.

Conceptually:

```text
size = 5

index:
4
3
2
1
0
```

This direction is important because deletion swaps the entity being removed with the last active entity.

For example:

```text
Before:

[ A ][ B ][ C ][ D ][ E ]
                    ^
                 delete
```

After:

```text
[ A ][ B ][ C ][ E ]
```

The deleted slot is filled by `E`.

Reverse iteration allows the newly moved entity to be handled without requiring a full array shift.

This is one of the reasons deletion can remain O(1).

---

# Recovering an Entity from Its Payload

Darken provides:

```c
#define DARKEN_DATA_GET_ENTITY(DATA) ...
```

The purpose is to recover the owning entity from:

```c
entity->data
```

This can be useful inside callbacks because callbacks receive only the payload.

For example:

```c
darken_state enemy_update(void *data)
{
    darken_entity entity = DARKEN_DATA_GET_ENTITY(data);

    Enemy *enemy = (Enemy *)data;

    ...
}
```

This creates a useful callback model:

```text
Darken
   |
   v
entity->update(entity->data)
                   |
                   v
              user payload
                   |
                   v
          recover entity if needed
```

However, see the portability warning in the [Pitfalls](#pitfalls) section.

---

# Building a Shmup

Let's build a small shmup architecture.

We will use four pools:

```text
Player
Enemies
Bullets
Particles
```

The architecture becomes:

```text
                  GAME
                   |
        +----------+----------+
        |          |          |
      player     enemies    bullets
                             |
                          particles
```

Each category has a fixed maximum.

For example:

```c
#define MAX_PLAYERS   1
#define MAX_ENEMIES  64
#define MAX_BULLETS 128
#define MAX_PARTICLES 256
```

This gives predictable memory consumption.

---

# Player Entity

Define:

```c
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t speed;
    uint16_t hp;
    uint16_t weapon;
} Player;
```

The update callback:

```c
darken_state player_update(void *data)
{
    Player *p = data;

    if (input_left())
        p->x -= p->speed;

    if (input_right())
        p->x += p->speed;

    if (input_up())
        p->y -= p->speed;

    if (input_down())
        p->y += p->speed;

    clamp_player_position(p);

    if (input_fire())
        fire_player_weapon(p);

    return DARKEN_LOOP;
}
```

Spawning the player:

```c
darken_entity entity = DARKEN_SPAWN(&players);

if (entity)
{
    Player *p = (Player *)entity->data;

    p->x = 160;
    p->y = 200;
    p->speed = 3;
    p->hp = 3;
    p->weapon = 0;

    entity->tag = TAG_PLAYER;
    entity->usr = 0;

    entity->update = player_update;
    entity->destroy = NULL;
}
```

---

# Enemy Entity

Define:

```c
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint16_t hp;
    uint16_t type;
} Enemy;
```

A simple enemy:

```c
darken_state enemy_update(void *data)
{
    Enemy *e = data;

    e->x += e->vx;
    e->y += e->vy;

    if (e->hp == 0)
        return DARKEN_DELETE;

    if (e->y > SCREEN_HEIGHT + 16)
        return DARKEN_DELETE;

    return DARKEN_LOOP;
}
```

---

# Spawning Enemies

A helper function can encapsulate initialization:

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
    e->vy = 1;
    e->hp = 3;
    e->type = 0;

    entity->tag = TAG_ENEMY;
    entity->usr = 0;
    entity->update = enemy_update;
    entity->destroy = enemy_destroy;
}
```

Now a wave generator can simply call:

```c
spawn_enemy(40, -20);
spawn_enemy(80, -40);
spawn_enemy(120, -60);
spawn_enemy(160, -80);
```

---

# Bullets

Bullets are usually much more numerous than enemies.

```c
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint16_t damage;
    uint16_t team;
} Bullet;
```

Update:

```c
darken_state bullet_update(void *data)
{
    Bullet *b = data;

    b->x += b->vx;
    b->y += b->vy;

    if (b->x < -8 ||
        b->x >= SCREEN_WIDTH + 8 ||
        b->y < -8 ||
        b->y >= SCREEN_HEIGHT + 8)
    {
        return DARKEN_DELETE;
    }

    return DARKEN_LOOP;
}
```

---

# Firing

A weapon can spawn a bullet without allocating anything:

```c
void spawn_bullet(int16_t x, int16_t y)
{
    darken_entity entity = DARKEN_SPAWN(&bullets);

    if (!entity)
        return;

    Bullet *b = (Bullet *)entity->data;

    b->x = x;
    b->y = y;
    b->vx = 0;
    b->vy = -6;
    b->damage = 1;
    b->team = TEAM_PLAYER;

    entity->tag = TAG_BULLET;
    entity->usr = 0;
    entity->update = bullet_update;
    entity->destroy = NULL;
}
```

This is exactly the sort of workload Darken is intended for:

```text
spawn
update
delete
spawn
update
delete
...
```

without heap churn.

---

# Collision Handling

Collision detection can use `DARKEN_FOREACH`.

For example:

```c
DARKEN_FOREACH(&bullets,
{
    Bullet *b = (Bullet *)_entity->data;

    DARKEN_FOREACH(&enemies,
    {
        Enemy *e = (Enemy *)_entity->data;

        if (bullet_hits_enemy(b, e))
        {
            e->hp -= b->damage;
            darken_entity_delete(
                DARKEN_DATA_GET_ENTITY(b)
            );

            break;
        }
    });
});
```

In a real game, collision detection should usually be optimized with spatial partitioning, grids, or another broad-phase system.

Darken deliberately does not attempt to solve that problem.

---

# Enemy Death

The enemy update can request deletion:

```c
darken_state enemy_update(void *data)
{
    Enemy *e = data;

    if (e->hp == 0)
        return DARKEN_DELETE;

    e->x += e->vx;
    e->y += e->vy;

    return DARKEN_LOOP;
}
```

The destroy callback can spawn an explosion:

```c
darken_state enemy_destroy(void *data)
{
    Enemy *e = data;

    spawn_explosion(e->x, e->y);

    return DARKEN_LOOP;
}
```

This gives a clean lifecycle:

```text
ACTIVE
   |
   | hp == 0
   v
DESTROY
   |
   +--> explosion
   |
   v
FREE
```

---

# Particle Pool

Particles are an excellent use case because they are:

* numerous
* short-lived
* homogeneous
* frequently spawned
* frequently destroyed

For example:

```c
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint16_t life;
} Particle;
```

Update:

```c
darken_state particle_update(void *data)
{
    Particle *p = data;

    p->x += p->vx;
    p->y += p->vy;

    if (--p->life == 0)
        return DARKEN_DELETE;

    return DARKEN_LOOP;
}
```

This produces a very cheap particle system.

---

# Enemy Waves

Suppose the game has:

```text
Wave 1
  |
  +-- 20 enemies
  |
  v
Wave 2
  |
  +-- 40 enemies
  |
  v
Boss
```

The enemy pool can remain allocated for the entire game.

At the end of a wave:

```c
darken_reset(&enemies);
```

Then spawn the next wave.

No memory reallocation is necessary.

---

# Pausing a Whole Game

One approach is simply not to call:

```c
darken_update()
```

while the game is paused.

For example:

```c
if (!game_paused)
{
    darken_update(&enemies);
    darken_update(&bullets);
    darken_update(&particles);
}
```

This is usually simpler than moving every entity into the paused zone.

The pause/resume functionality inside Darken is better suited to **individual entities** or subsets of entities.

---

# Individual Entity Pause

Suppose a boss should stop moving during a dialogue:

```c
darken_entity_pause(boss);
```

The boss remains alive.

Its payload remains valid.

Its memory remains allocated.

Its pointer remains usable.

Then:

```c
darken_entity_resume(boss);
```

returns it to active processing.

---

# State Machines

Darken's callback return mechanism makes state machines compact.

For example:

```c
darken_state boss_enter(void *data)
{
    Boss *boss = data;

    boss->y++;

    if (boss->y >= 40)
        return boss_attack;

    return DARKEN_LOOP;
}
```

Then:

```c
darken_state boss_attack(void *data)
{
    Boss *boss = data;

    update_boss_attack(boss);

    return DARKEN_LOOP;
}
```

And:

```c
darken_state boss_dying(void *data)
{
    Boss *boss = data;

    boss->animation++;

    if (boss->animation >= 30)
        return DARKEN_DELETE;

    return DARKEN_LOOP;
}
```

This creates:

```text
boss_enter
     |
     | reached position
     v
boss_attack
     |
     | HP == 0
     v
boss_dying
     |
     | animation finished
     v
DARKEN_DELETE
```

The callback pointer itself is the state.

---

# The `tag` Field

A tag can identify the broad category of an entity.

For example:

```c
#define TAG_PLAYER    0x00000001
#define TAG_ENEMY     0x00000002
#define TAG_BULLET    0x00000003
#define TAG_PARTICLE  0x00000004
```

Then:

```c
if (_entity->tag == TAG_ENEMY)
{
    ...
}
```

However, if different payload types already have separate pools, tags are not always necessary.

A tag is most useful for generic systems that need lightweight classification.

---

# The `usr` Field

`usr` is a small application-defined value.

Examples:

```text
enemy type
weapon number
sprite index
team
animation state
subtype
owner ID
```

For a bullet:

```c
entity->usr = WEAPON_SPREAD;
```

For an enemy:

```c
entity->usr = ENEMY_FORMATION_2;
```

It is intentionally not interpreted by Darken.

---

# Complexity

Darken's operations are designed around constant-time operations.

| Operation           | Expected complexity |
| ------------------- | ------------------: |
| Spawn               |                O(1) |
| Delete              |                O(1) |
| Pause               |                O(1) |
| Resume              |                O(1) |
| Update traversal    |  O(active entities) |
| Reset               |  O(active entities) |
| Pool initialization |         O(capacity) |

The important property is that deletion does **not** require shifting an array.

Instead:

```text
swap with boundary
```

is used.

---

# Why O(1) Deletion Matters in a Shmup

A shmup can easily perform:

```text
hundreds of bullet spawns
hundreds of bullet deletions
dozens of enemy deletions
many particle deletions
```

every second.

A naïve packed array might do:

```text
delete entity
    |
    +-- shift everything after it
```

Darken instead does:

```text
delete entity
    |
    +-- swap with boundary
```

This is particularly effective for unordered collections.

The trade-off is that entity ordering is not stable.

---

# Strengths

## 1. Stable entity addresses

This is probably the strongest architectural feature.

The entity's physical address never changes after initialization.

That makes raw pointers into payloads significantly safer than in a moving/compacting entity array.

---

## 2. No per-entity allocation

There is no:

```c
malloc()
```

per enemy.

There is no:

```c
free()
```

per bullet.

The pool is allocated once.

This eliminates allocator fragmentation from entity churn.

---

## 3. Deterministic memory usage

If:

```c
MAX_ENEMIES = 64
```

then the maximum number of enemy objects is known.

This is valuable for:

* embedded systems
* consoles
* retro hardware
* deterministic game loops
* memory-constrained systems

---

## 4. O(1) spawning

Spawning is essentially:

```c
pool[size++]
```

There is no search through the pool for a free object.

---

## 5. O(1) deletion

Deletion uses swaps rather than compaction.

This makes high-churn object types particularly cheap.

---

## 6. Cache-friendly payload storage

Entities of one type are contiguous:

```text
Entity
Entity
Entity
Entity
Entity
...
```

This is much more predictable than independently allocated objects.

---

## 7. Small API

The public API is deliberately tiny:

```c
darken_init()
darken_update()
darken_reset()

darken_entity_run()
darken_entity_update()
darken_entity_pause()
darken_entity_resume()
darken_entity_delete()
```

plus the pool and iteration macros.

There is relatively little machinery to learn.

---

## 8. State machines are naturally represented

The callback pointer doubles as the entity's state.

This is elegant for game logic such as:

```text
spawn
enter
attack
hurt
dying
dead
```

---

## 9. No imposed renderer, physics engine, or allocator

Darken only manages entities.

It does not care whether the game uses:

* software rendering
* OpenGL
* SDL
* a custom framebuffer
* a 68000 graphics system
* a console API
* an embedded display

That keeps the library focused.

---

# Limitations

## 1. One payload type per pool

A pool is homogeneous.

You cannot naturally put:

```text
Enemy
Bullet
Particle
Boss
```

into the same pool if they have different payload sizes.

The intended solution is multiple pools.

---

## 2. Entity ordering is unstable

Because deletion and lifecycle transitions use swaps:

```text
[A][B][C][D]
```

may become:

```text
[A][D][C]
```

after deleting `B`.

Therefore never rely on pool order as a persistent entity order.

If deterministic ordering matters, store an explicit ordering value.

---

## 3. No automatic initialization

Darken deliberately does not clear:

```c
update
destroy
tag
usr
data
```

when spawning an entity.

This is fast, but dangerous.

A recycled entity may contain stale state.

Always fully initialize the payload and lifecycle metadata after spawning.

---

## 4. Fixed capacity

A pool cannot grow automatically.

If the enemy pool has 64 slots:

```c
DARKEN_SPAWN(&enemies)
```

will eventually return null.

The application must decide what to do.

---

## 5. GNU C dependency

The implementation uses GNU extensions including:

```c
__attribute__
```

and statement expressions:

```c
({
    ...
})
```

Therefore this is not strictly portable ISO C99/C11.

It is intentionally closer to:

```text
GCC / Clang + low-level targets
```

than to strict portable C.

---

## 6. 16-bit limits

The context uses:

```c
uint16_t
```

for:

```text
capacity
size
paused
stride
slot
```

Therefore the design is inherently limited to values representable by 16 bits.

In practice, `stride` is particularly important.

A payload whose aligned entity stride exceeds `65535` cannot be represented correctly by the current `uint16_t stride`.

---

## 7. No thread safety

Darken assumes a single owner/thread.

Do not concurrently:

```text
spawn
delete
pause
resume
update
```

from multiple threads without external synchronization.

---

## 8. No generation counters

An entity pointer remains valid as a pointer to the storage address, but the **logical object occupying that slot can be recycled** after deletion.

For example:

```text
enemy A
   |
 delete
   |
   v
same storage
   |
 spawn
   |
   v
enemy B
```

A stale pointer may therefore still point to valid memory, but that memory may now represent another logical entity.

This is a classic object-pool lifetime hazard.

Stable addresses do not mean stable object identity.

---

# Pitfalls

## Pitfall 1 — Reusing stale entity state

This is probably the easiest mistake to make.

Suppose an old bullet had:

```c
entity->update = bullet_update;
entity->tag = TAG_BULLET;
entity->usr = 4;
```

It gets deleted.

Later the same slot is reused for another object.

Darken does not clear those fields.

Therefore:

```c
darken_entity entity = DARKEN_SPAWN(&pool);
```

must always be followed by complete initialization.

Bad:

```c
darken_entity entity = DARKEN_SPAWN(&pool);

Enemy *e = (Enemy *)entity->data;

e->x = 100;
e->y = 20;
```

Better:

```c
darken_entity entity = DARKEN_SPAWN(&pool);

if (entity)
{
    Enemy *e = (Enemy *)entity->data;

    memset(e, 0, sizeof(*e));

    e->x = 100;
    e->y = 20;
    e->hp = 3;

    entity->tag = TAG_ENEMY;
    entity->usr = 0;
    entity->update = enemy_update;
    entity->destroy = enemy_destroy;
}
```

Whether `memset()` is appropriate depends on the payload.

The important rule is:

> **Never assume a newly spawned entity contains zeroed data.**

---

# Pitfall 2 — Stale logical pointers

Consider:

```c
darken_entity enemy = DARKEN_SPAWN(&enemies);
```

Then:

```c
darken_entity_delete(enemy);
```

After deletion, `enemy` should no longer be treated as a live entity.

Even though its memory still exists, the slot can be reused immediately.

This is especially important with:

```c
boss
target
owner
parent
victim
```

pointers.

If another entity references a deleted entity, the reference must be invalidated or otherwise verified.

---

# Pitfall 3 — Pointer stability is not identity stability

This distinction is crucial.

Darken guarantees:

```text
same storage address
```

It does not guarantee:

```text
same logical entity forever
```

A storage address can eventually become:

```text
Enemy A
   |
 delete
   |
 Enemy B
```

Therefore a raw pointer is suitable for short-lived relationships, but not automatically for persistent references across arbitrary deletion/reuse.

---

# Pitfall 4 — Pool order must not be used as an ID

This is unsafe:

```c
uint16_t enemy_id = entity->slot;
```

and then later assuming:

```c
pool[enemy_id]
```

still refers to that entity.

`slot` changes whenever the entity's pointer moves within the pool.

`slot` is a **management position**, not a persistent identifier.

---

# Pitfall 5 — `slot` is mutable

The field:

```c
entity->slot
```

must never be treated as permanent identity.

It is maintained internally by Darken.

After a swap:

```text
entity->slot
```

changes.

---

# Pitfall 6 — Nested modification during iteration

Darken allows modifications during iteration, but the caller must understand that the pool is being reordered.

For example:

```c
DARKEN_FOREACH(&enemies,
{
    if (should_delete(_entity))
        darken_entity_delete(_entity);
});
```

This can be useful, but code should not assume the pool remains in the same order while the loop executes.

The safest mental model is:

> Iteration visits the active collection, not a stable snapshot.

---

# Pitfall 7 — Do not cache `slot`

This is wrong:

```c
uint16_t slot = entity->slot;

/* Something modifies the pool. */

use(pool[slot]);
```

The entity may have moved.

Use the entity pointer itself if you need to continue referring to that entity.

---

# Important Implementation Issues

The current development version has several areas that deserve attention before calling the implementation production-ready.

These are not merely stylistic concerns.

---

## Issue 1 — `DARKEN_PAUSE()` does not currently decrement `size`

The intended layout is:

```text
[ ACTIVE ][ FREE ][ PAUSED ]
```

Suppose:

```text
size = 5
paused = 8
```

and active entity `E` is paused.

The correct operation must ultimately produce:

```text
size = 4
paused = 7
```

because one entity leaves the active zone and enters the paused zone.

However, the current macro:

```c
#define _DARKEN_PAUSE(ENTITY) _DARKEN_BLOCK(                                \
    _darken_swap(ENTITY->owner->pool, ENTITY->slot, --ENTITY->owner->size); \
    _darken_swap(ENTITY->owner->pool, ENTITY->slot, --ENTITY->owner->paused);)
```

decrements `paused` but does not actually decrement `size`.

That breaks the fundamental invariant.

The implementation should be reviewed here before relying on pause/resume heavily.

The intended algorithm is approximately:

```text
1. swap entity with last active element
2. decrement size
3. move that entity to the end of the paused zone
4. decrement paused
```

The exact implementation should preserve:

```text
0 <= size <= paused <= capacity
```

after every operation.

---

## Issue 2 — State control values use function-pointer values

The callback type is:

```c
typedef void *(*darken_state)();
```

while control values are:

```c
#define DARKEN_DELETE ((void *)0)
#define DARKEN_LOOP   ((void *)1)
#define DARKEN_PAUSE  ((void *)2)
```

The implementation then compares the function pointer against values corresponding to:

```text
0
1
2
```

This is highly implementation-specific.

A function pointer and an object pointer are not guaranteed by ISO C to have the same representation or semantics.

For a deliberately GNU/68000-oriented library this may be acceptable as an implementation choice, but it should be documented as such.

A more strictly portable design would separate:

```text
callback
```

from:

```text
control result
```

or use a tagged return type.

That would, however, make the API more verbose.

---

## Issue 3 — `DARKEN_DATA_GET_ENTITY()` is highly non-portable

The macro:

```c
#define DARKEN_DATA_GET_ENTITY(DATA) \
    ((darken_entity)((uint8_t *)(DATA) - (uint32_t)&((darken_entity)0)->data))
```

contains a strong 32-bit assumption:

```c
(uint32_t)
```

On a system where pointers are larger than 32 bits, this can truncate the address.

It is therefore not suitable as a generally portable pointer-recovery mechanism.

The underlying concept is valid:

```text
payload address
      -
payload offset
      =
entity address
```

but the implementation needs to match the actual target ABI.

For a 68000-focused implementation this may be an intentional constraint.

For a modern 64-bit build, it deserves redesign.

---

## Issue 4 — Flexible-array offset calculation

The implementation intentionally avoids using a conventional `offsetof()` expression and computes the payload displacement through the existing structure representation.

That is consistent with the low-level philosophy of the library, but it means the code relies more heavily on compiler/layout assumptions.

The documentation should clearly state the supported compiler/ABI assumptions.

---

## Issue 5 — `stride` is `uint16_t`

The context stores:

```c
uint16_t stride;
```

Therefore:

```text
entity header + payload + padding
```

must fit within 65535 bytes.

For the intended game workloads this is probably fine.

Still, it is a hard limitation.

---

## Issue 6 — `capacity` is also `uint16_t`

The maximum number of entities is limited by:

```c
uint16_t
```

and by the representable pool size.

Again, this is reasonable for the target use case, but it should be explicit.

---

# Safe Usage Rules

For production code, the following rules are recommended.

## Rule 1

Always check the result of:

```c
DARKEN_SPAWN()
```

Example:

```c
darken_entity e = DARKEN_SPAWN(&enemies);

if (!e)
    return;
```

---

## Rule 2

Fully initialize every spawned entity.

At minimum:

```c
entity->update
entity->destroy
entity->tag
entity->usr
```

and the entire payload.

---

## Rule 3

Never use `slot` as a persistent ID.

---

## Rule 4

Do not use an entity pointer after deleting the entity.

---

## Rule 5

Do not assume pool order is stable.

---

## Rule 6

Treat payload pointers as invalid after logical entity deletion if the storage can be reused.

---

## Rule 7

Keep `capacity`, `size`, `paused`, and `stride` internally consistent.

---

## Rule 8

Do not modify the pool directly.

The pool is managed by Darken.

Use:

```c
DARKEN_SPAWN()
darken_entity_delete()
darken_entity_pause()
darken_entity_resume()
```

rather than manually moving entries.

---

# When Darken Is a Good Fit

Darken works particularly well for:

### Shoot-'em-ups

```text
hundreds of bullets
dozens of enemies
many particles
frequent spawning/deletion
```

### Particle systems

```text
spawn -> update -> die
```

### Enemy managers

```text
fixed maximum
simple lifecycle
homogeneous payload
```

### Embedded games

Where:

```text
heap allocation
fragmentation
unpredictable memory usage
```

are undesirable.

### Retro systems

The design is particularly compatible with systems where:

```text
memory is scarce
pointer chasing should be controlled
16-bit arithmetic is desirable
```

---

# When Darken Is Not a Good Fit

Darken is probably not appropriate if you need:

## Arbitrary heterogeneous entities

If one collection needs:

```text
Enemy
Weapon
UIObject
Particle
PhysicsBody
```

with unrelated layouts, multiple Darken pools may become cumbersome.

---

## Stable ordering

If entities must remain:

```text
sorted
ordered
indexed
```

then swap-based deletion is not ideal.

---

## Automatic dynamic growth

Darken deliberately uses fixed capacity.

If you need:

```c
spawn()
```

to automatically grow from:

```text
64 -> 128 -> 256 -> ...
```

you need a different allocator strategy.

---

## Multithreaded mutation

Darken is fundamentally a single-owner data structure.

A multithreaded ECS requires additional synchronization and often a different architecture.

---

# Complete Shmup Skeleton

The following combines the concepts into a small architecture.

```c
#include "darken.h"

#define MAX_ENEMIES   64
#define MAX_BULLETS  128
#define MAX_PARTICLES 256

#define TAG_PLAYER    1
#define TAG_ENEMY     2
#define TAG_BULLET    3
#define TAG_PARTICLE  4

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t speed;
    uint16_t hp;
} Player;

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint16_t hp;
    uint16_t type;
} Enemy;

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint16_t damage;
    uint16_t team;
} Bullet;

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint16_t life;
} Particle;


/* --------------------------------------------------------------------------
 * Pools
 * -------------------------------------------------------------------------- */

DARKEN_POOL_DECLARE(enemy_storage,
                    MAX_ENEMIES,
                    sizeof(Enemy));

DARKEN_POOL_DECLARE(bullet_storage,
                    MAX_BULLETS,
                    sizeof(Bullet));

DARKEN_POOL_DECLARE(particle_storage,
                    MAX_PARTICLES,
                    sizeof(Particle));

darken enemies =
    DARKEN_POOL_INIT(enemy_storage);

darken bullets =
    DARKEN_POOL_INIT(bullet_storage);

darken particles =
    DARKEN_POOL_INIT(particle_storage);


/* --------------------------------------------------------------------------
 * Enemy
 * -------------------------------------------------------------------------- */

darken_state enemy_destroy(void *data)
{
    Enemy *enemy = data;

    spawn_explosion(enemy->x, enemy->y);

    return DARKEN_LOOP;
}

darken_state enemy_update(void *data)
{
    Enemy *enemy = data;

    enemy->x += enemy->vx;
    enemy->y += enemy->vy;

    if (enemy->hp == 0)
        return DARKEN_DELETE;

    if (enemy->y > SCREEN_HEIGHT + 16)
        return DARKEN_DELETE;

    return DARKEN_LOOP;
}


/* --------------------------------------------------------------------------
 * Bullet
 * -------------------------------------------------------------------------- */

darken_state bullet_update(void *data)
{
    Bullet *bullet = data;

    bullet->x += bullet->vx;
    bullet->y += bullet->vy;

    if (bullet->x < -8 ||
        bullet->x >= SCREEN_WIDTH + 8 ||
        bullet->y < -8 ||
        bullet->y >= SCREEN_HEIGHT + 8)
    {
        return DARKEN_DELETE;
    }

    return DARKEN_LOOP;
}


/* --------------------------------------------------------------------------
 * Particle
 * -------------------------------------------------------------------------- */

darken_state particle_update(void *data)
{
    Particle *particle = data;

    particle->x += particle->vx;
    particle->y += particle->vy;

    if (--particle->life == 0)
        return DARKEN_DELETE;

    return DARKEN_LOOP;
}


/* --------------------------------------------------------------------------
 * Spawn helpers
 * -------------------------------------------------------------------------- */

void spawn_enemy(int16_t x, int16_t y)
{
    darken_entity entity = DARKEN_SPAWN(&enemies);

    if (!entity)
        return;

    Enemy *enemy = (Enemy *)entity->data;

    enemy->x = x;
    enemy->y = y;
    enemy->vx = 0;
    enemy->vy = 1;
    enemy->hp = 3;
    enemy->type = 0;

    entity->tag = TAG_ENEMY;
    entity->usr = 0;
    entity->update = enemy_update;
    entity->destroy = enemy_destroy;
}

void spawn_bullet(int16_t x, int16_t y)
{
    darken_entity entity = DARKEN_SPAWN(&bullets);

    if (!entity)
        return;

    Bullet *bullet = (Bullet *)entity->data;

    bullet->x = x;
    bullet->y = y;
    bullet->vx = 0;
    bullet->vy = -6;
    bullet->damage = 1;
    bullet->team = 0;

    entity->tag = TAG_BULLET;
    entity->usr = 0;
    entity->update = bullet_update;
    entity->destroy = NULL;
}


/* --------------------------------------------------------------------------
 * Game initialization
 * -------------------------------------------------------------------------- */

void game_init(void)
{
    darken_init(&enemies);
    darken_init(&bullets);
    darken_init(&particles);

    spawn_enemy(40,  -20);
    spawn_enemy(80,  -40);
    spawn_enemy(120, -60);
    spawn_enemy(160, -80);
}


/* --------------------------------------------------------------------------
 * Game update
 * -------------------------------------------------------------------------- */

void game_update(void)
{
    darken_update(&enemies);
    darken_update(&bullets);
    darken_update(&particles);

    resolve_collisions();
}


/* --------------------------------------------------------------------------
 * Rendering
 * -------------------------------------------------------------------------- */

void game_render(void)
{
    DARKEN_FOREACH(&enemies,
    {
        Enemy *enemy = (Enemy *)_entity->data;

        draw_enemy(enemy);
    });

    DARKEN_FOREACH(&bullets,
    {
        Bullet *bullet = (Bullet *)_entity->data;

        draw_bullet(bullet);
    });

    DARKEN_FOREACH(&particles,
    {
        Particle *particle = (Particle *)_entity->data;

        draw_particle(particle);
    });
}
```

This is the basic Darken architecture:

```text
                    GAME LOOP
                        |
              +---------+---------+
              |         |         |
           enemies   bullets   particles
              |         |         |
              v         v         v
           update     update     update
              |         |         |
              +---------+---------+
                        |
                    collision
                        |
                     render
```

---

# Architectural Summary

A complete Darken-based game can be thought of as:

```text
                    +----------------+
                    |      GAME      |
                    +--------+-------+
                             |
              +--------------+--------------+
              |              |              |
              v              v              v
          ENEMIES         BULLETS        PARTICLES
              |              |              |
         +----+----+    +----+----+    +----+----+
         | update |    | update |    | update |
         +----+----+    +----+----+    +----+----+
              |              |              |
              +--------------+--------------+
                             |
                        COLLISIONS
                             |
                           RENDER
```

Each pool has:

```text
fixed capacity
fixed payload size
fixed storage
O(1) spawn
O(1) delete
```

and each entity has:

```text
stable address
mutable lifecycle state
user payload
generic metadata
```

---

# The Most Important Concept

If there is only one thing to remember about Darken, it is this:

```text
             ENTITY MEMORY
                   |
                   | never moves
                   v
        +-----------------------+
        | entity + payload      |
        +-----------------------+
                   ^
                   |
                   |
        +-----------------------+
        | pool[]                |
        |                       |
        | pointers move here    |
        +-----------------------+
```

Darken moves **references**, not objects.

That single decision gives the library most of its useful properties:

* O(1) deletion
* O(1) pause/resume
* cheap spawning
* no entity copying
* stable payload addresses
* predictable memory usage

The price is:

* unstable ordering
* fixed capacity
* explicit initialization
* logical lifetime hazards
* compiler/ABI dependence

---

# Final Assessment

## Strong points

The architecture is particularly strong for its intended workload.

The best design decisions are:

1. **Separate storage from the management pool.**
2. **Never move entity objects.**
3. **Use a three-zone pointer array.**
4. **Use boundary swaps instead of shifting.**
5. **Use fixed-size homogeneous payloads.**
6. **Avoid per-entity allocation.**
7. **Represent state transitions through callbacks.**
8. **Keep the public API very small.**

For a fixed-capacity shmup entity system, this is a very sensible architecture.

---

## Weak points

The main weaknesses are not architectural; they are mostly around robustness and portability:

1. GNU C extensions are required.
2. Function-pointer control values are ABI-specific.
3. `DARKEN_DATA_GET_ENTITY()` assumes a 32-bit address representation.
4. `uint16_t stride` imposes a hard payload-size limit.
5. `slot` is not a persistent identifier.
6. Entity memory is recycled without automatic clearing.
7. Entity ordering is unstable.
8. There is no protection against stale logical entity references.
9. Pause/resume logic must rigorously preserve the three-zone invariant.

---

# Recommended Invariants

A future version should treat these as explicit internal invariants:

```text
0 <= size <= paused <= capacity
```

For every active entity:

```text
entity->slot < size
```

For every paused entity:

```text
entity->slot >= paused
```

For every free pool position:

```text
size <= slot < paused
```

Every pool entry must point to an entity whose:

```c
entity->slot
```

matches its actual pool position.

These invariants are the heart of Darken.

If they remain true, the rest of the system becomes comparatively simple.

---

# Recommended Testing Strategy

Before relying on Darken in a game, stress-test the following sequences:

```text
spawn -> delete
spawn -> pause -> resume
spawn -> pause -> delete
spawn -> delete -> spawn
spawn many -> delete many
pause many -> resume many
reset while active
reset after paused entities
delete first active
delete last active
delete middle active
delete first paused
delete last paused
```

Especially test:

```text
size
paused
slot
```

after every operation.

A debug build should ideally validate:

```c
0 <= size
size <= paused
paused <= capacity
```

and verify every pool position's `entity->slot`.

---

# Conclusion

Darken is a **small, explicit, low-level entity pool**, not a framework trying to solve every aspect of game architecture.

Its core model is:

```text
fixed storage
     +
pointer pool
     +
three lifecycle zones
     +
callback state machine
```

This makes it particularly attractive for games such as shmups, where thousands of lifecycle operations can occur while the maximum number of objects is known in advance.

The central trade-off is deliberate:

> **Give up generality and stable ordering in exchange for predictable memory usage, stable object addresses, and extremely cheap lifecycle operations.**

That is a good trade for the class of problems Darken is targeting.

Before moving from `2.0.0-dev` toward a stable release, however, the implementation should resolve the lifecycle invariants — especially the pause transition — and clearly define the compiler/ABI assumptions around callback control values and payload-to-entity pointer recovery.
