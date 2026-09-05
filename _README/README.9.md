# Darken

**DARKula ENgine — Entity System**

`darken-2.0.0-dev`

Darken is a small C entity system for games where entities are created and destroyed constantly, but their maximum number is known in advance.

It was designed with games such as **shoot-'em-ups** in mind:

```text
        PLAYER
          |
   +------+------+ 
   |      |      |
 BULLETS ENEMIES PARTICLES
   |      |      |
   +------+------+ 
          |
       GAME LOOP
```

The idea is not to build a complete ECS.

Darken does one thing:

> **It manages a fixed set of entities and their lifetimes without moving the entities themselves.**

That decision defines almost everything in the library.

---

## The idea

A traditional dynamic object system might do this:

```c
Enemy *enemy = malloc(sizeof(Enemy));
...
free(enemy);
```

for every enemy.

Darken allocates the memory once:

```text
+------+------+------+------+------+
| E0   | E1   | E2   | E3   | E4   |
+------+------+------+------+------+
```

and maintains a separate array of pointers:

```text
+----+----+----+----+----+
| E3 | E0 | E4 | E1 | E2 |
+----+----+----+----+----+
```

The pointers can move.

The entities cannot.

This means that:

```c
Enemy *enemy = (Enemy *)entity->data;
```

continues to point to the same memory when the entity is deleted, paused, resumed, or reordered **as long as that entity itself remains alive**.

That is the key property of Darken.

---

# A Shmup Example

Imagine a game with:

```c
#define MAX_ENEMIES   64
#define MAX_BULLETS  128
#define MAX_PARTICLES 256
```

We can create one Darken pool for each type.

```c
typedef struct
{
    int16_t x, y;
    int16_t vx, vy;
    uint16_t hp;
} Enemy;
```

Then:

```c
DARKEN_POOL_DECLARE(
    enemy_storage,
    MAX_ENEMIES,
    sizeof(Enemy)
);

darken enemies = DARKEN_POOL_INIT(enemy_storage);
```

Initialize it once:

```c
darken_init(&enemies);
```

There are now 64 available enemy objects, but none are active.

No enemy has been individually allocated.

---

# Spawning

When the game needs an enemy:

```c
darken_entity entity = DARKEN_SPAWN(&enemies);
```

If there is no free slot, the result is `0`.

Otherwise, initialize the entity:

```c
if (entity)
{
    Enemy *enemy = (Enemy *)entity->data;

    enemy->x  = 120;
    enemy->y  = -16;
    enemy->vx = 0;
    enemy->vy = 2;
    enemy->hp = 3;

    entity->update = enemy_update;
    entity->destroy = enemy_destroy;
    entity->tag = TAG_ENEMY;
    entity->usr = 0;
}
```

This is an important characteristic of Darken:

**spawning does not initialize the entity for you.**

The memory may have belonged to another entity previously.

That is deliberate.

It avoids clearing memory on every spawn, but puts initialization responsibility on the caller.

---

# The Entity Is the Payload

Darken does not try to define what an enemy, bullet or player looks like.

You do:

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

Darken simply provides the memory:

```text
+-----------------------+
| Darken entity header  |
+-----------------------+
| Enemy                 |
|                       |
| x y vx vy hp          |
+-----------------------+
```

The payload is available through:

```c
entity->data
```

or the convenience macro:

```c
DARKEN_DATA(Enemy, enemy, entity);
```

---

# Updating

The game loop can simply do:

```c
darken_update(&enemies);
```

Darken walks the active entities and calls their `update` callbacks.

An enemy might look like:

```c
darken_state enemy_update(void *data)
{
    Enemy *enemy = data;

    enemy->x += enemy->vx;
    enemy->y += enemy->vy;

    if (enemy->hp == 0)
        return DARKEN_DELETE;

    if (enemy->y > SCREEN_HEIGHT)
        return DARKEN_DELETE;

    return DARKEN_LOOP;
}
```

The callback therefore contains both the behavior and the state transition.

---

# State Is a Callback

An entity does not need a separate state machine unless your game wants one.

The callback itself can be the state.

For example:

```text
enemy_enter
     |
     v
enemy_attack
     |
     v
enemy_hurt
     |
     v
enemy_dying
     |
     v
DARKEN_DELETE
```

A state can return another state:

```c
darken_state enemy_enter(void *data)
{
    Enemy *enemy = data;

    enemy->y++;

    if (enemy->y >= 40)
        return enemy_attack;

    return DARKEN_LOOP;
}
```

Darken then stores the returned callback as the entity's next update function.

This is a particularly nice fit for game objects with simple state machines.

---

# Three Possible Outcomes

An update callback can return:

```c
DARKEN_LOOP
DARKEN_PAUSE
DARKEN_DELETE
```

### Continue

```c
return DARKEN_LOOP;
```

Keep updating normally.

### Pause

```c
return DARKEN_PAUSE;
```

Remove the entity from the active update set without destroying it.

### Delete

```c
return DARKEN_DELETE;
```

Run its destruction path and recycle its slot.

---

# Active, Free and Paused

The pool is logically divided into three regions:

```text
+----------------+----------------+----------------+
|     ACTIVE     |      FREE      |     PAUSED     |
+----------------+----------------+----------------+
0              size            paused         capacity
```

This is what makes the lifecycle operations cheap.

Active entities are at the beginning.

Free slots are in the middle.

Paused entities are at the end.

Darken only needs to move pointers between these boundaries.

It does not move the actual entity data.

---

# Why This Matters

Suppose we have:

```text
ACTIVE

[A] [B] [C] [D] [E]
```

and `C` is deleted.

Darken does not shift:

```text
[D] [E]
```

to fill the gap.

Instead it swaps pointers:

