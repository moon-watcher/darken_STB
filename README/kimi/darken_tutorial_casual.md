# Darken 2.0 — The Tutorial That Won't Make You Cry

> Hey, did someone tell you that writing an entity system in C is a pain? Lies. With Darken it's more like LEGO: tiny pieces, satisfying clicks, and at the end you have something moving on screen.

We're gonna build a **spaceship vs space junk** game. No enterprise architecture, no 47 layers of abstraction. One ship, some asteroids, and a loop that goes *tick tick tick*.

---

## What do I need to know before starting?

Three things, that's it:

1. **The Manager** is like the club owner. Decides who gets in, who leaves, and who's on the dance floor (active) vs who's at the bar taking a break (paused).
2. **Entities** are your game objects: ship, asteroids, bullets, particles... Each one has a `state`, which is just a function saying "do this every frame."
3. **`de_manager_update()`** is the DJ. Every time you call it, it goes through all active entities and says "alright, play your tune."

That's it. With that we can start.

---

## Step 1: Setting the Scene

First: reserve memory. Darken doesn't steal bytes behind your back; you say "yo, reserve space for 50 entities of size X" and that's it.

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdio.h>
#include <stdlib.h>

/* Our game data */
typedef struct {
    float x, y;
    float vx, vy;
    int   life;
} Thing;

int main(void)
{
    /* This creates an anonymous struct with:
       - pool[] : entity pointers
       - data[] : ACTUAL memory where the Things live
    */
    DE_MANAGER_STORAGE(world, 50, sizeof(Thing));

    struct de_manager manager;
    de_manager_init(&manager, DE_MANAGER_ARGS(world));

    printf("Manager ready. Capacity: %u\n", manager.capacity);
    return 0;
}
```

Compile it:
```bash
gcc -std=gnu99 main.c -o little_game && ./little_game
```

If you see `Manager ready. Capacity: 50`, you've already won half the battle.

---

## Step 2: Creating Entities (The Fun Part)

`de_manager_new()` gives you a fresh entity hot from the oven. If it returns `NULL`, the club is full.

```c
de_entity create_ship(de_manager *m)
{
    de_entity e = de_manager_new(m);
    if (!e) {
        printf("No room for more ships, buddy.\n");
        return NULL;
    }

    Thing *t = (Thing *)e->data;
    t->x = 40.0f;
    t->y = 20.0f;
    t->vx = 0.0f;
    t->vy = 0.0f;
    t->life = 3;

    /* Tag is free for whatever you want */
    e->tag = 1;  /* 1 = ship */

    return e;
}
```

Call it in your `main`:

```c
    de_entity ship = create_ship(&manager);
    if (ship) {
        Thing *t = (Thing *)ship->data;
        printf("Ship created at (%.0f, %.0f) with %d lives\n", t->x, t->y, t->life);
    }
```

**What happened here?**
- `de_manager_new` grabbed a slot from the free area and moved it to the active area.
- `e->data` points straight at your `Thing`. No magic, no weird offsets. It's your struct, period.

---

## Step 3: Giving It Life with States and `de_manager_update()`

Here's the magic. Every entity has a `state`: a function that takes `void *data` and returns what to do next.

```c
void *ship_update(Thing *t)
{
    /* Automatic movement for the example */
    t->x += t->vx;
    t->y += t->vy;

    /* Dumb bounce against imaginary borders */
    if (t->x < 0 || t->x > 80) t->vx *= -1;
    if (t->y < 0 || t->y > 24) t->vy *= -1;

    /* Keep this state next frame */
    return DE_STATE_LOOP;
}
```

Assign it when creating the ship:

```c
    ship->state = (de_state)ship_update;
    ((Thing *)ship->data)->vx = 0.5f;
    ((Thing *)ship->data)->vy = 0.2f;
```

And now the game loop:

```c
    for (int frame = 0; frame < 200; frame++) {
        de_manager_update(&manager);  /* THAT'S IT! */

        /* Just to see it move */
        Thing *t = (Thing *)ship->data;
        printf("\rFrame %3d | Ship at (%.1f, %.1f)", frame, t->x, t->y);
        fflush(stdout);
    }
    printf("\n");
