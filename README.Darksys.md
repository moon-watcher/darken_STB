# Darksys — Flat Pointer-Group Pool

`darksys.h` is a tiny, allocation-free container that packs fixed-size **groups of pointers** ("tuples") back-to-back inside one flat `void **` array. It has no idea what the pointers point to — it just keeps `N` pointers together as one indivisible unit, lets you add units, scan them, and remove one by matching its first pointer.

Think of it as a minimal "structure of tuples" store, useful for tracking auxiliary associations between opaque objects — e.g. `(entity, timer)` pairs, `(a, b)` collision pairs, `(listener, context)` bindings — without needing a dedicated struct or `malloc`. It's a companion to `darken.h` (both belong to the DARKula ENgine), but it is fully standalone and has no dependency on it.

## Design at a glance

- No dynamic allocation: the caller supplies the backing storage (typically via `DE_SYSTEM_STORAGE`).
- Data is untyped: everything is stored/read as `void *`; you decide what to cast it to.
- Single flat array, fixed capacity, no growth.
- `params` (the group width) is a *runtime* value of the `de_system` instance, decided once at `de_system_init` and never re-checked afterwards.

## Compatibility / build

- `de_system_add` expands to a GNU C **statement expression** (`({ ... })`), so this header needs GCC or Clang (or another compiler with that extension). It will not compile as strict ISO C on, e.g., MSVC.
- This is a single-header library in the classic "implementation macro" style: include the header normally wherever you need the declarations, and additionally `#define DARKSYS_IMPLEMENTATION` before **exactly one** `#include "darksys.h"` in the whole program. The implementation functions (`de_system_init`, `de_system_remove`, `de_system_clear`) are ordinary external functions with no `inline`/`static`, so defining `DARKSYS_IMPLEMENTATION` in more than one translation unit will cause "multiple definition" linker errors.

## Memory layout

For `params = 2`, a system with 3 groups added looks like this in `pool`:

```
index:   0     1     2     3     4     5
value: [A.a] [A.b] [B.a] [B.b] [C.a] [C.b]
        \___________/  \___________/  \___________/
           group A         group B        group C
```

`size` always holds the number of **occupied pointer slots** (not groups): after adding 3 groups of width 2, `size == 6`. `capacity` is the maximum number of pointer slots the pool can hold, i.e. `capacity_groups * params`.

## Data structure

```c
struct de_system
{
    void **pool;       // flat backing storage, capacity slots wide
    uint16_t capacity;  // total pointer slots available (NOT groups)
    uint16_t size;      // pointer slots currently used (NOT groups)
    uint16_t params;    // pointers per group ("tuple width")
};

typedef struct de_system *de_system;
```

`de_system` is a pointer typedef — every function takes a `de_system` value, i.e. the address of a `struct de_system` you own.

## Public API

### `DE_SYSTEM_STORAGE(NAME, CAPACITY, PARAMS)`

Declares (and defines) an anonymous-struct variable called `NAME`, sized to hold `CAPACITY` groups of `PARAMS` pointers each:

```c
DE_SYSTEM_STORAGE(pairs_storage, 32, 2); // room for 32 groups of 2 pointers
```

This expands roughly to:

```c
struct { void *pool[CAPACITY*PARAMS]; uint16_t capacity; uint16_t params; }
    NAME = { .capacity = CAPACITY, .params = PARAMS };
```

It only creates the raw storage block, not a working `de_system` — you still need `de_system_init`.

**Unit mismatch to be aware of:** the `.capacity` field set here is expressed in **groups**, while `de_system::capacity` (set later by `de_system_init`) ends up expressed in **raw pointer slots** (`groups * params`). Both fields share the name `capacity` but not the unit — don't read `NAME.capacity` expecting the same number you'll see in `sys->capacity` after init.

### `DE_SYSTEM_ARGS(NAME)`

Expands to `(NAME).pool, (NAME).capacity, (NAME).params` — exactly the 3 trailing arguments `de_system_init` expects. Meant to be used directly as call arguments:

```c
de_system_init(&sys, DE_SYSTEM_ARGS(pairs_storage));
```

### `void de_system_init(de_system sys, void **pool, uint16_t capacity_groups, uint16_t params)`

Wires a `de_system` instance to a storage block, sets `size = 0`, and converts `capacity_groups` into the internal `capacity` (raw slot count) by multiplying by `params`. Must be called once before any other operation on `sys`.

### `de_system_add(sys, ...)`

