# Darken 2.0 — Appendices

## 🚀 Appendix A: Hello World in 30 Lines

The minimum program that creates an entity, gives it a state, and makes it count to 5 before deleting itself.

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdio.h>

typedef struct { int counter; } Data;

void *count(Data *d) {
    printf("Counter: %d\n", d->counter++);
    return d->counter > 5 ? DE_STATE_DELETE : DE_STATE_LOOP;
}

int main(void) {
    DE_MANAGER_STORAGE(m, 10, sizeof(Data));
    struct de_manager manager;
    de_manager_init(&manager, DE_MANAGER_ARGS(m));

    de_entity e = de_manager_new(&manager);
    ((Data *)e->data)->counter = 1;
    e->state = (de_state)count;

    while (manager.size > 0) {
        de_manager_update(&manager);
    }

    printf("It deleted itself. Magic.\n");
    return 0;
}
```

Compile:
```bash
gcc -std=gnu99 hello.c -o hello && ./hello
```

**Expected output:**
```
Counter: 1
Counter: 2
Counter: 3
Counter: 4
Counter: 5
Counter: 6
It deleted itself. Magic.
```

That's it. One entity, one state, one loop. Everything else is details.

---

## 💀 Appendix B: Common Mistakes and How Not to Break Everything

### 1. "I forgot `#define DARKEN_IMPLEMENTATION`"
Without this, the compiler throws `undefined reference` on every function (`de_manager_update`, `de_entity_delete`, etc.). Half the header is macros and types; the other half (functions with bodies) only appears if you define that macro **before** the `#include`, in **exactly one** `.c` file.

```c
/* Good */
#define DARKEN_IMPLEMENTATION
#include "darken.h"

/* Bad: you put it in the .h or nowhere */
```

### 2. "`DE_MANAGER_FOREACH` + `de_entity_delete` = 💥"
The `DE_MANAGER_FOREACH` macro iterates with `while (INDEX--)` over the pool. If inside you call `de_entity_delete(ENTITY)`, the manager swaps with the last active entity and shrinks `size`. But the foreach already cached `INDEX` and `POOL` at the start. Result: you might skip entities or visit the same one twice.

**Golden rule:** Never delete inside a `DE_MANAGER_FOREACH`. If you want to kill something manually, do it **after** the foreach, or use `de_manager_update()` and let the entity's own `state` return `DE_STATE_DELETE`.

### 3. "I create 50 entities and suddenly `de_manager_new` returns `NULL`"
The manager has three zones: `[active][free][paused]`. `de_manager_new` only takes from the middle. If you paused 40 entities and have capacity 50, you only have 10 free slots left. If `size == paused`, the club is full.

**Fix:** Check `manager.paused - manager.size` to see how many free slots remain. If you pause a lot, eventually you run out of room for new stuff.

### 4. "I keep a pointer to `entity->data` and then the entity dies"
Pointers to `data[]` are **stable only for paused entities**. If the entity is active and `de_entity_delete` or `de_manager_update` destroys it, that pointer you stored in your `de_system` or in another entity now points at a corpse (or worse, another entity that took that slot).

**Fix:** If a `de_system` needs to point at data safely, make sure the entity is **paused** (`de_entity_pause`). Or better, rebuild the system's pointers every frame from the manager.

### 5. "`de_system_remove` finds nothing"
`de_system_remove(sys, ptr)` looks for a group whose **first** pointer (`pool[i]`) equals `ptr`. If you stored `(pos, vel)` and try to search by `vel`, it won't work. Always search by the first element of the group.

### 6. "My state returns `0` and the entity doesn't die"
`0` **is** `DE_STATE_DELETE`, but if your function accidentally returns `NULL` (which is `0`), the entity dies. If your state returns a pointer to another function, make sure it's not `NULL`, `1`, or `2`, because those are the engine's magic values.

### 7. "`de_manager_new` gives me entities with garbage in `data[]`"
Darken **does not initialize your payload**. It gives you the entity with `state = DE_STATE_DELETE`, `destructor = 0`, `tag = 0`, but `data[]` has whatever the previous tenant left behind. If you don't initialize your fields, you'll get random values.

### 8. "I use `uint16_t` for everything and the index overflows"
`slot`, `size`, `capacity`, etc. are `uint16_t`. If you do `slot - 1` when `slot` is `0`, you're in trouble (underflow to 65535). The engine handles this internally, but if you touch indices by hand, watch the edges.

### 9. "I pause an entity and it still shows up in my `DE_MANAGER_FOREACH`"
`DE_MANAGER_FOREACH` only walks the active zone `[0, size)`. If you paused something, it moved to `[paused, capacity)`. It shouldn't show up... unless you're iterating over `pool[]` manually without checking boundaries.

### 10. "My `de_system` has weird `capacity`"
`de_system_init` receives `capacity_groups` (how many groups you want) and `params` (how many pointers per group). The **total** pointer capacity is `groups * params`. If you reserve `DE_SYSTEM_STORAGE(sys, 10, 3)`, you have room for 10 groups of 3 pointers, not 30 groups.

---

**Summary to remember:**

| ✅ Do this | ❌ Don't do this |
|---|---|
| `#define DARKEN_IMPLEMENTATION` in exactly one `.c` | Forget it and cry at linker errors |
| Delete entities by returning `DE_STATE_DELETE` from their `state` | Call `de_entity_delete` inside a `DE_MANAGER_FOREACH` |
| Pause entities if you want stable pointers to their `data` | Keep pointers to `data` of active entities |
| Initialize your payload after `de_manager_new` | Assume `data[]` comes zeroed |
| Use `de_system_remove` searching by the **first** pointer of the group | Search by the second, third, etc. |

With these rules, Darken is practically impossible to break. Or well, harder. A bit. 🎮
