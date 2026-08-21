# Capture The Flag — Example Game with Darken 2.0

A complete **Capture The Flag** terminal game with two human players, three enemy types with distinct AI, and the Darken 2.0 engine managing everything.

---

## 🎮 How to Play

Compile this alongside your `darken.h`:

```bash
gcc -std=gnu99 capture.c -o ctf -lm
./ctf
```

**Controls:**
- **P1** (symbol `1`): `W A S D`
- **P2** (symbol `2`): `I J K L`
- **Quit**: `Q`

First one to touch the `$` wins. If an enemy touches you, you lose 1 of 3 lives and respawn. If you run out, the other player wins.

---

## 🧠 The Three AI Types

| Enemy | Symbol | Behavior |
|-------|--------|----------|
| **Chaser** | `C` | Hunts the closest player relentlessly. |
| **Patroller** | `P` | Changes direction every ~1.5 seconds, patrolling like a neighborhood cop. |
| **Blocker** | `B` | Positions itself halfway between the closest player and the flag. |

---

## 🎯 How Darken Is Used

| Feature | Where |
|---------|-------|
| **`de_manager_update()`** | Every frame in the main loop. The "heartbeat" that executes everyone's state. |
| **`de_state` (callbacks)** | Every entity has `game_state` as its callback. Movement, friction, and AI calls live inside it. |
| **`de_system` (render)** | `render_sys` is a flat array of pointers `(x, y, symbol)`. It auto-updates because it points directly at entity fields. |
| **`DE_MANAGER_FOREACH`** | Used in `check_collisions` to walk players and enemies without worrying about who died or who was just created. |
| **`DE_SYSTEM_ADD`** | Every time we create an entity, we register it in the render system with its 3 pointers. |
| **`DE_SYSTEM_FOREACH`** | Used in `render_frame` to iterate the render system with 3 unpacked pointers per group. |

---

## 📁 Code Structure

```
capture.c
├── Non-blocking input (kbhit, getch_noblock)
├── EntityData (payload for every entity)
├── Enemy AI
│   ├── ai_chaser()      → hunts closest player
│   ├── ai_patroller()   → changes direction every 50 frames
│   └── ai_blocker()     → blocks path to flag
├── game_state()         → de_state executed by de_manager_update()
├── create_entity()      → factory using de_manager_new + DE_SYSTEM_ADD
├── check_collisions()   → nested DE_MANAGER_FOREACH (players vs enemies)
├── render_frame()       → draws via DE_SYSTEM_FOREACH
└── main()               → game loop (~30 FPS)
```

---

## 🔧 Technical Notes

- Players have **friction** (`vx *= 0.85f`) so they don't slide forever.
- **Invulnerability** lasts 45 frames (~0.75s) after being hit to prevent damage spam.
- Enemies use **global coordinates** (`g_p1_x`, `g_p2_x`) to compute distances without searching the manager.
- Rendering uses **ANSI escape codes** to clear screen and position the cursor.
- The render system uses `DE_SYSTEM_FOREACH` with 3 unpacked pointers:
  ```c
  DE_SYSTEM_FOREACH(ren, float *px, float *py, char *sym, {
      /* draw at (*px, *py) using *sym */
  });
  ```

---

## 🚀 Ideas for Extension

- **Power-ups:** Items that freeze enemies (use `de_entity_pause`) or give speed boost.
- **Waves:** Spawn more enemies progressively using `de_manager_new` dynamically.
- **Destructors:** Add `e->destructor` that plays a sound or spawns particles on death.
- **More systems:** A `de_system` for sound, one for networking, one for persistence.

---

## API Notes

This code uses the **latest** `darken.h` API:

| Pattern | Usage |
|---------|-------|
| `struct de_manager manager;` | Concrete struct, not pointer |
| `struct de_system renderer;` | Concrete struct, not pointer |
| `_DE_ENTITY_IS_ACTIVE(e)` | Zone query macros |
| `DE_SYSTEM_FOREACH(sys, A, B, { code })` | Public variadic iteration |
| `DE_SYSTEM_ITERATOR_2(name, A, B, { code })` | Generate system functions |
| `void *foo(MyType *t)` | Typed state callbacks (cast to `de_state`) |

---

*Built with Darken 2.0. No malloc, no drama.* 🏴
