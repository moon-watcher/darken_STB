# Darken

**Darken (DARKula ENgine) 2.0 Entity System — Sega Genesis / Motorola 68000**

`darken.h` is a single-header C entity/manager library designed for the
Sega Genesis, with a focus on predictable memory usage, contiguous storage,
constant-time entity operations and inexpensive per-frame updates on the
Motorola 68000.

The library is intentionally small: a manager owns a fixed-capacity set of
entities, each entity has a state function and optional payload, and an
optional system layer can process entity-related data in batches.

---

## Table of contents

- [Basic usage](#basic-usage)
- [Naming convention](#naming-convention)
- [Architecture](#architecture)
- [Entity](#entity)
- [Entity lifecycle](#entity-lifecycle)
- [Special states](#special-states)
- [Manager](#manager)
- [Manager storage](#manager-storage)
- [Pause and resume](#pause-and-resume)
- [Deletion](#deletion)
- [Iteration](#iteration)
- [Filtered apply](#filtered-apply)
- [Systems](#systems-darkensystems)
- [Memory layout and alignment](#memory-layout-and-alignment)
- [Performance](#performance)
- [Limits and caveats](#limits-and-caveats)
- [API summary](#api-summary)
- [License](#license)

---

# Basic usage

## Implementation

`darken.h` follows the single-header pattern.

In **one** `.c` file:

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"