```

### What does `de_manager_update()` do under the hood?

It walks active entities **backwards** (yeah, in reverse like *Tenet*). For each one:

1. Calls `entity->state(entity->data)`.
2. If it returns `DE_STATE_LOOP`, does nothing. Entity stays alive with the same state.
3. If it returns `DE_STATE_DELETE`, kills it right there.
4. If it returns `DE_STATE_PAUSE`, removes it from the dance floor.
5. If it returns another function, switches to that new state.

> **Why backwards?** Because if an entity deletes itself (or another), array indices don't break. It's a simple trick that saves you a ton of headaches.

---

## Step 4: Throwing in a `de_system` (Because Why Not)

Managers handle life and death. **Systems** process data in a cache-friendly way. Let's make an **engine particle** system for the ship.

First, we need a system. It's a flat array of pointers. If you tell it 2 parameters, it stores groups of 2 pointers back-to-back: `(ptr1, ptr2), (ptr1, ptr2)...`

```c
/* Particle system: stores (position, velocity) */
DE_SYSTEM_STORAGE(particles, 100, 2);

/* Function that updates all particles */
void *update_particles(de_system sys)
{
    /* DE_SYSTEM_FOREACH_2 unpacks 2 pointers per group */
    DE_SYSTEM_FOREACH_2(sys, float *px, float *py, {
        *px += (rand() % 3 - 1) * 0.1f;  /* jitter on X */
        *py += 0.3f;                      /* fall down */
    });
    return DE_STATE_LOOP;
}
```

In `main`, initialize and use it:

```c
    struct de_system engine;
    de_system_init(&engine, DE_SYSTEM_ARGS(particles));

    /* Every time we want a particle: */
    float *pos_x = &((Thing *)ship->data)->x;
    float *pos_y = &((Thing *)ship->data)->y;
    DE_SYSTEM_ADD(&engine, pos_x, pos_y);  /* store pointers to ship position */
```

And in the loop:

```c
    for (int frame = 0; frame < 200; frame++) {
        de_manager_update(&manager);
        update_particles(&engine);

        /* ... print stuff ... */
    }
```

### What's the deal with `de_system`?

It's a box of flat pointers. Doesn't know about entities, doesn't know about managers. Only knows it has `params` pointers per group and a bunch of groups. Iteration is super fast because everything is packed tight in memory.

> **Tip:** If you have a rendering system, a physics system, and a sound system, each can have its own `de_system` pointing at the data it cares about. No "searching components by ID." Direct pointers, friend.

---

## Step 5: Spawning Enemies and Killing Them

Let's create asteroids that self-destruct when leaving the screen.

```c
void *asteroid_update(Thing *t)
{
    t->y += t->vy;

    if (t->y > 25.0f)
        return DE_STATE_DELETE;  /* Bye, asteroid */

    return DE_STATE_LOOP;
}

de_entity create_asteroid(de_manager *m)
{
    de_entity e = de_manager_new(m);
    if (!e) return NULL;

    Thing *t = (Thing *)e->data;
    t->x = rand() % 80;
    t->y = 0.0f;
    t->vx = 0.0f;
    t->vy = 0.1f + (rand() % 5) / 10.0f;
    t->life = 1;
    e->tag = 2;  /* 2 = asteroid */
    e->state = (de_state)asteroid_update;

    return e;
}
```

And in the loop, spawn every 30 frames:

```c
    for (int frame = 0; frame < 300; frame++) {
        if (frame % 30 == 0) create_asteroid(&manager);

        de_manager_update(&manager);

        printf("\rFrame %3d | Active: %2u | Free: %2u",
               frame, manager.size, manager.paused - manager.size);
        fflush(stdout);
    }
