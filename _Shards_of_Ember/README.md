# Shards of Ember — SGDK (Sega Genesis / Mega Drive) build

An RPG built on your `darken.h` entity system, running natively on real
hardware via SGDK 2.11 (GCC 13.2 / m68k) — no libc, no `stdio.h`, no
`stdlib.h`, no `printf`. Only `<genesis.h>` and SGDK's own functions.

## What broke, and the actual fix

Your error was:

```
battle.c:16:10: fatal error: stdio.h: No such file or directory
```

That's expected under a bare-metal `m68k-elf` target: there's no OS, so
there's no libc, so `<stdio.h>` simply doesn't exist to find. I rewrote
every file to drop `stdio.h`/`stdlib.h`/a host `string.h`/`time.h` entirely
and use only what `<genesis.h>` provides:

| Instead of              | Use (from `<genesis.h>`)                     |
|--------------------------|-----------------------------------------------|
| `printf`/`snprintf`      | `VDP_drawText(str, x, y)` + SGDK's own `sprintf`/`vsprintf` |
| `scanf`/`fgets`          | `JOY_readJoypad(JOY_1)` polled each frame, edge-detected |
| `rand`/`srand`           | `random()` / `setRandomSeed()`                |
| `memcpy`/`memset`/`strncpy` | same names, but SGDK's own (in `memory.h`/`string.h`, pulled in by `genesis.h`) |

**A second, less obvious problem** would have hit you right after fixing the
first one: `darken.h` itself does `#include <stdint.h>`, which is fine (that
header is freestanding, bundled by the compiler, not part of libc) — but
*if it's included after SGDK's `types.h`*, you get real conflicts like:

```
error: conflicting types for 's8'; have 'signed char'
```

`types.h` defines `uint8_t`/`int8_t`/... as macro aliases for its own
`u8`/`s8`/... the *first* time it's compiled, guarded by
`#if !defined(uint8_t)`. If `<stdint.h>` is included afterward, its own
`typedef ... int8_t;` line gets macro-substituted into `typedef ... s8;`,
colliding with SGDK's earlier real `typedef char s8;`. The fix is just
include order: `game.h` now includes `<stdint.h>` (and `<stddef.h>`, for
`offsetof`) **before** `"genesis.h"`, so the real typedefs are registered
first and `types.h`'s aliasing doesn't step on them. I verified this fix by
compiling every file against the real, current SGDK v2.11 headers (cloned
straight from `github.com/Stephane-D/SGDK`, tag `v2.11`) with a freestanding
m68k GCC 13 — all four files compile with **zero warnings** under the exact
flags SGDK's own `makefile.gen` uses (`-Wall -Wextra -fno-builtin
-fms-extensions -m68000 ...`). I could not fully link a ROM in this
sandbox — the real bare-metal `m68k-elf-gcc`/`libmd.a` toolchain isn't
installable here, only a repurposed Linux-target cross-gcc for the syntax/API
check — so please do a final `make` on your end with your existing SGDK
setup to produce `rom.bin`.

## Drop-in files

```
inc/darken.h   your engine, unmodified
inc/game.h     shared types, entity payloads, globals
src/world.c    maps, VDP text rendering, movement/collision, map switching
src/entities.c entity spawning, enemy AI state machine, leveling
src/battle.c   turn-based combat
src/main.c     SGDK entry point (int main(bool hardReset)), joypad-driven loop
```

Drop `inc/` and `src/` into your existing SGDK project (keep your own
`src/boot/rom_head.c`, `src/boot/sega.s`, and `Makefile`/`makefile.gen`
setup — those already work, per your error being from `battle.c`, further
along in the build). Then `make` as usual.

## Controls

| Screen   | Buttons |
|----------|---------|
| Explore  | D-Pad move · A talk/interact · B status screen |
| Battle   | A attack · B fireball (3 MP) · C potion · START flee |
| Shop     | A buy potion (10g) · B leave |
| Title / end screen | START to begin / play again |

Input is polled once per frame (`JOY_readJoypad` after
`SYS_doVBlankProcess`) and edge-detected (`state & ~prevState`), so holding
a direction doesn't repeat every frame — each press is one turn.

## Engine usage (why it's structured this way)

Same design as the original version, now running for real on the VDP/pad
instead of a terminal:

- **One pool, one payload size.** Every live object (player, monsters,
  ground items, the shopkeeper) lives in one `darken` manager, sharing
  `sizeof(EntityData)` — a union of the four concrete payloads.
  `entity->tag` says which union member is live; `entity->usr` says which
  map the entity belongs to.
- **Enemy AI as a real FSM.** `enemy_state_patrol` / `enemy_state_chase`
  hand control to each other by *returning* the other one's pointer — the
  mechanism `darken_state` exists for. A chasing enemy that reaches the
  player calls `battle_start()` and returns `DARKEN_PAUSE`; darken itself
  parks it in the paused zone on its next tick (see the note at the top of
  `battle.c` about why resolving a kill while the enemy is still "active"
  matters for the destructor).
- **Map switching = pause/resume, not spawn/destroy.** `switch_map()` in
  `world.c` pauses every entity that isn't tagged for the destination map
  and resumes every one that is, relying on darken's guarantee that a
  paused entity's data never moves until resumed or deleted.
- **`darken_reset()` for "press START to play again."** On game over or
  victory, `main()` calls `darken_reset(&g_world)` and respawns the world
  for a clean restart loop.
- **`darken_entity_from_data()`** (in `game.h`) recovers the owning entity
  from a bare `data` pointer via `offsetof`, since `struct darken_entity`
  is a complete (non-opaque) type — used by the AI functions and the loot
  destructor.

## Map / storage sizing

The whole entity pool (`DARKEN_STORAGE(world_storage, 40,
sizeof(EntityData))`) measures **~1.9 KB** total (`pool[]` + `data[]`),
comfortably inside the Genesis's 64 KB of work RAM alongside everything
else. `sizeof(EntityData)` is 24 bytes; the per-entity stride after
darken's 4-byte alignment is 44 bytes.
