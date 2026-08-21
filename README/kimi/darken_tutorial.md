# Darken 2.0 — Progressive Tutorial

> **Darken** (DARKula ENgine) is a C entity system for projects where manual memory control and performance on classic architectures (hello, Motorola 68000!) matter.
>
> It uses GNU C extensions (`__attribute__`, statement expressions) and a **state callback** model.

---

## Table of Contents

1. [Key Concepts (read this first)](#1-key-concepts)
2. [Step 0: Your First Entity](#2-step-0-your-first-entity)
3. [Step 1: Bringing It to Life with States](#3-step-1-bringing-it-to-life-with-states)
4. [Step 2: The Rendering System](#4-step-2-the-rendering-system)
5. [Step 3: Enemy Factory](#5-step-3-enemy-factory)
6. [Step 4: Shooting!](#6-step-4-shooting)
7. [Step 5: Collisions and Destruction](#7-step-5-collisions-and-destruction)
8. [Step 6: Strategic Pause](#8-step-6-strategic-pause)
9. [Step 7: Cleanup with Destructors](#9-step-7-cleanup-with-destructors)
10. [Complete API Reference](#10-complete-api-reference)

---

## 1. Key Concepts

Before writing code, understand these three ideas. Everything else is syntactic sugar.

### The Manager and Its 3 Zones

The `de_manager` is an array of **pointers** to entities. The actual entities live in a contiguous memory block that *you* provide. The manager only reorders *pointers*.

```
[ active ][ free ][ paused ]
0      size   paused   capacity
```

- **Active** `[0, size)`: Updated every frame. Create and destroy freely here.
- **Free** `[size, paused)`: Empty slots. `de_manager_new()` takes from here.
- **Paused** `[paused, capacity)`: Out of the loop. **Crucial:** a paused entity never moves in memory, so it is safe for external code to keep raw pointers into its `data[]`.

### States as Callbacks

Each entity has a `state` which is a function pointer:

```c
void *my_state(void *data);
```

The function receives the entity's data and returns what to do next:

| Return value | Meaning |
|--------------|---------|
| `DE_STATE_LOOP`   | Stay in this state next frame |
| `DE_STATE_DELETE` | Destroy the entity |
| `DE_STATE_PAUSE`  | Pause the entity (move to paused zone) |
| Any other `de_state` | Transition to that new state |

### Systems: Flat-Packed Arrays

A `de_system` is a flat pool of pointers. If you have a "position + velocity" system, you store contiguous pairs `(pos*, vel*)`. Iteration is cache-friendly and trivial.

---

## 2. Step 0: Your First Entity

Our game will be *Orbit Defender*: a ship in the center that shoots asteroids. Let's start by defining the ship's data.

### Project Structure

```
orbit_defender/
├── darken.h
└── main.c
```

### `main.c` — The Basics

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdio.h>

/* ============================================================
 * GAME DATA
 * ============================================================ */

typedef struct { float x, y; } Vec2;

typedef struct
{
    Vec2 pos;
    Vec2 vel;
    int hp;
} Ship;

/* ============================================================
 * STEP 0: Manager setup and ship creation
 * ============================================================ */

int main(void)
{
    /* 1. Reserve memory for 32 entities with payload sizeof(Ship).
     *    The DE_MANAGER_STORAGE macro creates an anonymous struct with:
     *    - pool[]: array of entity pointers
     *    - data[]: actual memory block for the payloads
     */
    DE_MANAGER_STORAGE(world, 32, sizeof(Ship));

    /* 2. Initialize the manager. Arguments come from another macro. */
    struct de_manager manager;
    de_manager_init(&manager, DE_MANAGER_ARGS(world));

    /* 3. Create our first entity. */
    de_entity ship = de_manager_new(&manager);
    if (!ship) {
        printf("No free slots!\n");
        return 1;
    }

    /* 4. Access its payload and initialize it. */
    Ship *data = (Ship *)ship->data;
    data->pos = (Vec2){ 40.0f, 12.0f };  /* center of an 80x24 screen */
    data->vel = (Vec2){ 0.0f, 0.0f };
    data->hp  = 3;

    printf("Ship created at slot %u, position (%.1f, %.1f)\n",
           ship->slot, data->pos.x, data->pos.y);

    return 0;
}
```

Compile with:
```bash
gcc -std=gnu99 main.c -o orbit_defender
```

**What just happened?**
- `DE_MANAGER_STORAGE` reserved `32 * sizeof(struct de_entity + sizeof(Ship))` bytes aligned to 4.
- `de_manager_new()` gave us the first entity from the free zone, moving the `size` boundary to the right.
- `ship->data` is a flexible array member (`uint8_t data[]`) that points directly at the `Ship` payload.

---

## 3. Step 1: Bringing It to Life with States

A static ship is boring. Let's make it move with simulated keyboard input using a **state**.

```c
/* Ship state: runs every frame */
void *ship_update(Ship *s)
{
    /* Simulated input: move right automatically */
    s->pos.x += s->vel.x;
    s->pos.y += s->vel.y;

    /* Simple bounce at edges */
    if (s->pos.x > 78.0f || s->pos.x < 1.0f) s->vel.x *= -1;
    if (s->pos.y > 22.0f || s->pos.y < 1.0f) s->vel.y *= -1;

    /* Keep this state next frame */
    return DE_STATE_LOOP;
}
```

And in `main`, after creating the ship:

```c
    data->vel = (Vec2){ 0.5f, 0.3f };

    /* Assign the initial state */
    ship->state = (de_state)ship_update;

    /* Simulate 60 frames of gameplay */
    for (int frame = 0; frame < 60; ++frame) {
        de_manager_update(&manager);
    }
```

### What does `de_manager_update` do?

It traverses **only the active zone** `[0, size)` backwards:
1. Calls `entity->state(entity->data)`.
2. If the state returns `DE_STATE_LOOP`, it does not touch `entity->state`.
3. If it returns any other value (including `DE_STATE_DELETE`), it updates the state.

> **Note:** The reverse traversal allows an entity to delete itself (or others) without breaking iteration.

---

## 4. Step 2: The Rendering System

Iterating entities one by one with `DE_MANAGER_FOREACH` is fine, but for rendering we want a specialized **system** with flat pointers and cache-friendly layout.

We define a `de_system` that stores pointers to `Vec2` (positions) for drawing.

```c
/* Rendering system: 1 parameter (pointer to Vec2) */
DE_SYSTEM_STORAGE(render_sys, 64, 1);

/* Generate a system function that draws entities.
 * DE_SYSTEM_ITERATOR_1 creates: void *draw_system(de_system system)
 * with one unpacked pointer per group: Vec2 *pos = pool[0] */
DE_SYSTEM_ITERATOR_1(draw_system, Vec2 *pos, {
    printf("\033[%d;%dH*", (int)pos->y, (int)pos->x);
})
```

In `main`:

```c
    struct de_system renderer;
    de_system_init(&renderer, DE_SYSTEM_ARGS(render_sys));

    /* Every time we create a visible entity, register it */
    DE_SYSTEM_ADD(&renderer, &data->pos);

    /* In the game loop, before or after update: */
    draw_system(&renderer);
```

### Why a System?

- **Manager**: manages lifecycle (create, destroy, pause).
- **System**: processes data. It is a flat array of pointers to the components you care about.

You can have multiple systems: one for physics, one for AI, one for sound. Each with its own `de_system`.

---

## 5. Step 3: Enemy Factory

Let's create asteroids that fall from the top. We need **tags** to distinguish types.

```c
typedef enum { TAG_SHIP, TAG_ASTEROID } Tag;

typedef struct
{
    Vec2 pos;
    Vec2 vel;
    int hp;
    Tag tag;
} Entity;  /* Renamed: now generic */
```

Asteroid state:

```c
void *asteroid_update(Entity *e)
{
    e->pos.y += e->vel.y;  /* falling */

    if (e->pos.y > 24.0f)
        return DE_STATE_DELETE;  /* destroyed when off-screen */

    return DE_STATE_LOOP;
}
```

Spawner function:

```c
void spawn_asteroid(de_manager m, de_system renderer)
{
    de_entity a = de_manager_new(m);
    if (!a) return;

    Entity *e = (Entity *)a->data;
    e->pos = (Vec2){ rand() % 80, 0.0f };
    e->vel = (Vec2){ 0.0f, 0.2f + (rand() % 10) / 10.0f };
    e->hp  = 1;
    e->tag = TAG_ASTEROID;

    a->state = (de_state)asteroid_update;
    DE_SYSTEM_ADD(renderer, &e->pos);
}
```

In the game loop, spawn every few frames:

```c
    for (int frame = 0; frame < 300; ++frame) {
        if (frame % 30 == 0) spawn_asteroid(&manager, &renderer);

        de_manager_update(&manager);   /* moves everything */
        draw_system(&renderer);          /* draws everything */
    }
```

---

## 6. Step 4: Shooting!

The ship shoots projectiles upward. Projectiles are short-lived entities.

```c
typedef struct { Vec2 pos; Vec2 vel; int life; } Bullet;

void *bullet_update(Bullet *b)
{
    b->pos.y -= b->vel.y;
    b->life--;

    if (b->pos.y < 0 || b->life <= 0)
        return DE_STATE_DELETE;

    return DE_STATE_LOOP;
}

void shoot(de_manager m, Vec2 origin, de_system renderer)
{
    de_entity b = de_manager_new(m);
    if (!b) return;

    Bullet *dat = (Bullet *)b->data;
    dat->pos = origin;
    dat->vel = (Vec2){ 0.0f, 1.5f };
    dat->life = 40;

    b->state = (de_state)bullet_update;
    DE_SYSTEM_ADD(renderer, &dat->pos);
}
```

Modify the ship state to shoot automatically:

```c
void *ship_update(Entity *n)
{
    n->pos.x += n->vel.x;
    /* ... bounces ... */

    /* Auto-shoot every frame (for the example) */
    static int cooldown = 0;
    if (--cooldown <= 0) {
        cooldown = 10;
        shoot(n->_owner, n->pos, &renderer);  /* you'd pass renderer around */
    }

    return DE_STATE_LOOP;
}
```

> **Tip:** Because `de_manager_update` iterates backwards, if an asteroid and a bullet collide and both mark themselves for deletion, there are no invalid index issues.

---

## 7. Step 5: Collisions and Destruction

Let's implement a simple collision system: if a bullet touches an asteroid, both die.

We use `DE_MANAGER_FOREACH` to iterate over active entities:

```c
void check_collisions(de_manager m)
{
    DE_MANAGER_FOREACH(m, {
        if (ENTITY->state == DE_STATE_DELETE) continue;

        Entity *e = (Entity *)ENTITY->data;
        if (e->tag != TAG_ASTEROID) continue;

        /* Check against all bullets */
        DE_MANAGER_FOREACH(m, {
            if (ENTITY->state == DE_STATE_DELETE) continue;
            Bullet *b = (Bullet *)ENTITY->data;

            /* Simple distance */
            float dx = e->pos.x - b->pos.x;
            float dy = e->pos.y - b->pos.y;
            if (dx*dx + dy*dy < 4.0f) {
                de_entity_delete(ENTITY);  /* bullet */
                /* asteroid is the outer ENTITY */
                break;
            }
        });
    });
}
```

Add `check_collisions(&manager)` in your game loop, between `update` and `draw`.

### `de_entity_delete` in depth

When you call `de_entity_delete(e)`:
1. If it has an active `destructor`, it executes it.
2. If in active zone: swaps with the last active and shrinks `size`.
3. If in paused zone: swaps with the first paused and shrinks paused zone (increments `paused`).
4. The slot joins the free zone.

---

## 8. Step 6: Strategic Pause

Imagine we want to freeze asteroids when the ship uses a "time shield". Pausing an entity removes it from the update loop but **does not free its memory**.

```c
/* At some point in the game... */
de_entity some_asteroid = ...;
de_entity_pause(some_asteroid);  /* Frozen. Its data[] remains valid. */
```

Later:

```c
de_entity_resume(some_asteroid);  /* Back to active zone */
```

### Why is this powerful?

A `de_system` can keep pointing to `entity->data` of a paused entity. For example, a rendering system can keep drawing it (frozen in ice) while logic stops processing it.

> **Golden rule:** Never keep pointers to `data[]` of active entities that might get destroyed. It *is* safe for paused entities.

---

## 9. Step 7: Cleanup with Destructors

When an asteroid explodes, we want to spawn particles. The destructor runs **right before** the entity is moved to the free zone.

```c
void *asteroid_destructor(Entity *e)
{
    printf("Asteroid at (%.0f,%.0f) destroyed\n", e->pos.x, e->pos.y);
    /* Here you could spawn particles, add score, etc. */
    return 0;  /* destructor return value is ignored */
}

/* When creating the asteroid: */
a->destructor = (de_state)asteroid_destructor;
```

To clean everything up when closing the game:

```c
de_manager_reset(&manager);  /* Deletes all active and paused */
```

---

## 10. Complete API Reference

### Types

```c
typedef void *(*de_state)(void *);
typedef struct de_entity *de_entity;
typedef struct de_manager *de_manager;
typedef struct de_system *de_system;
```

### Storage Macros

```c
DE_MANAGER_STORAGE(name, capacity, payload_size)  /* declare static memory */
DE_MANAGER_ARGS(name)                             /* args for init */

DE_SYSTEM_STORAGE(name, capacity, params)         /* declare static pool */
DE_SYSTEM_ARGS(name)                              /* args for init */
```

### Iteration Macros

```c
DE_MANAGER_FOREACH(manager, code)   /* iterates active zone, defines ENTITY */

DE_SYSTEM_ADD(system, ptr1, ...)    /* add pointer group */

/* Iterate a system inline, unpacking N pointers per group.
 * pool[0], pool[1], etc. are accessible inside code. */
DE_SYSTEM_FOREACH(system, code)
DE_SYSTEM_FOREACH(system, A, code)       /* A = pool[0] */
DE_SYSTEM_FOREACH(system, A, B, code)    /* A = pool[0], B = pool[1] */
/* ... up to 5 pointers */
```

### System Iterator Generators

```c
/* Generate a de_state function that iterates a system.
 * NAME will be: void *NAME(de_system system) */
DE_SYSTEM_ITERATOR_0(NAME, code)
DE_SYSTEM_ITERATOR_1(NAME, A, code)       /* A = pool[0] */
DE_SYSTEM_ITERATOR_2(NAME, A, B, code)    /* A = pool[0], B = pool[1] */
/* ... up to 5 pointers */
```

Example:
```c
DE_SYSTEM_ITERATOR_2(update_particles, float *px, float *py, {
    *px += (rand() % 3 - 1) * 0.1f;
    *py += 0.3f;
})
/* Generates: void *update_particles(de_system system) { ... } */
```

### Entity Functions

| Function | Description |
|----------|-------------|
| `void *de_entity_exec(e)` | Execute state once manually |
| `void *de_entity_update(e)` | Execute and update `state` field |
| `uint16_t de_entity_pause(e)` | Move to paused zone. Returns 1 on success |
| `uint16_t de_entity_resume(e)` | Move to active zone. Returns 1 on success |
| `uint16_t de_entity_delete(e)` | Delete (calls destructor, moves to free) |
| `uint16_t de_entity_move_front(e)` | Last in active array (drawn on top) |
| `uint16_t de_entity_move_back(e)` | First in active array (drawn behind) |

### Manager Functions

| Function | Description |
|----------|-------------|
| `void de_manager_init(m, pool, storage, cap, bytes)` | Initialize manager |
| `de_entity de_manager_new(m)` | Create entity in active zone |
| `void de_manager_update(m)` | Execute states of all active entities |
| `void de_manager_reset(m)` | Delete everything and reset zones |

### System Functions

| Function | Description |
|----------|-------------|
| `void de_system_init(s, storage, cap_groups, params)` | Initialize system |
| `uint16_t de_system_remove(s, first_ptr)` | Remove group whose first ptr matches |

### State Constants

```c
DE_STATE_DELETE   /* Destroy entity */
DE_STATE_LOOP     /* Keep current state */
DE_STATE_PAUSE    /* Pause entity */
```

### Zone Query Macros

```c
_DE_ENTITY_IS_ACTIVE(e)
_DE_ENTITY_IS_PAUSED(e)
_DE_ENTITY_IS_FREE(e)
```

---

## Design Philosophy

1. **No hidden allocators:** You control the memory block. Ideal for embedded systems or where `malloc` is unwelcome.
2. **Stable pointers on pause:** Systems can cache `&entity->data` of paused entities without fear of invalidation.
3. **States as machines:** Each entity is a tiny state machine. There are no "systems that process entities by type"; each entity manages itself.
4. **68K-friendly:** 4-byte alignment, preference for `uint16_t`, and compact structures.

---

## Compilation

Requirements: GCC with GNU extensions support.

```bash
gcc -std=gnu99 -O2 -Wall main.c -o orbit_defender
```

For Motorola 68000 platforms (cross-compile):

```bash
m68k-elf-gcc -std=gnu99 -O2 -m68000 ...
```

---

*That's it! You now have a ship, asteroids, bullets, collisions, pauses, and explosions. From here you can add power-ups, waves, or even multiple managers for background and foreground layers. Darken does not impose architecture: it gives you the tools to build your own.*