```text
[A] [B] [E] [D]
```

and reduces the active range.

Deletion is therefore O(1).

This is exactly what we want for bullets:

```text
fire
  ↓
spawn
  ↓
update
  ↓
leave screen
  ↓
delete
  ↓
reuse slot
```

A shmup can perform this thousands of times without performing thousands of heap allocations.

---

# Pausing Without Moving the Entity

Suppose a boss is temporarily frozen:

```c
darken_entity_pause(boss);
```

The boss leaves the active region.

Its entity memory does not move.

Later:

```c
darken_entity_resume(boss);
```

returns it to the active region.

This makes pause/resume fundamentally different from deletion:

```text
DELETE → object becomes reusable

PAUSE  → object remains alive
```

---

# Iterating

Rendering can use:

```c
DARKEN_FOREACH(&enemies,
{
    Enemy *enemy = (Enemy *)_entity->data;

    draw_enemy(enemy);
});
```

Only active entities are visited.

A particle system therefore becomes very simple:

```c
darken_update(&particles);

DARKEN_FOREACH(&particles,
{
    Particle *p = (Particle *)_entity->data;
    draw_particle(p);
});
```

---

# Multiple Pools

Darken intentionally works best with several homogeneous pools:

```text
enemies   → Enemy
bullets   → Bullet
particles → Particle
```

rather than one pool containing unrelated structures.

For a shmup:

```c
darken_update(&enemies);
darken_update(&bullets);
darken_update(&particles);
```

Each pool can have a capacity appropriate to its workload.

For example:

```text
Enemies      64
Bullets     128
Particles   256
```

The result is predictable memory usage.

---

# Resetting

When the player starts another game or level:

```c
darken_reset(&enemies);
darken_reset(&bullets);
darken_reset(&particles);
```

The pools are emptied without freeing their memory.

The same allocated storage can immediately be reused.

This is useful for:

* restarting a level
* changing waves
* game over
* restarting a run

---

# Dynamic or Static?

Darken supports both.

For dynamically allocated storage:

```c
darken enemies = DARKEN_POOL_ALLOC(
    malloc,
    64,
    sizeof(Enemy)
);
```

For fixed storage:

```c
DARKEN_POOL_DECLARE(
    enemy_storage,
    64,
    sizeof(Enemy)
);

darken enemies = DARKEN_POOL_INIT(enemy_storage);
```

Static storage is particularly appropriate when the game knows its maximum object counts in advance.

Dynamic allocation is useful when those capacities are determined at runtime.

The entity system itself does not care where the storage came from.

---

# The Small Public API

The core API is intentionally small:

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

with the pool and iteration macros:

```c
DARKEN_POOL_ALLOC()
DARKEN_POOL_DECLARE()
DARKEN_POOL_INIT()
DARKEN_POOL_BIND()

DARKEN_SPAWN()
DARKEN_FOREACH()

DARKEN_DATA()
DARKEN_DATA_GET_ENTITY()
```

There is no renderer, physics system, allocator framework, component registry or scheduler.

Darken manages entities.

The game remains in control of everything else.

---

# What Darken Does Well

Darken is particularly good when:

* the maximum number of objects is known
* objects are frequently created and destroyed
* objects have homogeneous data
* deterministic memory usage matters
* heap allocation per object is undesirable
* object ordering does not matter
* stable payload addresses are useful

A shmup is almost a textbook example:

```text
          spawn      update       delete
Bullets  ─────────────────────────────────
Enemies  ─────────────────────────────────
Particles─────────────────────────────────
```

---

# What Darken Deliberately Does Not Do

Darken is not trying to be:

* a general ECS
* a dynamic container
* a type-erased object system
* a physics engine
* a renderer
* a garbage collector
* a multithreaded entity database

It gives you a fixed pool and lifecycle management.

Your game decides what the entities actually mean.

---

# Important Caveats

## Entity order is not stable

Because Darken uses swaps, deleting an entity can change the order of the active pool.

Do not use:

```c
entity->slot
```

as a permanent entity ID.

`slot` is an internal pool position and can change.

---

## Recycled entities are not cleared

After:

```c
darken_entity_delete(entity);
```

the storage can later be reused.

Therefore a newly spawned entity must be initialized completely.

---

## Stable address does not mean stable identity

This:

```c
Enemy *enemy = (Enemy *)entity->data;
```

remains valid while that entity exists.

After deletion, however, the same memory can eventually belong to another entity.

So:

```text
stable memory address
        ≠
stable entity identity
```

---

## Capacity is fixed

If the pool is full:

```c
DARKEN_SPAWN(&enemies)
```

returns `0`.

There is no automatic growth.

For games this is often a feature rather than a limitation: maximum memory consumption is known beforehand.

---

## Low-level by design

Darken uses GNU C features such as statement expressions and `__attribute__`.

It also uses 16-bit fields and assumes a low-level memory model compatible with its intended targets, including Motorola 68000 systems.

It should therefore be regarded as a **targeted low-level C library**, not a strictly portable ISO C abstraction.

---

# The Philosophy

Darken can be summarized in one diagram:

```text
             GAME
              |
       +------+------+------+
       |      |      |      |
    ENEMIES BULLETS PLAYER PARTICLES
       |      |      |      |
       +------+------+------+
              |
          DARKEN POOL
              |
       +------+------+------+
       | ACTIVE | FREE | PAUSED |
       +------+------+------+
              |
          pointers move
              |
              X
              |
       entities do not move
```

That is Darken.

Small pools, fixed memory, cheap lifecycle operations, and entities that stay where they were allocated.

For a shmup, where bullets, enemies and particles constantly appear and disappear, this makes a very practical foundation without imposing a larger engine architecture on the game.
