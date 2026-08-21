# Darken — DARKula ENgine 2.0 Entity System

`darken.h` is a fixed-capacity, allocation-free entity manager built around a tiny finite-state-machine convention. It's aimed at constrained/retro targets (the header explicitly calls out 4-byte alignment and the Motorola 68000), but builds fine on any GCC/Clang target.

## Design at a glance

- Entities live in one caller-provided, contiguous block of memory, sized and aligned up front. Once placed there by `de_manager_init`, an entity's own address **never moves** for the lifetime of the manager.
- What *does* move is a pointer to that entity inside `manager->pool[]` — the manager reorders these pointers (via O(1) swaps) to keep three logical zones packed:

```
[ active entities ][   free slots   ][ paused entities ]
0                  size              paused             capacity
```

- **Active** `[0, size)`: touched every `de_manager_update()`, visited by `DE_MANAGER_FOREACH`. Entities are freely created and deleted here.
- **Free** `[size, paused)`: unused slots; `de_manager_new()` claims the next one here.
- **Paused** `[paused, capacity)`: parked out of the update loop and out of `DE_MANAGER_FOREACH`. `de_manager_new()` never hands out a slot from here, so a paused entity's address and `data` payload stay untouched until it's explicitly resumed or deleted.

Because an entity's address is stable, it's safe to keep a raw pointer into `entity->data` even while the entity gets paused, resumed, or reordered elsewhere in the pool.

## Compatibility / build

- Uses `__attribute__((aligned(4)))` and C's `inline` keyword — GCC/Clang oriented, matching the 68000 4-byte-alignment requirement called out in the header.
- Single-header "implementation macro" pattern: `#define DARKEN_IMPLEMENTATION` before including it in the translation unit(s) that need the function bodies.
- **Linkage subtlety:** unlike `darksys.h` (whose implementation functions are plain, non-`inline`), every function under `DARKEN_IMPLEMENTATION` here is declared `inline` **without** `static`. Plain C99 `inline` (no `static`) needs an external, non-inline definition to exist in exactly one translation unit, or you can hit "undefined reference" / "multiple definition" errors at link time depending on the compiler and `-std=` flags (GNU89 vs. C99 inline semantics differ). If you run into link errors around these functions, either keep `DARKEN_IMPLEMENTATION` consistent across every file that includes the header, add your own `static`, or provide one non-inline instantiation.

## The state machine convention

An entity's behavior is one function pointer, `de_state`, i.e. `void *(*)(void *)`. It receives the entity's own `data` payload and returns one of:

| Return value            | Meaning                                                                                   |
| ----------------------- | ----------------------------------------------------------------------------------------- |
| a real function pointer | become this state next update                                                             |
| `DE_STATE_LOOP` (`1`)   | keep the *current* state — shorthand for "return myself" without needing a self-reference |
| `DE_STATE_PAUSE` (`2`)  | move the entity to the paused zone                                                        |
| `DE_STATE_DELETE` (`0`) | delete the entity                                                                         |

Internally, "is this state pointer a real function, or one of the three sentinels" is decided with `state > (de_state)2`. This is a fast, common trick in small/retro engines, but it relies on ordinary function-pointer addresses always comparing greater than the small integers 0/1/2 once cast to the same pointer type — true on every mainstream flat-address-space target (including the 68000 this header targets), but not something strict ISO C guarantees in general (pointer relational comparisons across unrelated values have limited defined behavior). Worth knowing if this is ever ported to an unusual architecture.

### ⚠️ Deletion and pause are deferred by one update cycle

This is the single most important behavioral detail to internalize. Look at what `de_manager_update()` does per entity:

```c
de_state state = entity->state;

if (_DE_STATE_IS_ACTIVE(state)) {      // state is a real function pointer
    state = state(entity->data);       // call it
    if (_DE_STATE_IS_UPDATABLE(state)) // result isn't LOOP
        entity->state = state;         // store the sentinel / next state
}

else if (_DE_STATE_IS_PAUSED(state))   // state was already a sentinel from a PRIOR update
    de_entity_pause(entity);
    
else if (_DE_STATE_IS_DELETED(state))
    de_entity_delete(entity);
```

