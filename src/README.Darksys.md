# Darksys — DARKula SYStems handle-based sparse pool ("slot map")

`darksys.h` is a single-header slot map for C: up to `capacity` records of `params` `void*` fields each, packed contiguously so iteration is a plain linear pointer walk, reached through stable handles that stay valid across removals. Built for GCC on the Motorola 68000 (Sega Genesis / Mega Drive via [SGDK](https://github.com/Stephane-D/SGDK)), and equally usable on a normal host.

Current version: **darksys-1.0.0_dev**

- [Darksys — DARKula SYStems handle-based sparse pool ("slot map")](#darksys--darkula-systems-handle-based-sparse-pool-slot-map)
  - [Core idea](#core-idea)
  - [Storage vs. view — a footgun to know about](#storage-vs-view--a-footgun-to-know-about)
  - [Getting a `darksys_t`](#getting-a-darksys_t)
  - [Adding and removing records](#adding-and-removing-records)
  - [Iterating](#iterating)
  - [Checking handle validity](#checking-handle-validity)
  - [API reference](#api-reference)
  - [Complexity](#complexity)
  - [Limits](#limits)
  - [Gotchas](#gotchas)
  - [Using this from SGDK](#using-this-from-sgdk)

## Core idea

Every active record is reachable through a stable **handle**, kept in sync with the record's current **slot** (its position in the dense `pool[]` array) through two parallel tables:

```
handles[slot]   -> handle
lookup[handle]  -> slot
```

Removing a record moves the *last* active record into the freed slot (swap-and-pop) and updates both tables together, so `pool[]` stays dense — no holes to skip during iteration — while the moved record's own handle keeps working unchanged.

## Storage vs. view — a footgun to know about

`DARKSYS_DECLARE` lays out the raw memory: `pool`/`lookup`/`handles` plus `capacity`/`params`. It does **not** carry `count`/`next`/`free_head` — that bookkeeping triplet lives on the `darksys_t` *view*, not the storage. `DARKSYS_BIND` therefore always hands you a **fresh** view (`count = 0`, `next = 0`, `free_head = INVALID`) over whatever bytes happen to already be in the storage arrays.

Consequence: two `darksys_t` bound to the *same* storage share `pool[]`/`lookup[]`/`handles[]` but each carries its **own** independent `count`/`next`/`free_head`. Using both at once corrupts the tables, since each thinks it alone owns the bookkeeping. Bind once, keep a single `darksys_t` around, and use `darksys_clear()` (or a fresh re-`BIND`) when you want to start over — don't bind a second view over storage that's already in use.

## Getting a `darksys_t`

```c
// Dynamic allocation
darksys_t pool = DARKSYS_ALLOC(malloc, 64, 3);  // 64 records, 3 void* fields each
...
DARKSYS_FREE(free, &pool);

// Static storage
DARKSYS_DECLARE(storage, 64, 3);
darksys_t pool = DARKSYS_BIND(storage);
```

Both `DARKSYS_ALLOC` and `DARKSYS_BIND` expand to a `(darksys_t){...}` compound literal, so they work anywhere a `darksys_t` expression is legal — a declaration initializer, a plain assignment to reassign an existing variable, a function argument, and so on.

`ALLOC` is any `void *(*)(size_t-like)` allocator — `malloc`, or SGDK's `MEM_alloc`.

## Adding and removing records

```c
darksys_handle_t h = DARKSYS_ADD(&pool, sprite, &x, &y);
// h is DARKSYS_INVALID_HANDLE if the arg count didn't match `params`, or the pool is full

if (darksys_valid(&pool, h)) {
    void **row = DARKSYS_DATA(&pool, h);   // row[0..params-1], in write order
}

darksys_remove(&pool, h);   // O(params) -- caller must know h is valid; see Gotchas
```

## Iterating

```c
DARKSYS_FOREACH(&pool, Sprite *sprite, int16_t *x, int16_t *y, {
    sprite->x += *x;
    sprite->y += *y;
});
```

Each bound name can be a full declaration (`Sprite *sprite`) — the macro expands it as `Sprite *sprite = <field>;` each iteration, so there's no need to pre-declare the locals separately. Visits active records in **pool order** (not handle order).

To write a field back in place rather than mutate through a pointer it already holds, assign to `_pool[N]` for the N-th bound field, inside the loop body — that's the one identifier the macro guarantees is in scope, pointing at the current record's row.

## Checking handle validity

```c
uint16_t ok = darksys_valid(&pool, h);
```

Returns nonzero only if `h` was actually issued (`h < next`) *and* the slot it currently maps to still points back to it — the second check is what catches a stale handle whose slot has since been recycled by swap-and-pop or handle reuse. This is the safety net `darksys_remove()` itself doesn't provide (see Gotchas) — call it first whenever a handle's liveness isn't already known some other way.

## API reference

| Symbol                                    | What it does                                                          |
| ----------------------------------------- | --------------------------------------------------------------------- |
| `DARKSYS_ALLOC(alloc, capacity, params)`  | Heap-backed `darksys_t`                                               |
| `DARKSYS_FREE(free, system)`              | Frees what `DARKSYS_ALLOC` allocated                                  |
| `DARKSYS_DECLARE(name, capacity, params)` | Declares static storage (no `darksys_t` view yet)                     |
| `DARKSYS_BIND(name)`                      | Fresh `darksys_t` view over `DARKSYS_DECLARE`d storage                |
| `DARKSYS_ADD(system, ...)`                | Adds a record and writes its fields in one call                       |
| `DARKSYS_DATA(system, handle)`            | Pointer to the first field of `handle`'s record                       |
| `DARKSYS_FOREACH(system, ..., code)`      | Iterates active records, binding one local per field                  |
| `darksys_add(system)`                     | Reserves a slot/handle without writing fields (used by `DARKSYS_ADD`) |
| `darksys_valid(system, handle)`           | Is this handle currently live?                                        |
| `darksys_remove(system, handle)`          | Swap-and-pop removal (handle must be known-valid)                     |
| `darksys_clear(system)`                   | Empties the pool, keeps the storage/capacity                          |

## Complexity

| Operation                     | Cost                                         |
| ----------------------------- | -------------------------------------------- |
| `darksys_add` / `DARKSYS_ADD` | O(1)                                         |
| `darksys_valid`               | O(1)                                         |
| `darksys_remove`              | O(params) — copies the moved record's fields |

## Limits

`capacity`/handles are `uint16_t`, and `DARKSYS_INVALID_HANDLE` is `0xFFFF`, so the largest usable capacity is 65535 (handles `0..0xFFFE`). A literal `capacity >= 65536` truncates silently through the `uint16_t` members; a runtime value that large is undefined behavior you need to avoid yourself.

## Gotchas

- **`darksys_remove()` does not validate its handle.** It's meant for handles already known to be valid (freshly returned by `darksys_add()`/`DARKSYS_ADD`, or checked with `darksys_valid()`). Passing a never-issued handle reads `lookup[]` out of bounds; removing the same handle twice corrupts `count` and the free list. Guard with `darksys_valid()` at any call site where this could happen.
- **Don't bind two views over one storage block at once** (see [Storage vs. view](#storage-vs-view--a-footgun-to-know-about)).
- Values bound by `DARKSYS_FOREACH` are **copies** read out of the pool for that iteration, not aliases into it — for fields that store *pointers to externally-owned data* (the common case, e.g. a `Sprite*` pointing at data you own elsewhere), just write through the pointer as usual. Only if you need to change *which* pointer a field holds for that record do you assign to `_pool[N]` directly.
- Relies on GNU C: statement expressions (`DARKSYS_ADD`). Needs GCC or Clang, not a strict ISO-C-only compiler.

## Using this from SGDK

```c
#include <genesis.h>
#include "darksys.h"

darksys_t pool = DARKSYS_ALLOC(MEM_alloc, 64, 3);
...
DARKSYS_FREE(MEM_free, &pool);
```

**Don't `#include <stdint.h>` yourself in an SGDK project.** This header deliberately does not include it either — SGDK's own `types.h` (pulled in by `<genesis.h>`) is SGDK's documented stand-in for `<stdint.h>`, defining `uint8_t`/`uint16_t`/`uint32_t` as compatibility macros over its native `u8`/`u16`/`u32`. Bringing in a real `<stdint.h>` on top of that has been confirmed to produce hard `conflicting types` errors on at least one toolchain. Outside SGDK, just `#include <stdint.h>` before this header — it isn't included for you.
