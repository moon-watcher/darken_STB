# Darken Tactics — an SGDK example for darken.h

A tiny turn-based tactical RPG for Sega Genesis / Mega Drive, in the
spirit of Shining Force 2: a grid battlefield, a party of 3 heroes, a
squad of 3 monsters, move + optionally attack on your turn, then a simple
AI takes the enemy turn. It exists as a companion example for `darken.h`,
showing the library used for something other than an action-game update
loop.

## Files

```
darken_tactics/
├── EXAMPLE_README.md   — this file
├── src/
│   ├── darken.h         — the library (unmodified)
│   └── main.c           — the game, heavily commented
└── res/
    ├── resources.res     — SGDK resource script
    ├── battlefield.png   — placeholder background (320x224)
    ├── hero.png          — placeholder hero sprite (16x16)
    ├── enemy.png         — placeholder enemy sprite (16x16)
    └── cursor.png        — placeholder cursor sprite (16x16)
```

All four PNGs are simple flat-colour placeholders — swap them for real art
without touching `main.c`, as long as you keep the sprites at 2x2 tiles
(16x16px).

## Controls

| Button | Action |
|---|---|
| D-Pad | Move the cursor, or the unit you're currently moving |
| A | Select a unit / confirm a move / confirm an attack |
| B | Cancel a move while you're still choosing where to go |
| START | End your phase early, even with units left to act |

## Why `DARKEN_DIRECT`

`darken.h` defaults to STATE-MACHINE mode, where `update()` returns a
value (`DARKEN_CONTINUE`, `DARKEN_DELETE`, `DARKEN_PAUSE`, or a new state)
and the engine drives the entity's lifecycle for you. That model fits
things that behave like little autonomous state machines — patrol →
chase → attack, that kind of loop.

A tactics-game unit doesn't really behave that way. Almost everything
that happens to one is commanded from the outside by the turn manager:
move here, attack that, you're done for the round, you died. Nearly every
one of those call sites needs the `darken_entity` handle in hand, so
`DARKEN_DIRECT` — which hands `update()`/`destroy()` the entity directly —
is the better fit here. `main.c`'s opening comment goes through this in
more detail, along with a full list of which parts of the public API this
example actually exercises.

## The interesting bit: reusing pause/resume as "acted this round"

The example uses `darken_entity_pause()` to mean "this unit already acted
this round," which removes it from `darken_update()`/`DARKEN_FOREACH`
without deleting it — and resumes the whole team at the start of their
next phase. It's a genuinely good fit for the mechanic, but it surfaces
two things worth knowing about if you try something similar:

1. **`darken_entity_delete()` skips `destroy()` for paused entities.**
   A unit can absolutely die while "resting" (paused) mid-enemy-turn.
   `main.c`'s `kill_unit()` always resumes before deleting, specifically
   so `destroy()` (which frees the unit's hardware sprite) always runs.
   Skipping this would leak a sprite slot every time a unit died on
   someone else's turn.

2. **Resuming "every paused unit of team X" isn't a simple forward scan.**
   `darken_entity_resume()` reshuffles the paused zone via swaps, so a
   naive "check index i, resume-or-advance" loop can end up re-reading a
   slot that just became something else entirely. `main.c`'s
   `resume_team()` has the full explanation, and it was checked against a
   quick fuzz test during development: the naive version failed (an
   infinite-loop guard tripped) in just over half of 1000 randomised
   team/order combinations against the real `darken.h`; the corrected
   version — rescan-from-the-boundary-after-every-resume — had zero
   failures.

## Building this for real hardware / an emulator

You'll need [SGDK](https://github.com/Stephane-D/SGDK) and its bundled
`m68k-elf-gcc` toolchain. With SGDK set up (its own README covers
Docker-based and native setups):

1. Drop this whole `darken_tactics/` folder into your SGDK workspace
   (or copy `src/` and `res/` into an existing SGDK project skeleton —
   SGDK projects expect `src/` and `res/` at the project root, alongside
   its `makefile.gen`).
2. `rescomp` runs automatically as part of the build and turns
   `res/resources.res` into `res/resources.h`/`.c` — `main.c` includes
   `resources.h` expecting exactly that.
3. Build with SGDK's make setup (typically `make -f
   %SGDK%/makefile.gen`, or via the Docker image / VS Code task SGDK
   ships with).
4. Run the resulting `.bin`/`rom.bin` in an emulator with good accuracy
   (BlastEm, Gens KMod) or on a flash cart.

## What's actually been verified, and what hasn't

Being upfront about this, since it matters for an example someone might
build on:

- **`darken.h` itself**: extensively compiled and unit-tested with a
  regular PC compiler (gcc, with AddressSanitizer) throughout its
  development — both modes, pause/resume/delete edge cases, the exact
  zone-swap mechanics `main.c` relies on.
- **`main.c`'s game logic**: carefully written against SGDK's current,
  documented API (sprite engine, joypad, palettes, `IMAGE`/`SPRITE`
  resource directives), and syntax/type-checked end-to-end by compiling
  it against a small stand-in `genesis.h`/`resources.h` on a regular
  compiler — that catches typos, type mismatches, and undeclared
  identifiers, but obviously doesn't exercise real graphics, real timing,
  or real controller input. The trickiest piece of logic (`resume_team`)
  was additionally fuzz-tested in isolation against the real `darken.h`.
- **Not done**: an actual build with `m68k-elf-gcc`/`rescomp`, and a run
  on real hardware or in an emulator. Neither toolchain was available in
  the environment this was written in. If something doesn't compile or
  look right in a real SGDK build, that's the part to double-check first.