```

Notice how `manager.size` grows when asteroids appear and shrinks when they die. `de_manager_update()` handles everything: calls states, deletes whatever returns `DE_STATE_DELETE`, and shuffles pointers so there are no gaps.

---

## Step 6: The Grand Finale — Everything Together

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  /* for usleep */

typedef struct {
    float x, y;
    float vx, vy;
    int   life;
} Thing;

/* ---------- STATES ---------- */

void *ship_update(Thing *t)
{
    t->x += t->vx;
    t->y += t->vy;
    if (t->x < 0 || t->x > 80) t->vx *= -1;
    if (t->y < 0 || t->y > 24) t->vy *= -1;
    return DE_STATE_LOOP;
}

void *asteroid_update(Thing *t)
{
    t->y += t->vy;
    if (t->y > 25.0f) return DE_STATE_DELETE;
    return DE_STATE_LOOP;
}

/* ---------- HELPERS ---------- */

de_entity create_ship(de_manager *m)
{
    de_entity e = de_manager_new(m);
    if (!e) return NULL;
    Thing *t = (Thing *)e->data;
    t->x = 40; t->y = 12; t->vx = 0.3f; t->vy = 0.15f; t->life = 3;
    e->tag = 1;
    e->state = (de_state)ship_update;
    return e;
}

de_entity create_asteroid(de_manager *m)
{
    de_entity e = de_manager_new(m);
    if (!e) return NULL;
    Thing *t = (Thing *)e->data;
    t->x = rand() % 80; t->y = 0;
    t->vx = 0; t->vy = 0.1f + (rand() % 5)/10.0f; t->life = 1;
    e->tag = 2;
    e->state = (de_state)asteroid_update;
    return e;
}

/* ---------- MAIN ---------- */

int main(void)
{
    DE_MANAGER_STORAGE(world, 50, sizeof(Thing));
    struct de_manager manager;
    de_manager_init(&manager, DE_MANAGER_ARGS(world));

    /* Engine particle system */
    DE_SYSTEM_STORAGE(particles, 100, 2);
    struct de_system engine;
    de_system_init(&engine, DE_SYSTEM_ARGS(particles));

    create_ship(&manager);

    printf("=== ORBIT DEFENDER (terminal mode) ===\n\n");

    for (int frame = 0; frame < 200; frame++) {
        if (frame % 30 == 0) create_asteroid(&manager);

        de_manager_update(&manager);

        /* Dumb terminal drawing */
        printf("\033[2J\033[H");  /* clear screen */
        printf("Frame: %d | Active: %u | Free: %u\n", frame, manager.size, manager.paused - manager.size);

        DE_MANAGER_FOREACH(&manager, {
            Thing *t = (Thing *)ENTITY->data;
            if (ENTITY->tag == 1)
                printf("\033[%d;%dH@", (int)t->y, (int)t->x);  /* ship */
            else
                printf("\033[%d;%dH#", (int)t->y, (int)t->x);  /* asteroid */
        });

        fflush(stdout);
        usleep(50000);  /* ~20 FPS */
    }

    printf("\n\nDone! You didn't explode. That's something.\n");
    return 0;
}
```

Compile and run:

```bash
gcc -std=gnu99 main.c -o orbit && ./orbit
```

---

## Cheat Sheet

| I want to... | I do... |
|--------------|---------|
| Create something | `de_manager_new(&manager)` |
| Make it move/change every frame | Assign a `state` and call `de_manager_update()` |
| Make it disappear | The `state` returns `DE_STATE_DELETE` |
| Process lots of data fast | `de_system` + `DE_SYSTEM_FOREACH` |
| Check if something is active | `DE_ENTITY_IS_ACTIVE(e)` |
| Reset everything | `de_manager_reset(&manager)` |

---

## Now what?

You've got the skeleton. From here you can:

- Add `de_entity_delete()` manually when an asteroid hits the ship.
- Use `e->destructor` to spawn explosions when something dies.
- Pause entities with `de_entity_pause()` (useful for pause menus or freeze effects).
- Make more `de_system`s: one for sound, one for AI, one for networking.

Darken doesn't tell you how to structure your game. It gives you a manager that doesn't break, a system that iterates fast, and states that are just functions. The rest is up to you.

**Go code, champ.** 🚀