When a state function returns `DE_STATE_DELETE` or `DE_STATE_PAUSE`, that value is only *stored* into `entity->state` during this call — the entity is **not** removed or paused yet. It's still sitting in the active zone, will still be visited by `DE_MANAGER_FOREACH`, and calling `de_entity_exec`/`de_entity_update` on it will now silently no-op (its state no longer passes `_DE_STATE_IS_ACTIVE`). The actual `de_entity_pause`/`de_entity_delete` call — the one that physically removes it from the active zone — only happens the *next* time `de_manager_update()` runs and re-reads that stored sentinel.

Practical implications:

- Expect a one-update lag between "a state function returned `DE_STATE_DELETE`" and the entity actually disappearing from the manager.
- If you act on entities via `DE_MANAGER_FOREACH` right after `de_manager_update()` in the same frame (e.g. to render them), an entity that "died" this frame will still show up once more. Order your update/render passes accordingly, or check `entity->state` yourself if that one-frame ghost matters.
- This is also *why* `de_manager_update()` walks the active zone **backward** (from `size - 1` down to `0`): a same-pass swap-removal only ever touches indices at or below the current position, so a backward loop can never disturb entities it hasn't visited yet. Follow the same pattern (iterate backward) in any custom loop that deletes or pauses entities as it goes.

## Data structures

```c
struct de_entity
{
    de_state state;      // current behavior / FSM state
    de_state destructor; // called with entity->data on delete, if > 2 (a real pointer)
    de_manager owner;    // back-pointer to the manager
    uint16_t slot;       // current index into owner->pool[]
    uint16_t tag;        // free-form user field; only touched (reset to 0) by de_manager_new
    uint8_t data[];      // flexible payload, sized by DE_MANAGER_STORAGE's PAYLOAD_SIZE
};

struct de_manager
{
    de_entity *pool;     // capacity pointers into the entity storage block
    uint16_t capacity;
    uint16_t size;       // active-zone boundary
    uint16_t paused;     // paused-zone boundary
};
```

## Public API

### Data access

#### `DE_DATA(TYPE, VAR, ENTITY)`

Declares a new pointer variable and points it at `ENTITY`'s payload, cast to `TYPE *`, in one line:

```c
DE_DATA(enemy_data, d, e); // equivalent to: enemy_data *d = (enemy_data *)e->data;
```

It expands to a plain declaration (`TYPE *VAR = (TYPE *)(ENTITY)->data;`) — unlike the macros in `darksys.h`, it is **not** a statement expression, so it must appear wherever a variable declaration is legal and it does not evaluate to a value.

`ENTITY` must be a `de_entity` (or an expression yielding one) with a live `->data` payload — in practice that's the value returned by `de_manager_new()`, or the `ENTITY` identifier bound inside `DE_MANAGER_FOREACH`. It's purely a shorthand for the cast: it performs no type or bounds checking, so it's on you to pass a `TYPE` that actually matches the `PAYLOAD_SIZE`/layout the entity's manager was created with.

**Not for use inside a `de_state` callback on its raw parameter.** A state function receives `void *data` directly (already `entity->data`, not an entity handle), so it has no `->data` to dereference — just cast that parameter to `TYPE *` yourself. `DE_DATA` is for the places where you're holding an actual `de_entity` (right after `de_manager_new()`, or as `ENTITY` inside `DE_MANAGER_FOREACH`) and want its typed payload without writing the cast by hand.

### Entity lifecycle

#### `void de_entity_exec(de_entity e)`

Calls `e->state(e->data)` once and **discards the result** — `e->state` is left untouched no matter what the call returns. Useful for triggering the entity's current handler as a one-off (e.g. dispatching an event to it) without affecting its normal state progression. No-ops silently if `e` isn't active or its state isn't a real function pointer.

