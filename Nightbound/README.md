# Nightbound — SGDK / Mega Drive

Base de un plataformas 2D usando el `darken.h` proporcionado por el proyecto Darken.

## Estructura

```text
src/
  main.c
  darken.c                  <- ÚNICA unidad con DARKEN_IMPLEMENTATION
  game/
    game.c game.h
  entities/
    entity.c entity.h
    player.c player.h
    enemy.c enemy.h
    coin.c coin.h
    bullet.c bullet.h
  world/
    level.c level.h
    collision.c collision.h
  systems/
    input.c input.h
    camera.c camera.h
inc/
  darken.h
res/
  resources.res
```

## Regla de includes de Darken

`darken.h` puede incluirse desde cualquier header o `.c` del proyecto, pero `DARKEN_IMPLEMENTATION` se define **una sola vez**, en `src/darken.c`:

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
```

No se define `DARKEN_IMPLEMENTATION` en `main.c` ni en ningún header.

Esto permite usar el header de Darken con su modelo STB-style sin producir redefiniciones de `_darken_entity_swap`, `darken_init`, `darken_spawn`, etc.

## Compilación

Usa el `makefile.gen` con tu SGDK habitual:

```bat
%GDK%\bin\make -f %GDK%\makefile.gen -j1 debug
```

## Controles

- Derecha/Izquierda: movimiento.
- A/B/C: salto.
- START: pausa.
- Abajo: reinicio del nivel cuando el jugador muere.
