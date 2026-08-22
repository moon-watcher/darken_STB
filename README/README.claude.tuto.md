# Darken — DARKula ENgine 2.0

Darken is a single-header, allocation-free entity system for C, built for GCC/SGDK and the Motorola 68000 (Sega Mega Drive / Genesis). No `malloc` at runtime, no archetypes, no reflection — just a fixed-capacity entity manager plus a small pointer pool (`darksys`) for cross-entity logic.

Rather than dump the whole API on you up front, this README builds one small thing with it: **Meteor Dodge**, a tiny terminal simulation — a stationary ship, three falling meteors, a pause button, and a score. It's not a real game (no input, no graphics), just enough moving parts to exercise every corner of Darken naturally, in the order you'd actually reach for it.

Every snippet below is a real piece of one program that compiles and runs. Every terminal block you see is copy-pasted output from actually running it — nothing here is hand-typed or imagined.

---

## Table of contents

- [Darken — DARKula ENgine 2.0](#darken--darkula-engine-20)
  - [Table of contents](#table-of-contents)
  - [Setup](#setup)
  - [1. Reserve memory for entities](#1-reserve-memory-for-entities)
  - [2. Spawn your first entity](#2-spawn-your-first-entity)
  - [3. Give it behavior: states](#3-give-it-behavior-states)
  - [4. See everything at once: `DE_MANAGER_FOREACH`](#4-see-everything-at-once-de_manager_foreach)
  - [5. Cleanup on death: destructors](#5-cleanup-on-death-destructors)
  - [6. Who goes first: `move_front` / `move_back`](#6-who-goes-first-move_front--move_back)
  - [7. Freeze frame: pause and resume](#7-freeze-frame-pause-and-resume)
  - [8. Talking across entities: `darksys`](#8-talking-across-entities-darksys)
  - [9. Tearing it down: `de_manager_reset`](#9-tearing-it-down-de_manager_reset)
  - [10. Full listing](#10-full-listing)
  - [11. Quick reference](#11-quick-reference)
    - [Types](#types)
    - [Entity](#entity)
    - [Manager](#manager)
    - [System](#system)
    - [Macros](#macros)
    - [State control values](#state-control-values)
  - [12. If you're upgrading from an older `darken.h`](#12-if-youre-upgrading-from-an-older-darkenh)

---

## Setup

Darken is one header, STB-style. In exactly one `.c` file:

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
```

Everywhere else, just `#include "darken.h"`. Only dependency: `<stdint.h>`. It needs GCC (or a compatible compiler) for `__attribute__((aligned(4)))` and a statement expression used internally — that's the trade Darken makes for being efficient and predictable on an actual 68000, and it's why every example in this README was compiled with `gcc -std=gnu11 -Wall -Wextra`.

---

## 1. Reserve memory for entities

Darken never allocates. You give it a block of memory up front, sized for a maximum number of entities and a payload size, and it slices that block up itself. `DE_MANAGER_STORAGE` is the macro that declares that block for you:

```c
DE_MANAGER_STORAGE(storage, 8, sizeof(struct meteor_data));
```

That's "room for up to 8 entities, each carrying a `struct meteor_data` payload." Now the manager itself:

```c
struct de_manager g_manager_storage;
de_manager g_manager = &g_manager_storage;

de_manager_init(g_manager, DE_MANAGER_ARGS(storage));
```

Two lines instead of one, and this is worth pausing on: `de_manager` is a **pointer** type (`typedef struct de_manager *de_manager;`), not a struct you can declare directly. Write `de_manager g_manager;` on its own and you get an uninitialized pointer pointing at nothing. So we declare the real `struct de_manager` ourselves and take its address. Same deal for `darksys` later on. It reads a little unusual the first time, then it's just a habit — declare the struct, hand its address to the API.

`DE_MANAGER_ARGS(storage)` expands to the four arguments `de_manager_init` wants (its pointer table, its raw byte storage, the capacity, the payload size) — you never have to spell those out by hand.

---

## 2. Spawn your first entity

Every entity carries a fixed-size payload — for us, `struct meteor_data`. The player gets its own payload type and its own storage, but let's spawn it in the *same* manager as the meteors (nothing stops you from having one manager per entity type, but for a game this small there's no need):

```c
struct player_data { int16_t lane; };
struct meteor_data { int16_t lane; int16_t row; };

de_entity g_player = de_manager_new(g_manager);

struct player_data *pd = (struct player_data *)g_player->data;
pd->lane = 2;
g_player->tag = TAG_PLAYER;
```

`de_manager_new` hands you a `de_entity` — already a pointer, always was — pointing into the manager's own storage block. `entity->data` is the raw payload; you cast it to whatever struct you're using. `de_manager_new` resets `state`, `destructor`, and `tag` for you, but **not** the payload — it's yours to fill in, which is exactly what we just did with `pd->lane = 2`.

Right now `g_player` does absolutely nothing: a freshly created entity's `state` is `DE_STATE_DELETE`. On to fixing that.

---

## 3. Give it behavior: states

An entity's `state` field is a function pointer: `void *(*)(void *)`. Darken calls it with `entity->data` every frame, and whatever it returns becomes the next state — with three special return values reserved for the manager itself:

| Return value | Meaning |
|---|---|
| another function pointer | switch to that state |
| `DE_STATE_LOOP` | keep the current state |
| `DE_STATE_PAUSE` | pause the entity |
| `DE_STATE_DELETE` | delete the entity |

Here's a meteor falling:

```c
void *meteor_fall(void *raw)
{
    struct meteor_data *m = (struct meteor_data *)raw;
    m->row++;

    if (m->row >= GRID_ROWS) {
        printf("  [miss]  lane %d meteor passed the ship\n", m->lane);
        return DE_STATE_DELETE;
    }
    return DE_STATE_LOOP;
}
```

and a little spawner:

```c
de_entity spawn_meteor(int16_t lane)
{
    de_entity e = de_manager_new(g_manager);
    struct meteor_data *m = (struct meteor_data *)e->data;
    m->lane = lane;
    m->row  = 0;

    e->state = meteor_fall;
    e->tag   = TAG_METEOR;
    return e;
}
```

Spawn three and hand-crank a couple of frames:

```c
spawn_meteor(0);
spawn_meteor(2);
spawn_meteor(4);

for (int frame = 1; frame <= 2; ++frame) {
    printf("frame %d\n", frame);
    de_manager_update(g_manager);
}
```

```text
frame 1
frame 2
```

Nothing printed because nothing fell off the bottom yet — but the rows *are* advancing (row 8 is where `GRID_ROWS` cuts them off; we'll actually see them a couple of sections down, once we can look inside the manager).

**One thing worth knowing before you go further:** returning `DE_STATE_DELETE` doesn't delete the entity *that frame*. It gets stored in `entity->state`, and the actual deletion — destructor and all — happens on the *next* call to `de_manager_update`. Verified in isolation:

```text
call #1 to de_manager_update:
  callback ran, returning DELETE
  after call #1: destroyed=0, still in active zone? size=1
call #2 to de_manager_update:
  destructor ran
  after call #2: destroyed=1, size=0
```

Keep that in your back pocket — it explains a slightly surprising line of output near the end of this README.

---

## 4. See everything at once: `DE_MANAGER_FOREACH`

We want a "radar" that prints every meteor currently on screen. `DE_MANAGER_FOREACH` walks the manager's active entities and hands you three variables inside the block: `INDEX`, `POOL` (the manager's own pointer array), and `ENTITY`:

```c
void print_radar(void)
{
    printf("  radar:");
    DE_MANAGER_FOREACH(g_manager, {
        if (ENTITY->tag == TAG_METEOR) {
            struct meteor_data *m = (struct meteor_data *)ENTITY->data;
            printf(" (lane %d, row %d)", m->lane, m->row);
        }
    });
    printf("\n");
}
```

Call it once after spawning the three meteors:

```text
  radar: (lane 4, row 0) (lane 2, row 0) (lane 0, row 0)
```

Notice the order: **lane 4 first**, even though lane 0 was spawned first. `DE_MANAGER_FOREACH` walks backward — from the highest active index down to zero — which is exactly what lets a callback safely delete, pause, or resume *itself* mid-loop without corrupting the traversal. (Mutating a *different*, not-yet-visited entity from inside the loop is a different story — more on that with `darksys` in a moment, where it's easier to see exactly what goes wrong.)

---

## 5. Cleanup on death: destructors

Give a meteor a destructor and it runs right before the entity's slot is freed — whether it died from falling off-screen or was explicitly deleted elsewhere:

```c
void *meteor_destroyed(void *raw)
{
    struct meteor_data *m = (struct meteor_data *)raw;
    printf("  [boom]  lane %d meteor destroyed\n", m->lane);
    g_score++;
    return 0;
}
```

```c
e->destructor = meteor_destroyed;
```

One rule worth knowing up front: **the destructor's return value is completely ignored.** It cannot refuse or cancel the deletion — it only gets a chance to clean up before the slot is handed back.

---

## 6. Who goes first: `move_front` / `move_back`

Since the active zone updates *backward*, "front" and "back" refer to array position, not intuitive left-to-right time. Moving an entity to the front of the array makes it run **earlier** next frame; to the back, **later**. Watch the radar order change:

```c
print_radar();
de_entity_move_front(g_meteors[0]);   /* the lane-0 meteor */
print_radar();
```

```text
  radar: (lane 4, row 0) (lane 2, row 0) (lane 0, row 0)
  moving the lane-0 meteor to the front of the update order...
  radar: (lane 0, row 0) (lane 2, row 0) (lane 4, row 0)
```

Lane 0 jumped to the front of the printout, because it's now at the *highest* active index — the first one the backward traversal reaches. Both functions are no-ops if the entity isn't active, or is already where you're asking it to go.

---

## 7. Freeze frame: pause and resume

Pausing an entity pulls it out of the active zone entirely — `de_manager_update` skips it, `DE_MANAGER_FOREACH` skips it — but its **address never moves**. That's the whole point of the three-zone design: a paused entity's `data` pointer stays valid for as long as you hold onto it.

```c
if (frame == 4) {
    printf("  -- paused --\n");
    de_entity_pause(g_meteors[0]);
    de_entity_pause(g_meteors[1]);
    de_entity_pause(g_meteors[2]);
}
if (frame == 6) {
    printf("  -- resumed --\n");
    de_entity_resume(g_meteors[0]);
    de_entity_resume(g_meteors[1]);
    de_entity_resume(g_meteors[2]);
}
```

```text
frame 3:
  radar: (lane 0, row 3) (lane 2, row 3) (lane 4, row 3)
frame 4:
  -- paused --
  radar:
frame 5:
  radar:
frame 6:
  -- resumed --
  radar: (lane 4, row 4) (lane 2, row 4) (lane 0, row 4)
```

Rows freeze at `3` through the two paused frames — the radar prints nothing because paused entities aren't in the active zone `DE_MANAGER_FOREACH` walks — then pick up exactly where they left off once resumed. Nothing was lost; the meteors just weren't part of the update loop for a couple of frames.

---

## 8. Talking across entities: `darksys`

The manager handles *one* entity's lifecycle at a time. Checking "did any meteor reach the ship's row *and* lane" needs to compare a meteor against the player — that's what `darksys` is for: a flat pool of pointer groups, with no lifecycle of its own, that you fill and drain by hand.

Reserve one, sized for 8 groups of 2 pointers each:

```c
struct darksys g_hits_storage;
darksys g_hits = &g_hits_storage;

DARKSYS_STORAGE(hits_storage, 8, 2);
darksys_init(g_hits, DARKSYS_ARGS(hits_storage));
```

Register a `{meteor payload, meteor entity}` pair every time we spawn one:

```c
darksys_add(g_hits, m, e);
```

And check the whole pool each frame:

```c
void check_collisions(void)
{
    struct player_data *p = (struct player_data *)g_player->data;

    DARKSYS_FOREACH(g_hits, struct meteor_data *m, de_entity meteor_entity,
    {
        if (m->row == GRID_ROWS - 1 && m->lane == p->lane)
            de_entity_delete(meteor_entity);
    });
}
```

Deleting the meteor here triggers its destructor, which is the right place to keep `g_hits` in sync:

```c
void *meteor_destroyed(void *raw)
{
    struct meteor_data *m = (struct meteor_data *)raw;
    darksys_remove(g_hits, m);
    printf("  [boom]  lane %d meteor destroyed\n", m->lane);
    g_score++;
    return 0;
}
```

**A gotcha worth actually seeing, not just being told about.** Is it safe to mutate `g_hits` from inside its own `DARKSYS_FOREACH`? The answer is "it depends on *which* group you touch," and it's easy to get wrong, so here it is proven rather than asserted. Two tests, both against the real header:

*Removing the group you're currently visiting* — every one of six groups deletes itself as it's visited:

```text
visit #1: 1
visit #2: 2
visit #3: 3
visit #4: 4
visit #5: 5
visit #6: 6
total visits = 6, final size = 0
```

Clean. Every group visited exactly once, nothing lost.

*Removing a group you haven't reached yet* — while visiting `10`, we remove `30`, which the loop hasn't gotten to:

```text
visiting 10
visiting 20
visiting 50
visiting 40
visiting 50
```

`50` shows up **twice**. `DARKSYS_FOREACH` snapshots its end boundary once, at the start; removing a group compacts the pool by copying the last group into the hole, but the loop's stale boundary doesn't know that and walks one step too far, re-reading memory that's no longer logically part of the pool.

So: **self-removal — the group currently being visited — is safe. Removing anything else mid-pass is not.** It's the exact same rule `DE_MANAGER_FOREACH` has for entities, just not written down anywhere before now. Our `check_collisions` above only ever deletes the entity it's currently looking at, so it's on the safe side of that line.

---

## 9. Tearing it down: `de_manager_reset`

Run the whole thing for ten frames and call `de_manager_reset()` at the end:

```text
frame 9:
  [boom]  lane 2 meteor destroyed
  radar: (lane 4, row 7) (lane 0, row 7)
frame 10:
  [miss]  lane 4 meteor passed the ship
  [miss]  lane 0 meteor passed the ship
  radar: (lane 4, row 8) (lane 0, row 8)

final score: 1
  [boom]  lane 4 meteor destroyed
  [boom]  lane 0 meteor destroyed
after reset: size=0 paused=8
```

Two things to notice. First: the lane-2 meteor hit the ship at frame 9 (our score of `1`), and lanes 4 and 0 missed at frame 10 — but their "boom" lines print *after* `final score`, not during frame 10 itself. That's [the deferred-delete rule from §3](#3-give-it-behavior-states) at work: frame 10's `meteor_fall` returned `DE_STATE_DELETE`, which only got *stored*; there was no frame 11 to act on it, so those two meteors were still sitting in the active zone — merely pending deletion — when `de_manager_reset()` ran and swept them up.

Second, and more important: `de_manager_reset()` only visits the **active** zone. If any of those meteors had still been *paused* instead, their destructors would never have run. Small, isolated proof:

```c
de_entity temp = de_manager_new(g_manager);
temp->destructor = meteor_destroyed;
de_entity_pause(temp);

de_manager_reset(g_manager);
```

```text
entity created and paused; calling de_manager_reset()...
(no '[boom]' line above this one -> the paused entity's destructor never ran)
```

If your entities hold onto resources through a destructor, resume or explicitly delete anything you've paused *before* calling `de_manager_reset()` — it will not do that for you.

---

## 10. Full listing

Everything above, assembled into the program that produced every output block in this README. Compiled with `gcc -std=gnu11 -Wall -Wextra`, zero warnings.

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdio.h>

#define GRID_LANES 5
#define GRID_ROWS  8

enum { TAG_PLAYER = 1, TAG_METEOR = 2 };

struct player_data { int16_t lane; };
struct meteor_data { int16_t lane; int16_t row; };

static struct de_manager g_manager_storage;
static de_manager g_manager = &g_manager_storage;

static struct darksys g_hits_storage;
static darksys g_hits = &g_hits_storage;

static de_entity g_player;
static de_entity g_meteors[3];
static int g_meteor_count = 0;
static int g_score = 0;

void *player_idle(void *raw) { (void)raw; return DE_STATE_LOOP; }

void *meteor_fall(void *raw)
{
    struct meteor_data *m = (struct meteor_data *)raw;
    m->row++;

    if (m->row >= GRID_ROWS) {
        printf("  [miss]  lane %d meteor passed the ship\n", m->lane);
        return DE_STATE_DELETE;
    }
    return DE_STATE_LOOP;
}

void *meteor_destroyed(void *raw)
{
    struct meteor_data *m = (struct meteor_data *)raw;
    darksys_remove(g_hits, m);
    printf("  [boom]  lane %d meteor destroyed\n", m->lane);
    g_score++;
    return 0;
}

de_entity spawn_meteor(int16_t lane)
{
    de_entity e = de_manager_new(g_manager);
    if (!e) return 0;

    struct meteor_data *m = (struct meteor_data *)e->data;
    m->lane = lane;
    m->row  = 0;

    e->state      = meteor_fall;
    e->destructor = meteor_destroyed;
    e->tag        = TAG_METEOR;

    darksys_add(g_hits, m, e);
    g_meteors[g_meteor_count++] = e;
    return e;
}

void check_collisions(void)
{
    struct player_data *p = (struct player_data *)g_player->data;

    DARKSYS_FOREACH(g_hits, struct meteor_data *m, de_entity meteor_entity,
    {
        if (m->row == GRID_ROWS - 1 && m->lane == p->lane)
            de_entity_delete(meteor_entity); /* self-removal: safe, see §8 */
    });
}

void print_radar(void)
{
    printf("  radar:");
    DE_MANAGER_FOREACH(g_manager, {
        if (ENTITY->tag == TAG_METEOR) {
            struct meteor_data *m = (struct meteor_data *)ENTITY->data;
            printf(" (lane %d, row %d)", m->lane, m->row);
        }
    });
    printf("\n");
}

int main(void)
{
    DE_MANAGER_STORAGE(storage, 8, sizeof(struct meteor_data));
    de_manager_init(g_manager, DE_MANAGER_ARGS(storage));

    DARKSYS_STORAGE(hits_storage, 8, 2);
    darksys_init(g_hits, DARKSYS_ARGS(hits_storage));

    g_player = de_manager_new(g_manager);
    struct player_data *pd = (struct player_data *)g_player->data;
    pd->lane = 2;
    g_player->state = player_idle;
    g_player->tag   = TAG_PLAYER;

    spawn_meteor(0);
    spawn_meteor(2); /* aimed straight at the player's lane */
    spawn_meteor(4);

    printf("-- move_front demo --\n");
    print_radar();
    printf("  moving the lane-0 meteor to the front of the update order...\n");
    de_entity_move_front(g_meteors[0]);
    print_radar();
    printf("\n");

    for (int frame = 1; frame <= 10; ++frame) {
        printf("frame %d:\n", frame);

        if (frame == 4) { printf("  -- paused --\n");  de_entity_pause(g_meteors[0]);
                                                          de_entity_pause(g_meteors[1]);
                                                          de_entity_pause(g_meteors[2]); }
        if (frame == 6) { printf("  -- resumed --\n"); de_entity_resume(g_meteors[0]);
                                                          de_entity_resume(g_meteors[1]);
                                                          de_entity_resume(g_meteors[2]); }

        de_manager_update(g_manager);
        check_collisions();
        print_radar();
    }

    printf("\nfinal score: %d\n", g_score);

    de_manager_reset(g_manager);
    printf("after reset: size=%d paused=%d\n", g_manager->size, g_manager->paused);

    printf("\n-- reset skips paused entities: a clean demo --\n");
    de_entity temp = de_manager_new(g_manager);
    temp->destructor = meteor_destroyed;
    struct meteor_data *td = (struct meteor_data *)temp->data;
    td->lane = 9; td->row = 9;
    de_entity_pause(temp);
    printf("  entity created and paused; calling de_manager_reset()...\n");
    de_manager_reset(g_manager);
    printf("  (no '[boom]' line above this one -> the paused entity's destructor never ran)\n");

    return 0;
}
```

---

## 11. Quick reference

### Types

```c
typedef void *(*de_state)(void *);

typedef struct de_entity  *de_entity;    /* pointer typedefs, all three */
typedef struct de_manager *de_manager;
typedef struct darksys  *darksys;

struct de_entity  { de_state state, destructor; de_manager owner; uint16_t slot, tag; uint8_t data[]; };
struct de_manager { de_entity *pool; uint16_t capacity, size, paused; };
struct darksys  { void **pool, **end; uint16_t capacity, size, params; };
```

### Entity

| | |
|---|---|
| `de_entity_exec(e)` | runs the active state, doesn't write `e->state` |
| `de_entity_update(e)` | runs and stores the transition — only call on an entity that currently has an active callback; calling it on one that doesn't forces `DE_STATE_DELETE` |
| `de_entity_pause(e)` / `de_entity_resume(e)` | move zones without moving memory; no-op if not applicable |
| `de_entity_delete(e)` | destructor (if any) then frees the slot; no-op if already free |
| `de_entity_move_front(e)` / `de_entity_move_back(e)` | run earlier / later next update, O(1) |

### Manager

| | |
|---|---|
| `de_manager_init(m, pool, storage, capacity, payload_size)` | one-time setup over caller-owned memory |
| `de_manager_new(m)` | `NULL` if full; resets `state`/`destructor`/`tag`, **not** `data` |
| `de_manager_update(m)` | runs the active zone, backward, once |
| `de_manager_reset(m)` | deletes active entities only — **not** paused ones (§9) |

### System

| | |
|---|---|
| `darksys_init(s, storage, capacity_groups, params)` | one-time setup |
| `darksys_add(s, ...)` | 1–5 pointers, must match `params` every time |
| `DARKSYS_FOREACH(s, ...)` | 0–5 output vars; safe to remove the *current* group, unsafe to remove a different one (§8) |
| `darksys_remove(s, first)` | matches by first pointer, swap-removes (order not preserved) |

### Macros

| | |
|---|---|
| `DE_MANAGER_STORAGE` / `DE_MANAGER_ARGS` | static manager storage + init args |
| `DE_MANAGER_FOREACH(m, code)` | backward, active-only; exposes `INDEX`, `POOL`, `ENTITY` |
| `DARKSYS_STORAGE` / `DARKSYS_ARGS` | static system storage + init args |
| `DARKSYS_ITERATOR(name, ...)` | generates `void *name(darksys)`, installable as a `state` |

### State control values

`DE_STATE_DELETE` (`0`) · `DE_STATE_LOOP` (`1`) · `DE_STATE_PAUSE` (`2`) — anything else is a real callback. Returning `DELETE`/`PAUSE` from a callback takes effect on the *next* `de_manager_update` call, not the current one (§3/§9). Never assign `DE_STATE_LOOP` to `entity->state` by hand — it's only meaningful as a return value.

---

## 12. If you're upgrading from an older `darken.h`

The one structural change that will break old code: `de_manager` and `darksys` are now pointer typedefs, matching `de_entity`. If you have code like

```c
de_manager mgr;
de_manager_init(&mgr, ...);
```

it needs to become

```c
struct de_manager mgr;
de_manager_init(&mgr, ...);
```

— the `struct` keyword, not the typedef, for the actual instance. Every function that used to take `de_manager *` now takes a plain `de_manager`. A few field names changed too (`items`→`pool`, `active_count`→`size`, `paused_start`→`paused`, the entity's `manager`→`owner`), and `DE_ENTITY_STRIDE` is no longer public — it's `_DE_ENTITY_STRIDE` now, internal only. Everything else in this README describes current, verified behavior either way.