#### `void de_entity_update(de_entity e)`

Calls `e->state(e->data)` and stores the result into `e->state` unless it's `DE_STATE_LOOP`. This is the single-entity version of what `de_manager_update()` does for the whole active zone — **except** it does not act on `DE_STATE_PAUSE`/`DE_STATE_DELETE` results itself. If you call this directly (instead of going through `de_manager_update()`) and the state function returns `DE_STATE_DELETE`/`DE_STATE_PAUSE`, that sentinel just gets stored — the entity is not actually paused or deleted. It will appear "stuck" (subsequent `de_entity_exec`/`de_entity_update` calls become no-ops, since their guards now fail) until something explicitly calls `de_entity_pause`/`de_entity_delete` on it, or until a later `de_manager_update()` pass sweeps it.

#### `void de_entity_pause(de_entity e)`

Moves an **active** entity into the paused zone via two O(1) swaps. No-ops if `e` isn't currently active (including if it's already paused, or still free/unclaimed).

#### `void de_entity_resume(de_entity e)`

Moves a **paused** entity back into the active zone via two O(1) swaps. No-ops if `e` isn't currently paused.

#### `void de_entity_delete(de_entity e)`

Calls `e->destructor(e->data)` if a destructor is set (a real function pointer), then swap-removes `e` from the active zone into the free zone. **Only works on active entities** — a paused entity cannot be deleted directly; `de_entity_resume()` it first, then delete it, or the call will silently no-op.

### Manager

#### `DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)`

Declares an anonymous-struct variable `NAME` holding:

- `pool[CAPACITY]` — the `de_entity` pointer array (filled in by `de_manager_init`).
- `data[...]` — a raw, 4-byte-aligned byte buffer sized `CAPACITY * stride`, where `stride = align4(sizeof(struct de_entity) + PAYLOAD_SIZE)`. This is where entities physically live, one fixed-size slot per capacity unit.

Note that the entity header (`state`, `destructor`, `owner`, `slot`, `tag`) already consumes bytes out of every slot — `PAYLOAD_SIZE` only needs to cover your own data, but size your capacity/memory budget with `sizeof(struct de_entity)` overhead in mind, especially on memory-constrained targets.

#### `DE_MANAGER_ARGS(NAME)`

Expands to `(NAME).pool, (NAME).data, (NAME).capacity, (NAME).payload_size` — exactly the 4 trailing arguments `de_manager_init` expects.

#### `void de_manager_init(de_manager m, de_entity *pool, void *storage, uint16_t capacity, uint16_t payload_size)`

Places one entity at each stride offset in `storage`, records each one's fixed address in `pool[i]`, and sets `size = 0`, `paused = capacity` (everything starts in the free zone). Entities' `state`/`destructor`/`tag` are left uninitialized at this point — that's safe because nothing in the free zone is ever read until `de_manager_new()` claims and initializes it.

#### `de_entity de_manager_new(de_manager m)`

Claims the next free slot, resets `state = DE_STATE_DELETE`, `destructor = 0`, `tag = 0`, and returns it — or returns `0` (NULL) if the manager is full (`size == paused`, no free slots left). **Always check for NULL.**

**⚠️ New entities default to `DE_STATE_DELETE`.** If you don't assign a real function to `entity->state` before the next `de_manager_update()`, the entity is simply deleted (with no destructor call, since `destructor` also defaults to `0`) on that very next update, without ever having run any logic. Configure `state` (and `destructor`/`tag`/payload as needed) immediately after `de_manager_new()` returns.

**Object-pool reuse note:** the `data[]` payload of a reused slot is *not* cleared by `de_manager_new()` — it can still hold whatever the previous occupant left behind. Initialize every field your logic depends on; don't assume zeroed memory. If you need to detect "is this still logically the entity I remember," the `tag` field is available and reset to `0` on creation, but there's no built-in generation counter — you'd need to manage that scheme yourself (e.g. write an incrementing id into `tag` each time you create one, and compare it later).

