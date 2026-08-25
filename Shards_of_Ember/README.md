# Shards of Ember

Un RPG completo por turnos, jugable en terminal, construido enteramente sobre
tu motor de entidades **darken.h**. Explora, pelea, sube de nivel, compra en
la tienda, baja a la mazmorra y derrota al Ember Dragon.

## Compilar y jugar

```sh
make
./shards_of_ember
```

(o `make run`). Controles: `w/a/s/d` para moverte, `e` para hablar/interactuar,
`i` para ver tu estado, `q` para salir.

## Cómo usa el motor Darken (por qué está estructurado así)

Todo objeto vivo del juego —jugador, monstruos, ítems en el suelo, el NPC de
la tienda— vive en **un solo `darken` manager** (`g_world`, en `main.c`).
Todos comparten el mismo tamaño de payload: `EntityData`, una `union` de los
cuatro estados posibles (`PlayerData`, `EnemyData`, `ItemData`, `NpcData`).
`entity->tag` (el campo "available to the user") guarda cuál de esos es el
que realmente vive ahí; `entity->usr` (el otro campo libre) guarda a qué mapa
pertenece la entidad (overworld o mazmorra).

Puntos concretos donde se usan las piezas del motor tal como están pensadas:

- **IA de enemigos como FSM real** (`entities.c`): cada enemigo alterna entre
  `enemy_state_patrol` y `enemy_state_chase`. Cada función devuelve
  `DARKEN_LOOP` para seguir en el mismo comportamiento, o el puntero de la
  *otra* función para cambiar de estado — el mecanismo exacto para el que
  existe `darken_state`. Cuando un enemigo que persigue alcanza al jugador,
  llama a `battle_start()` y devuelve `DARKEN_PAUSE`: el motor mismo mueve al
  enemigo a la zona de pausados en su siguiente tick.

- **Pausar/reanudar para cambiar de mapa** (`world.c: switch_map`): en vez de
  destruir y volver a crear las entidades de la mazmorra cada vez que entrás
  o salís, simplemente se pausan las que no son del mapa destino y se
  reanudan las que sí. Como el header garantiza que la dirección de una
  entidad —y por lo tanto sus datos— nunca se mueve mientras está pausada,
  la mazmorra entera queda "congelada" en la zona de pausados mientras
  explorás el overworld, y vuelve exactamente como la dejaste.

- **`destructor` para loot** (`entities.c: enemy_destructor`): al morir un
  enemigo hay una probabilidad de que suelte oro extra. Importante: el motor
  sólo invoca el destructor cuando la entidad se borra desde la zona
  *activa* (ver `darken_entity_delete`); por eso las batallas siempre
  resuelven la muerte del enemigo mientras sigue activo (ver el comentario al
  inicio de `battle.c`), para que el destructor se dispare de forma
  consistente.

- **Recuperar la entidad desde `data`** (`game.h: darken_entity_from_data`):
  los callbacks de estado sólo reciben `entity->data`. Como `struct
  darken_entity` es un tipo completo (no opaco), se puede volver desde el
  miembro flexible hasta la entidad contenedora con `offsetof` — la misma
  garantía de estabilidad de direcciones que documenta el header es lo que
  hace seguro hacer esto desde dentro de un callback.

## Nota técnica sobre la inclusión de darken.h

Las declaraciones de `darken.h` están protegidas por `#ifndef DARKEN_H`, pero
su bloque de implementación (guardado sólo por `#ifdef DARKEN_IMPLEMENTATION`,
sin guard propio) no lo está. `game.h` también incluye `darken.h` (para los
tipos), así que `main.c` hace `#undef DARKEN_IMPLEMENTATION` justo después de
su propio `#include "darken.h"`, para asegurarse de que la implementación se
compile una sola vez en esa unidad de traducción.

## Estructura

```
darken.h      motor de entidades (sin modificar)
game.h        tipos compartidos, payloads de entidad, declaraciones globales
world.c       mapas, render, movimiento/colisión, cambio de mapa
entities.c    spawn de entidades, IA de enemigos, subida de nivel
battle.c      combate por turnos
main.c        implementación del motor, loop principal, tienda
```

## El juego

- Overworld con un NPC ("Mira", vende pociones), ítems para recoger y cuatro
  monstruos que patrullan y persiguen si te acercás.
- Una mazmorra (escaleras `>` / `<`) con enemigos más duros y el jefe final,
  el Ember Dragon.
- Combate por turnos: Atacar, Bola de fuego (gasta MP), Poción, Huir.
- Progresión por experiencia y nivel, oro para comprar pociones.