Appends one group. The number of trailing arguments (1 to 5) becomes the width of the group written:

```c
de_system_add(&sys, ptr_a, ptr_b); // writes a 2-pointer group, evaluates to 1 or 0
```

It expands (via `_de_system_add_1` … `_de_system_add_5`, selected by counting `__VA_ARGS__`) into a statement expression that:

1. Checks `size + N <= capacity`, where `N` is the literal number of arguments given *at this call site* (1–5).
2. If there's room, writes the `N` pointers starting at `pool + size`, advances `size += N`, and evaluates to `1`.
3. Otherwise evaluates to `0` without writing anything.

Always check the return value — a full pool silently drops the add.

**⚠️ Important — argument count is not validated against `params`.** `N` above is a compile-time literal (how many values you wrote at the call site), completely independent from the runtime `params` value stored in `sys` during `de_system_init`. If you initialize a system with `params = 3` but call `de_system_add(&sys, a, b)` (2 args), the call happily writes 2 pointers and advances `size` by 2 — silently breaking the fixed-width grouping that `DE_SYSTEM_FOREACH` and `de_system_remove` both rely on (they stride by the *real* `params`). From that point on, every subsequent read is misaligned relative to the intended groups. There is no assertion or runtime check for this; it is entirely the caller's responsibility to always pass exactly `params` arguments to `de_system_add` for a given system.

### `DE_SYSTEM_FOREACH(sys, [a[, b[, c[, d[, e]]]]], code)`

Iterates every group in `sys`, binding up to 5 pre-declared variables to the group's pointer slots and running `code` once per group:

```c
void *a, *b;
DE_SYSTEM_FOREACH(&sys, a, b, {
    use(a, b);
});
```

The bind variables (`a`, `b`, …) must already be declared, with a type compatible with a plain assignment from `void *`, before the call — the macro only assigns to them (`a = pool[0];`), it doesn't declare them. It reads `sys->params` at runtime to know the stride, so it iterates correctly regardless of `params`'s value — as long as every `de_system_add` call for that system used matching width (see caveat above).

Internally it uses `_system`, `_pool`, `_size`, `_params`, `_return` as temporary names inside a `do { } while(0)` block / statement expression. Avoid reusing those exact identifiers as your bind variables or inside `code`, since C macros aren't hygienic and you could shadow or clash with them.

### `uint16_t de_system_remove(de_system sys, void *first)`

Removes the first group whose *first* pointer equals `first`. The search runs backward (from the last group toward the first), so with duplicate first-pointers you don't control which one is removed on a given call, and duplicates are not all removed in one call — call it repeatedly if that's needed. Removal is O(n) (linear scan over groups) and uses swap-with-last-group to keep the array packed, so group order is not preserved across removals. Returns `1` if something was removed, `0` if `first` wasn't found.

### `void de_system_clear(de_system sys)`

Resets `size` to `0`. Existing pointer values in `pool` are left untouched in memory (they're just no longer considered "in use") — nothing is zeroed.

## Usage example

```c
#define DARKSYS_IMPLEMENTATION
#include "darksys.h"

DE_SYSTEM_STORAGE(pairs_storage, 32, 2); /* 32 groups, 2 pointers each */
struct de_system pairs;

void example(void)
{
    de_system_init(&pairs, DE_SYSTEM_ARGS(pairs_storage));

    void *entity = (void *)0x1000;
    void *timer  = (void *)0x2000;

    if (!de_system_add(&pairs, entity, timer))
    {
        /* pool full: 32 groups already stored */
    }

    void *e, *t;
    DE_SYSTEM_FOREACH(&pairs, e, t, {
        printf("entity=%p timer=%p\n", e, t);
    });

    de_system_remove(&pairs, entity);
    de_system_clear(&pairs);
}
```

## Other things worth knowing

- **No type safety.** Everything goes in and out as `void *`; the `ADD` macros cast for you (`(void *)(A)`), but reading requires you to cast back to the correct type yourself. `darksys.h` has no idea what you're actually storing.
- **No random access.** There is no "get group by index" call — only add, remove-by-first-pointer, foreach, and clear. It's meant for scan-and-associate use cases, not indexed lookup.
- **Not thread-safe.** No locking of any kind; concurrent add/remove/foreach from multiple threads is unsafe.
- **Integer sizes.** `capacity`, `size`, and `params` are all `uint16_t`, so a single system tops out at 65535 pointer slots.
