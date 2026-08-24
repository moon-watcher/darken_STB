# Darken 2.0 — Guía de uso para un RPG pequeño

> *Darken* es un gestor de entidades mínimo en C puro. No hay clases, no hay herencia, no hay `malloc` en caliente. Solo punteros a funciones, memoria contigua y tres zonas lógicas que hacen la magia.
>
> Esta guía te muestra cómo construir un RPG sencillo —con héroes, enemigos, pociones y menús de pausa— usando cada pieza del API público.

---

## Tabla de contenidos

1. [El mundo del juego](#1-el-mundo-del-juego)
2. [Macros de control y estados](#2-macros-de-control-y-estados)
3. [Macros de introspección](#3-macros-de-introspección)
4. [Ciclo de vida de una entidad](#4-ciclo-de-vida-de-una-entidad)
5. [El manager y el bucle principal](#5-el-manager-y-el-bucle-principal)
6. [Características del motor](#6-características-del-motor)
7. [Avisos y consejos de uso](#7-avisos-y-consejos-de-uso)

---

## El mundo del juego

Imagina un RPG clásico de vista cenital. Existen tres tipos de cosas en pantalla:

- **Héroe y enemigos**: se mueven, atacan, reciben daño. Son *entidades activas*.
- **Cofres y puertas**: esperan a que el jugador interactúe. Cuando se abren, desaparecen o cambian de estado. Pueden *pausarse* si sales de la habitación.
- **Menús y diálogos**: cuando abres el inventario, el mundo sige existiendo pero *no se actualiza*. Las entidades del mundo pasan a la zona de pausa.

Todas estas cosas son entidades gestionadas por un único `darken` manager.

### La estructura de datos del jugador

Cada entidad lleva un `data[]` flexible al final de su estructura. Para un personaje del RPG podría ser:

```c
struct hero_data{
    int16_t x, y;       // Posición en el mapa
    int16_t hp, max_hp; // Vida
    uint16_t atk, def;  // Estadísticas
    uint8_t  facing;    // Dirección (0=N,1=E,2=S,3=O)
    uint8_t  is_boss;   // 1 si es jefe final
};
```

---
## Paso a paso

### `DARKEN_STORAGE(nombre, capacidad, tamaño_payload)`

Declara el almacenamiento.

Reserva la memoria en el stack o en el segmento de datos (según dónde declares la variable) sin fragmentación ni asignación dinámica de memoria.
 
```c
// Hasta 64 entidades cada una de 50 bytes tamaño.
DARKEN_STORAGE(rpg_world, 64, 50);
```

### `DARKEN_ARGS(nombre)`

Expande los argumentos que necesita `darken_init()` a partir de una variable creada con `DARKEN_STORAGE`.

```c
DARKEN_ARGS(rpg_world);
```

### `darken_init(manager, pool, storage, capacidad, bytes_payload)`

Inicializa el manager. Se llama una sola vez.

Particiona el bloque de memoria en entidades contiguas, rellena el pool de punteros y deja todo listo para `darken_spawn()`.

```c
darken world;

/**
 * Equivalente a:
 * darken_init(&world, rpg_world.pool, rpg_world.data, 64, 32);
 */
darken_init(&world, DARKEN_ARGS(rpg_world));
```

### `darken_spawn(manager)`

Devuelve una entidad libre del pool.

```c
darken_entity hero = darken_spawn(world);
```

<!-- A partir de aquí, tú decides qué es: un ```hero```, una poción, una partícula de sangre. -->

### `DARKEN_DATA(Tipo, variable, entidad)`

Declara un puntero local al *payload* de una entidad, ya casteado al tipo que necesitas.

Hace el código legible y evita errores de tipeo.

```c
/**
 * Equivalente a:
 * struct hero_data *h = (struct hero_data *) entity->data;
 */ 
DARKEN_DATA(struct hero_data, h, entity);
```

### `darken_update(manager)`

Recorre las entidades activas de atrás hacia adelante, ejecuta sus estados, y aplica transiciones (borrado, pausa, cambio de estado).

```c
while (game_running) {
    read_input();
    darken_update(&world); // Actualiza todas las entidades
    render_frame();
    vsync_wait();
}
```

### `darken_reset(manager)`

Borra las entidades activas, ejecutando sus destructores, y devuelve el manager a su estado inicial.

```c
darken_reset(&world);
```







**Uso en el RPG:**
```c
DARKEN_STORAGE(rpg_world, 64, 50);

darken world;
darken_init(&world, DARKEN_ARGS(rpg_world));

darken_entity hero = darken_spawn(world);
DARKEN_DATA(struct hero_data, hero, entity);

hero_data->x = x;
hero_data->y = y;
hero_data->hp = 20;

hero->state = state_slime_idle;
hero->destructor = destructor_slime;  // Libera recursos gráficos si hace falta
hero->tag = TAG_ENEMY;                // Para colisiones y filtros
hero->usr = 0;                        // Lo usaremos como contador de frames de invencibilidad

while (game_running) {
    read_input();
    darken_update(&world); // Actualiza todas las entidades
    render_frame();
    vsync_wait();
}

darken_reset(&world);
```



---





///////////////////////////////////////

---



## Macros de control y estados


---

### `DARKEN_LOOP`

Devuélvelo desde tu callback de estado para indicar: *"este frame no ha pasado nada especial, repite el mismo estado el próximo frame"*.

**Uso en el RPG:**
```c
void *state_enemy_patrol(void *data) {
    DARKEN_DATA(enemy_data, e, entity);
    e->x += patrol_speed[e->facing];

    // El enemigo sigue patrullando...
    return DARKEN_LOOP;
}
```

Es el valor por defecto para la mayoría de estados: caminar, esperar, animar.

---

### `DARKEN_DELETE`

Devuélvelo cuando la entidad debe desaparecer del mundo. El manager la moverá a la zona libre y, si tiene `destructor` asignado, lo ejecutará antes.

**Uso en el RPG:**
```c
void *state_potion_effect(void *data) {
    DARKEN_DATA(potion_data, p, entity);

    p->timer--;
    if (p->timer == 0) {
        spawn_heal_particles(p->x, p->y);
        return DARKEN_DELETE;   // La poción se consume y desaparece
    }

    return DARKEN_LOOP;
}
```

Útil para: proyectiles que impactan, enemigos que mueren, objetos consumibles, partículas que terminan su vida.

---

### `DARKEN_PAUSE`

Devuélvelo para sacar la entidad del bucle de actualización sin destruirla. La entidad pasa a la zona pausada y deja de consumir CPU.

**Uso en el RPG:**
```c
void *state_chest_opening(void *data) {
    DARKEN_DATA(chest_data, c, entity);

    if (c->open_animation_done) {
        give_loot_to_player(c->contents);
        return DARKEN_PAUSE;   // El cofre sigue en el mapa, pero ya no hace nada
    }

    return DARKEN_LOOP;
}
```

También es la clave para el **sistema de menús**: cuando abres el inventario, pausas todas las entidades del mundo y solo dejas activas las del menú.

---

## Macros de introspección

Estas macros te permiten preguntarle al sistema cómo está una entidad o un estado, sin acceder a campos privados.

### `DARKEN_STATE_IS_ACTIVE(state)`

Devuelve verdadero si el estado es un puntero a función real (es decir, la entidad está viva y procesándose).

**Uso en el RPG:**
```c
void *state_boss_phase_transition(void *data) {
    // El jefe cambia de fase. Verificamos que el estado anterior era activo
    // antes de tocar estadísticas que podrían estar medio borradas.
    if (DARKEN_STATE_IS_ACTIVE(previous_state)) {
        boost_boss_stats();
    }
    return state_boss_enraged;
}
```

---

### `DARKEN_STATE_IS_LOOP(state)` / `DARKEN_STATE_IS_PAUSED(state)` / `DARKEN_STATE_IS_DELETED(state)`

Te dicen si un puntero de estado vale `DARKEN_LOOP`, `DARKEN_PAUSE` o `DARKEN_DELETE`.

**Uso en el RPG:**
```c
void *state_trap_triggered(void *data) {
    DARKEN_DATA(trap_data, t, entity);

    // Si el héroe ya ha sido marcado para borrar este frame (por ejemplo,
    // cayó al vacío justo antes), no aplicamos daño adicional.
    if (DARKEN_STATE_IS_DELETED(hero_entity->state))
        return DARKEN_LOOP;

    apply_damage(hero_entity, t->damage);
    return DARKEN_PAUSE;  // La trampa se desactiva tras un solo uso
}
```

---

### `DARKEN_ENTITY_IN_ACTIVE(entity)` / `DARKEN_ENTITY_IN_PAUSED(entity)` / `DARKEN_ENTITY_IN_USED(entity)` / `DARKEN_ENTITY_IN_FREE(entity)`

Te dicen en qué zona del pool vive una entidad en este momento.

**Uso en el RPG:**
```c
void *state_magic_mirror(void *data) {
    DARKEN_DATA(mirror_data, m, entity);

    // El espejo mágico solo funciona si el reflejo (otra entidad)
    // sigue activo en el mundo. Si está pausado o libre, no hay reflejo.
    if (DARKEN_ENTITY_IN_ACTIVE(m->reflection)) {
        sync_position_with_reflection(m);
    }

    return DARKEN_LOOP;
}
```

`DARKEN_ENTITY_IN_USED` combina activas + pausadas: útil para saber si una entidad todavía "existe" aunque esté congelada.

`DARKEN_ENTITY_IN_FREE` te avisa si una entidad ya fue devuelta al pool y su memoria está disponible para respawn.

---

## Ciclo de vida de una entidad

### `darken_entity_run(entity)`

Ejecuta el callback `state` de la entidad **una sola vez**, de forma inmediata. No espera al `darken_update()` del manager.

**Uso en el RPG:**
```c
// El jugador pulsa el botón de atacar. Queremos que el ataque se resuelva
// AHORA, no en el próximo frame del manager.
darken_entity_run(hero_entity);
```

Ideal para eventos síncronos: pulsar un botón, recibir un comando de red, activar un quick-time event.

---

### `darken_entity_update(entity)`

Fuerza la actualización de una entidad concreta, aplicando la misma lógica que `darken_update()` haría si la encontrara en la zona activa.

**Uso en el RPG:**
```c
// En un combate por turnos, solo el combatiente activo actúa.
// Las demás entidades enemigas esperan sin ejecutar su IA.
if (current_turn == ENEMY_TURN) {
    darken_entity_update(active_enemy);
}
```

Te permite tener granularidad sobre quién se actualiza sin romper la arquitectura del manager.

---

### `darken_entity_pause(entity)`

Mueve una entidad activa a la zona de pausada. Deja de ser visitada por `DARKEN_FOREACH` y por `darken_update()`.

**Uso en el RPG:**
```c
// Al entrar en una tienda, congelamos al héroe y a todos los enemigos.
// El mundo sigue existiendo, pero no se mueve.
void enter_shop(void) {
    darken_entity_pause(hero_entity);
    DARKEN_FOREACH(&world_manager, {
        if (ENTITY != hero_entity)
            darken_entity_pause(ENTITY);
    });
    spawn_shop_ui();
}
```

---

### `darken_entity_resume(entity)`

Saca una entidad de la zona pausada y la devuelve a la zona activa, justo al final del array de activas.

**Uso en el RPG:**
```c
// Al salir de la tienda, reactivamos todo.
void exit_shop(void) {
    darken_entity_resume(hero_entity);
    DARKEN_FOREACH(&world_manager, {
        // Nota: este foreach solo ve activas, así que necesitamos
        // otro mecanismo o un array auxiliar para las pausadas.
    });
}
```

También sirve para **spawnear entidades ya pausadas** y activarlas más tarde: creas un cofre sellado pausado, y cuando el jugador consigue la llave, haces `darken_entity_resume(chest_entity)`.

---

### `darken_entity_delete(entity)`

Borra una entidad inmediatamente, ejecutando su destructor si lo tiene.

**Uso en el RPG:**
```c
// El jugador usa un hechizo de disipación sobre un enemigo invocado.
// El enemigo muere al instante, sin esperar al próximo darken_update().
void cast_banish(darken_entity target) {
    play_sfx(SFX_BANISH);
    darken_entity_delete(target);
}
```

Útil cuando necesitas feedback inmediato: destrucción de muros por explosiones, eliminación de items del suelo al recogerlos, etc.

---




### `DARKEN_FOREACH(manager, código)`

Itera sobre todas las entidades activas. Define la variable `ENTITY` automáticamente.

**Uso en el RPG:**
```c
// Detectar colisiones entre el héroe y todos los enemigos
DARKEN_FOREACH(&world, {
    DARKEN_DATA(struct hero_data, h, ENTITY);

    if (h->tag == TAG_ENEMY && rects_overlap(hero_rect, enemy_rect(ENTITY))) {
        apply_damage(hero_entity, h->atk);
    }
});
```

Otro ejemplo: dibujar todas las entidades en orden:
```c
DARKEN_FOREACH(&world, {
    DARKEN_DATA(sprite_data, spr, ENTITY);
    draw_sprite(spr->gfx_id, spr->x, spr->y);
});
```

Como itera hacia atrás, si dibujas en ese orden las entidades creadas después se pintan encima (útil para capas de efectos sobre personajes).

---

## Características del motor

| Característica                            | Qué significa para tu RPG                                                                                                                                                                                                        |
| ----------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Sin asignación dinámica**               | Todo el memoria se reserva al inicio. Nunca hay pausas por `malloc` en medio del combate. Ideal para consolas retro o sistemas embebidos.                                                                                        |
| **Punteros estables a datos**             | Una vez spawneas una entidad, su dirección `entity->data` nunca cambia. Puedes guardar punteros cruzados entre entidades (un hechizo que sigue a su objetivo, un jefe que referencia a sus esbirros) sin miedo a invalidaciones. |
| **O(1) en spawn, delete, pause y resume** | Spawneas 50 partículas de una explosión, borras 20 enemigos y pausas el mundo para abrir un menú: todo es constante, sin bucles de búsqueda.                                                                                     |
| **Máquina de estados por callbacks**      | Cada entidad define su comportamiento como una cadena de funciones. Un enemigo puede pasar de `patrol → chase → attack → hurt → die` simplemente retornando el siguiente estado.                                                 |
| **Tres zonas lógicas**                    | Activas, libres y pausadas conviven en el mismo array. Pausar el mundo para un menú es tan barato como mover punteros.                                                                                                           |
| **Alineación a 4 bytes**                  | Pensado para arquitecturas donde el acceso desalineado es costoso o ilegal (Motorola 68000, algunos ARM). Los payloads se redondean automáticamente.                                                                             |
| **Destructor por entidad**                | Cada entidad puede llevar su propia función de limpieza. Un enemigo libera su sprite, una partícula libera su sonido, un cofre guarda su estado en el disco.                                                                     |
| **Iteración inversa segura**              | `darken_update` y `DARKEN_FOREACH` recorren de atrás adelante. Puedes borrar o pausar entidades durante la iteración sin corromper el bucle.                                                                                     |
| **Header-only**                           | `#define DARKEN_IMPLEMENTATION` en un solo archivo `.c`. Sin bibliotecas externas, sin CMake, sin dependencias.                                                                                                                  |

---

## Avisos y consejos de uso

### Sobre el diseño de estados

Cada callback de estado recibe `void *data` y devuelve `void *`. Ese valor de retorno **es tu próximo estado**. No es un código de error, no es un enum: es literalmente la función que se ejecutará en el siguiente frame.

**Consejo:** Diseña tus estados como si fueran *frames de una animación*. Cada llamada avanza un tick. Si necesitas un temporizador, guárdalo en `entity->usr` o en tu payload.

```c
void *state_enemy_stunned(void *data) {
    DARKEN_DATA(enemy_data, e, entity);
    if (--entity->usr == 0)
        return state_enemy_recover;   // Se acabó el aturdimiento
    return DARKEN_LOOP;               // Sigue aturdido
}
```

---

### Sobre `entity->tag` y `entity->usr`

Son dos campos públicos que Darken no toca. Úsalos para lo que necesites:

- **`tag` (32 bits):** Identificador de tipo. Define constantes como `TAG_HERO`, `TAG_ENEMY`, `TAG_ITEM`. Te permite filtrar entidades en un `DARKEN_FOREACH` sin mirar el payload.
- **`usr` (16 bits):** Variable de uso general. Temporizadores, contadores de frames, índices de animación, flags de estado. Es parte de la entidad, no del payload, así que sobrevive aunque cambies el tipo de datos.

---

### Sobre los destructores

`entity->destructor` solo se ejecuta si es un puntero a función válido. Si no necesitas limpieza especial, déjalo a `NULL` (o a `0`).

**Consejo:** Si tu entidad reservó recursos externos (un canal de audio, un sprite en VRAM, una entrada en una tabla de colisiones), el destructor es el lugar correcto para liberarlos. No lo hagas dentro del estado de borrado, porque `darken_entity_delete()` también invoca el destructor.

```c
void destructor_explosion(void *data) {
    DARKEN_DATA(explosion_data, ex, entity);
    free_audio_channel(ex->channel);  // O devolverlo a un pool tuyo
}
```

---

### Sobre pausar y reanudar

Cuando pausas una entidad, su puntero `data` sigue siendo válido. Esto es poderoso: puedes seguir leyendo sus coordenadas para dibujar el mapa congelado, o consultar su vida mientras eliges un objetivo para un hechizo en el menú de pausa.

**Consejo:** No pauses entidades que estén en medio de una transición crítica. Si un enemigo pausa justo cuando ha iniciado un ataque pero antes de aplicar daño, al reanudar continuará desde ese punto exacto. A veces eso es lo que quieres; otras veces, querrás resetear su estado al reanudar.

---

### Sobre el tamaño del payload

`DARKEN_STORAGE` fija el tamaño de `data[]` para **todas** las entidades del manager. Si tu RPG tiene entidades muy dispares (un héroe con 32 bytes de stats y una partícula con solo 4 bytes de posición), tienes dos opciones:

1. **Usar un payload único grande** que contenga un `union` de todos los tipos.
2. **Usar múltiples managers**: uno para el mundo (payload grande) y otro para efectos visuales (payload pequeño).

La segunda opción es más eficiente en memoria caché y permite iterar solo sobre lo que necesitas.

```c
DARKEN_STORAGE(world_entities, 32, sizeof(struct hero_data));
DARKEN_STORAGE(fx_entities, 128, sizeof(particle_data));
```

---

### Sobre `darken_spawn` y la inicialización

`darken_spawn` te da una entidad limpia de la zona libre, pero **no inicializa su contenido**. El payload puede contener basura de una entidad anterior que ocupó esa ranura.

**Consejo:** Siempre inicializa los campos críticos inmediatamente después del spawn. Al menos:

```c
darken_entity e = darken_spawn(&world);
e->state = state_default;
e->destructor = NULL;
e->tag = 0;
e->usr = 0;
// Luego inicializa tu payload...
```

---

### Sobre `darken_reset`

`darken_reset` ejecuta los destructores de todas las entidades activas y devuelve el manager a cero. Es la forma correcta de cambiar de nivel, de escena o de reiniciar tras un Game Over.

**Consejo:** Si tienes entidades pausadas que quieres preservar entre escenas (por ejemplo, el estado del inventario del jugador), no uses `darken_reset` ciegamente. Muévelas a otro manager o guárdalas en variables globales antes de resetear.

---

### Sobre el renderizado y el orden de dibujo

`DARKEN_FOREACH` itera de atrás hacia adelante. Esto significa que las entidades creadas más recientemente se procesan primero.

En un RPG esto es útil para el *z-ordering*: si spawneas primero el suelo, luego las paredes, después los personajes y al final las partículas, el `FOREACH` las dibujará en el orden inverso (partículas encima de personajes, personajes encima de paredes).

Si necesitas un orden diferente, ordena tu `pool` manualmente o usa múltiples managers por capa.

---

## mplo mínimo completo: un héroe y un slime

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdio.h>

typedef struct {
    int16_t x, y;
    int16_t hp;
} actor;

void *state_hero_idle(void *data) {
    DARKEN_DATA(actor, a, entity);
    // ... leer input, mover a ...
    return DARKEN_LOOP;
}

void *state_slime_chase(void *data) {
    DARKEN_DATA(actor, a, entity);
    // ... IA simple hacia el héroe ...
    if (a->hp <= 0) return DARKEN_DELETE;
    return DARKEN_LOOP;
}

void destructor_slime(void *data) {
    printf("¡El slime ha muerto!\n");
}

int main(void) {
    darken world;
    DARKEN_STORAGE(rpg, 16, sizeof(actor));
    darken_init(&world, DARKEN_ARGS(rpg));

    darken_entity hero = darken_spawn(&world);
    DARKEN_DATA(actor, h, hero);
    h->x = 100; h->y = 100; h->hp = 50;
    hero->state = state_hero_idle;
    hero->tag = 1;  // TAG_HERO

    darken_entity slime = darken_spawn(&world);
    DARKEN_DATA(actor, s, slime);
    s->x = 200; s->y = 200; s->hp = 10;
    slime->state = state_slime_chase;
    slime->destructor = destructor_slime;
    slime->tag = 2;  // TAG_ENEMY

    for (int frame = 0; frame < 60; ++frame) {
        darken_update(&world);
    }

    darken_reset(&world);
    return 0;
}
```

---

*Darken no es un motor completo: no dibuja, no carga mapas, no reproduce sonido. Pero si tu juego puede expresarse como un conjunto de entidades que cambian de estado frame a frame, Darken es el esqueleto sobre el que construir todo lo demás.*
