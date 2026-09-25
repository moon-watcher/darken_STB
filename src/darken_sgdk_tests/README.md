# Darken 1.4 — Mega Drive / 68K SGDK tests and benchmarks

This is a small SGDK ROM project intended specifically for Darken 1.4 on the
Mega Drive / Genesis 68000 target.

Copy `darken.h` into `src/` before building.

## Main ROM

`src/main.c` tests and benchmarks:

- `darken_init()`
- `DARKEN_SPAWN()`
- full active/free capacity behavior
- `darken_update()` in STATE-MACHINE mode
- state callback replacement
- `DARKEN_DELETE`
- `destroy`
- slot recycling without implicit clearingv
- reverse `DARKEN_FOREACH`
- deletion during reverse iteration
- same/cross-context `darken_entity_swap()`
- active `darken_entity_migrate()`
- free-entity `darken_entity_migrate()`
- payload/header preservation
- benchmark of spawn/delete
- benchmark of update
- benchmark of foreach
- benchmark of swap
- benchmark of migrate

The benchmark uses SGDK's `getSubTick()` timer. SGDK documents it as a
1/76800-second timer and notes its VBlank limitation, so timing is sampled
around the benchmark block rather than around each individual operation.

## DIRECT-mode ROM

`direct/src/main.c` is the same kind of smoke test with:

```c
#define DARKEN_DIRECT
```

It verifies the direct callback ABI and explicit `darken_entity_delete()` path.

## Build

The project is intentionally SGDK-only. It does not use:

- stdio
- stdlib
- string.h
- custom timing code
- Mega Drive hardware registers
- 68K inline assembly

Use the normal SGDK build command, with the SGDK makefile:

```text
<SGDK_PATH>\bin\make -f <SGDK_PATH>\makefile.gen
```

The current SGDK documentation describes `makefile.gen` as the normal project
build path.

For the DIRECT ROM, build the `direct/` directory as a separate SGDK project.

## Reading benchmark numbers

The benchmark reports elapsed SGDK subticks and integer subticks per operation.
Because integer division is used, the displayed per-operation value is rounded
down. The useful comparison is to keep:

- the same SGDK version,
- the same compiler optimization/profile,
- the same emulator or real Mega Drive,
- the same PAL/NTSC mode,
- the same benchmark constants.

For cycle-level 68000 analysis, use the SGDK-generated assembly listing in a
separate build; the ROM benchmark is intended to measure the complete C-level
operation in the actual target environment.