#### `void de_manager_update(de_manager m)`

Runs one frame/tick: walks the active zone back-to-front, calls each active entity's state function (storing its result unless it's `DE_STATE_LOOP`), and — for entities already carrying a `DE_STATE_PAUSE`/`DE_STATE_DELETE` sentinel left over from a previous call — performs the actual pause/delete. See "Deletion and pause are deferred by one update cycle" above.

#### `DE_MANAGER_FOREACH(m, code)`

Iterates the **active zone only**, back-to-front, binding the fixed identifier `ENTITY` (a `de_entity`) for each one:

```c
DE_MANAGER_FOREACH(&manager, {
    DE_DATA(my_payload, p, ENTITY);
    draw(p);
});
```

Paused entities are never visited. The loop's internal variables are fixed, capitalized names (`INDEX`, `POOL`, `ENTITY`) chosen to reduce (not eliminate) the chance of clashing with your own identifiers — avoid reusing those names inside `code`. The internal swap-removal pattern is proven safe for the manager's *own* self-mutating calls (`de_manager_update`, `de_manager_reset`); if your `code` block deletes or pauses entities *other than* `ENTITY` while inside a `DE_MANAGER_FOREACH`, treat it with the same caution as any other "mutate the container while iterating it" scenario — entries can be skipped or revisited.

#### `void de_manager_reset(de_manager m)`

Deletes every **active** entity (calling destructors via `de_entity_delete`) and then forces `size = 0`, `paused = capacity`.

**⚠️ Paused entities are not destructed.** `DE_MANAGER_FOREACH` (used internally here) never visits the paused zone, so any entities you had paused are simply reclaimed into the free pool by the `paused = capacity` assignment — their `destructor` is never called. If paused entities in your game hold resources that need explicit cleanup, resume (or otherwise handle) them yourself before calling `de_manager_reset()`.

## Usage example

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"

typedef struct { int x, hp; } enemy_data;

void *enemy_idle(enemy_data *e) {
    e->x++;
    if (e->hp <= 0)
        return DE_STATE_DELETE;
    return DE_STATE_LOOP; /* keep running enemy_idle next update */
}

void enemy_cleanup(enemy_data *e) {
    /* free anything e owns, log, etc. */
    (void)e;
}

DE_MANAGER_STORAGE(storage, 64, sizeof(enemy_data));
struct de_manager manager;

void spawn_enemy(void) {
    de_entity e = de_manager_new(&manager);
    if (!e) return; /* pool full */

    e->state = enemy_idle;
    e->destructor = (de_state)enemy_cleanup;

    DE_DATA(enemy_data, d, e);
    d->x = 0;
    d->hp = 10;
}

void game_loop(void) {
    de_manager_init(&manager, DE_MANAGER_ARGS(storage));

    for (;;) {
        de_manager_update(&manager);

        DE_MANAGER_FOREACH(&manager, {
            DE_DATA(enemy_data, d, ENTITY);
            /* draw d */
            (void)d;
        });
    }
}
```

## Other things worth knowing

- **Guards are silent, not diagnostic.** `_DE_ASSERT` is just an early `return` on failure — no crash, no log. Calling an operation on an entity in the wrong zone (deleting a paused entity, resuming an active one, pausing an already-paused one, etc.) simply does nothing, which is safe but can hide logic bugs during development if return values / entity state aren't checked carefully.
- **Capacity ceiling.** `capacity`, `size`, and `paused` are all `uint16_t`, so a single manager tops out at 65535 entities.
- **`destructor` uses the same sentinel check as `state`.** Only values `> (de_state)2` are treated as callable; leaving `destructor` at its default `0` (or accidentally setting it to `DE_STATE_LOOP`/`DE_STATE_PAUSE`) means it is simply never called.
