# Darken 2.0 — Guía de uso para un shoot 'em up

> *Darken* es un gestor de entidades mínimo en C puro. No hay clases, no hay herencia, no hay `malloc` en caliente. Solo punteros a funciones, memoria contigua y tres zonas lógicas que hacen la magia.
>
> Esta guía te muestra cómo construir un shmup sencillo —con nave del jugador, enemigos en formación, ráfagas de balas y jefes de fin de oleada— usando cada pieza del API público.

---

## Tabla de contenidos

1. [El mundo del juego](#1-el-mundo-del-juego)
2. [Paso a paso: de cero a gameplay](#2-paso-a-paso-de-cero-a-gameplay)
3. [Macros de control y estados](#3-macros-de-control-y-estados)
4. [Macros de introspección](#4-macros-de-introspección)
5. [Ciclo de vida de una entidad](#5-ciclo-de-vida-de-una-entidad)
6. [Iteración y renderizado](#6-iteración-y-renderizado)
7. [Características del motor](#7-características-del-motor)
8. [Avisos y consejos de uso](#8-avisos-y-consejos-de-uso)

---

## 1. El mundo del juego

Imagina un shmup vertical clásico. En pantalla conviven varios tipos de entidades:

- **Nave del jugador**: se mueve con el stick, dispara, recoge power-ups. Siempre activa mientras haya vidas.
- **Enemigos**: entran por los bordes, siguen trayectorias (línea recta, seno, espiral) y disparan. Son *entidades activas*.
- **Balas**: del jugador y de los enemigos. Se spawnean a decenas por segundo y se autodestruyen al salir de pantalla o impactar. Vida muy corta.
- **Power-ups**: quedan flotando tras destruir ciertos enemigos. Si el jugador no los recoge a tiempo, pueden *pausarse* al entrar en un menú de pausa o *borrarse* al cambiar de oleada.
- **Jefe de fin de oleada**: una entidad compleja con múltiples fases. Puede *pausar* partes de su cuerpo (escudos) mientras ataca con otras.
- **Partículas**: explosiones, chispas, humo. Entidades efímeras que nacen y mueren en pocos frames.

Todas estas cosas son entidades gestionadas por uno o varios managers `darken`.

### Estructura de datos de la nave

Cada entidad lleva un `data[]` flexible al final de su estructura. Para la nave del jugador podría ser:

```c
struct ship_data {
    int16_t x, y;        // Posición en pantalla (fixed-point o píxeles)
    int16_t vx, vy;      // Velocidad
    int16_t hp;          // Escudo / vida
    uint8_t power;       // Nivel de disparo (1 = simple, 4 = spread)
    uint8_t inv_timer;   // Frames de invencibilidad post-golpe
    uint8_t bombs;       // Bombas restantes
};
```

Para una bala, el payload es mucho más ligero:

```c
struct bullet_data {
    int16_t x, y;
    int16_t vx, vy;
    uint8_t owner;       // 0 = jugador, 1 = enemigo
    uint8_t damage;
};
```

---

## 2. Paso a paso: de cero a gameplay

### `DARKEN_STORAGE(nombre, capacidad, tamaño_payload)`

Declara el almacenamiento estático. Reserva el pool de punteros, el bloque de memoria para los datos de las entidades, y metadatos de capacidad. Todo en una sola estructura anónima, sin fragmentación ni `malloc`.

```c
// Hasta 128 entidades, cada una con 32 bytes de payload.
// En un shmup, 128 es suficiente para: 1 jugador + ~30 enemigos +
// ~80 balas + partículas y power-ups.
DARKEN_STORAGE(shmup_world, 128, 32);
```

### `DARKEN_ARGS(nombre)`

Expande los cuatro argumentos que necesita `darken_init()` a partir de una variable creada con `DARKEN_STORAGE`.

```c
darken_init(&world, DARKEN_ARGS(shmup_world));
// Equivalente a:
// darken_init(&world, shmup_world.pool, shmup_world.data, 128, 32);
```

### `darken_init(manager, pool, storage, capacidad, bytes_payload)`

Inicializa el manager. Particiona el bloque de memoria en entidades contiguas, rellena el pool de punteros y deja todo listo para `darken_spawn()`. Se llama una sola vez al arrancar el juego.

```c
darken world;
DARKEN_STORAGE(shmup_world, 128, 32);

darken_init(&world, DARKEN_ARGS(shmup_world));
```

### `darken_spawn(manager)`

Devuelve una entidad libre del pool. A partir de aquí, tú decides qué es: una nave, una bala, una explosión.

```c
darken_object player = darken_spawn(&world);
```

### `DARKEN_DATA(Tipo, variable, entidad)`

Declara un puntero local al *payload* de una entidad, ya casteado al tipo que necesitas. Evita errores de tipeo y hace el código legible.

```c
DARKEN_DATA(struct ship_data, ship, player);

ship->x = 120;   // Centro de pantalla (240px de ancho)
ship->y = 200;
ship->hp = 3;
ship->power = 1;
ship->bombs = 2;

player->update = state_player_alive;
player->destroy= destructor_player;
player->tag = TAG_PLAYER;
player->usr = 0;   // Usaremos esto como temporizador de parpadeo post-golpe
```

### `darken_update(manager)`

Recorre las entidades activas de atrás hacia adelante, ejecuta sus callbacks `state`, y aplica transiciones (borrado, pausa, cambio de estado). Este es el núcleo del bucle de juego.

```c
while (game_running) {
    read_input();
    darken_update(&world);   // Toda la IA, física y lógica ocurre aquí
    render_frame();
    vsync_wait();
}
```

El orden inverso es intencional: si una entidad se borra o pausa durante su update, el swap no afecta a las entidades que aún no han sido procesadas este frame.

### `darken_reset(manager)`

Borra todas las entidades activas, ejecutando sus destructores, y devuelve el manager a su estado inicial. Útil al cambiar de oleada o tras un Game Over.

```c
// El jugador pierde todas las vidas. Limpiamos el mundo para la pantalla de título.
darken_reset(&world);
```

### Ejemplo completo de setup

```c
DARKEN_STORAGE(shmup_world, 128, 32);

darken world;
darken_init(&world, DARKEN_ARGS(shmup_world));

darken_object player = darken_spawn(&world);
DARKEN_DATA(struct ship_data, ship, player);

ship->x = 120; ship->y = 200;
ship->hp = 3; ship->power = 1;

player->update = state_player_alive;
player->tag = TAG_PLAYER;

while (game_running) {
    read_input();
    darken_update(&world);
    render_frame();
    vsync_wait();
}

darken_reset(&world);
```

---

## 3. Macros de control y estados

### `DARKEN_LOOP`

Devuélvelo desde tu callback de estado para indicar: *"este frame no ha pasado nada especial, repite el mismo estado el próximo frame"*.

**Uso en el shmup:**
```c
void *state_enemy_sine(struct enemy_data *e)
{
    e->x = e->origin_x + (int16_t)(sin(e->t * 0.05f) * 60);
    e->y += e->speed;
    e->t++;

    if (e->y > SCREEN_H + 16)
        return DARKEN_DELETE;   // Se fue por abajo

    return DARKEN_LOOP;         // Sigue oscilando
}
```

Es el valor por defecto para la mayoría de estados: volar, caer, animar, perseguir.

---

### `DARKEN_DELETE`

Devuélvelo cuando la entidad debe desaparecer del mundo. El manager la moverá a la zona libre y, si tiene `destructor` asignado, lo ejecutará antes.

**Uso en el shmup:**
```c
void *state_bullet_fly(struct bullet_data *b)
{
    b->x += b->vx;
    b->y += b->vy;

    // Fuera de pantalla o impacto confirmado por el sistema de colisiones
    if (b->y < -8 || b->y > SCREEN_H + 8 || b->x < -8 || b->x > SCREEN_W + 8)
        return DARKEN_DELETE;

    return DARKEN_LOOP;
}
```

Útil para: balas que salen de pantalla, enemigos destruidos, power-ups que expiran, partículas que terminan su animación.

---

### `DARKEN_PAUSE`

Devuélvelo para sacar la entidad del bucle de actualización sin destruirla. La entidad pasa a la zona pausada y deja de consumir CPU.

**Uso en el shmup:**
```c
void *state_powerup_float(struct powerup_data *p)
{
    p->y += 1;   // Cae lentamente
    p->blink++;

    if (p->y > SCREEN_H + 8)
        return DARKEN_DELETE;   // Se perdió

    // Si el jugador abre el menú de pausa, el juego pausará esta entidad
    // desde fuera. Pero también puede pausarse a sí misma en ciertos casos:
    if (boss_intro_is_playing)
        return DARKEN_PAUSE;   // Congela el power-up durante la intro del jefe

    return DARKEN_LOOP;
}
```

También es la clave para el **menú de pausa**: congelas todas las entidades del mundo y solo dejas activas las del HUD de pausa.

---

## 4. Macros de introspección

Estas macros te permiten preguntarle al sistema cómo está una entidad o un estado, sin acceder a campos privados.

### `DARKEN_STATE_IS_ACTIVE(state)`

Devuelve verdadero si el estado es un puntero a función real (es decir, la entidad está viva y procesándose).

**Uso en el shmup:**
```c
void *state_boss_core(struct boss_data *boss)
{
    // El núcleo solo ataca si sus escudos (entidades separadas) siguen activos.
    // Si los escudos fueron destruidos, el jefe entra en fase de furia.
    if (!DARKEN_STATE_IS_ACTIVE(boss->shield_left->update) &&
        !DARKEN_STATE_IS_ACTIVE(boss->shield_right->update)) {
        return state_boss_frenzy;
    }

    return DARKEN_LOOP;
}
```

---

### `DARKEN_STATE_IS_LOOP(state)` / `DARKEN_STATE_IS_PAUSED(state)` / `DARKEN_STATE_IS_DELETED(state)`

Te dicen si un puntero de estado vale `DARKEN_LOOP`, `DARKEN_PAUSE` o `DARKEN_DELETE`.

**Uso en el shmup:**
```c
void *state_homing_missile(struct missile_data *m)
{
    // Si el objetivo ya fue marcado para borrar este frame (destruido por
    // otra bala justo antes), el misil pierde el lock y vuela recto.
    if (DARKEN_STATE_IS_DELETED(m->target->update)) {
        m->target = NULL;
        return state_missile_dumb;   // Vuela en línea recta
    }

    steer_towards(m, m->target);
    return DARKEN_LOOP;
}
```

---

### `DARKEN_OBJECT_IN_ACTIVE(entity)` / `DARKEN_OBJECT_IN_PAUSED(entity)` / `DARKEN_OBJECT_IN_USED(entity)` / `DARKEN_OBJECT_IN_FREE(entity)`

Te dicen en qué zona del pool vive una entidad en este momento.

**Uso en el shmup:**
```c
void *state_formation_leader(struct enemy_data *leader)
{
    // Soy el líder de una formación de 5 naves. Si algún aliado fue
    // destruido, reajusto la formación para cerrar el hueco.
    for (int i = 0; i < 4; ++i)
        if (!DARKEN_OBJECT_IN_USED(leader->wingmen[i]))
            leader->wingmen[i] = leader->wingmen[--leader->wingmen_count];

    return DARKEN_LOOP;
}
```

`DARKEN_OBJECT_IN_USED` combina activas + pausadas: útil para saber si una entidad todavía "existe" aunque esté congelada.

`DARKEN_OBJECT_IN_FREE` te avisa si una entidad ya fue devuelta al pool y su memoria está disponible para respawn.

---

## 5. Ciclo de vida de una entidad

### `darken_object_run(entity)`

Ejecuta el callback `state` de la entidad **una sola vez**, de forma inmediata. No espera al `darken_update()` del manager.

**Uso en el shmup:**
```c
// El jugador pulsa el botón de disparo. Queremos que la bala nazca
// AHORA, no en el próximo frame del manager.
if (input_pressed(BUTTON_A)) {
    darken_object bullet = darken_spawn(&world);
    DARKEN_DATA(struct bullet_data, b, bullet);
    b->x = player_x; b->y = player_y - 8;
    b->vy = -4;
    bullet->update = state_bullet_fly;
    bullet->tag = TAG_PLAYER_BULLET;

    darken_object_run(bullet);   // Avanza un frame de inmediato
}
```

Ideal para eventos síncronos: pulsar un botón, recibir un comando de red, activar una bomba.

---

### `darken_object_update(entity)`

Fuerza la actualización de una entidad concreta, aplicando la misma lógica que `darken_update()` haría si la encontrara en la zona activa.

**Uso en el shmup:**
```c
// En un modo de juego a cámara lenta (bullet time), solo el jugador
// y sus balas se actualizan a velocidad normal. El resto del mundo
// va a mitad de velocidad.
if (bullet_time_active) {
    darken_object_update(player);
    DARKEN_FOREACH(&world, {
        if (ENTITY->tag == TAG_PLAYER_BULLET)
            darken_object_update(ENTITY);
    });
}
```

Te permite tener granularidad sobre quién se actualiza sin romper la arquitectura del manager.

---

### `darken_object_pause(entity)`

Mueve una entidad activa a la zona de pausada. Deja de ser visitada por `DARKEN_FOREACH` y por `darken_update()`.

**Uso en el shmup:**
```c
// El jugador pulsa START. Congelamos todo el mundo.
void enter_pause(void)
{
    darken_object_pause(player);
    DARKEN_FOREACH(&world,
    {
        darken_object_pause(ENTITY);
    });
    spawn_pause_menu();
}
```

---

### `darken_object_resume(entity)`

Saca una entidad de la zona pausada y la devuelve a la zona activa, justo al final del array de activas.

**Uso en el shmup:**
```c
// El jugador pulsa START de nuevo. Descongelamos todo.
void exit_pause(void)
{
    // Nota: DARKEN_FOREACH solo ve activas, así que necesitamos
    // otro mecanismo para iterar las pausadas. Una opción es
    // mantener un array auxiliar de entidades pausadas.
    for (int i = world.paused; i < world.capacity; ++i) {
        darken_object_resume(world.pool[i]);
    }
}
```

También sirve para **spawnear entidades ya pausadas** y activarlas más tarde: creas una oleada de enemigos pausados fuera de pantalla, y cuando el jugador llega al trigger, haces `darken_object_resume(enemy)` para cada uno.

---

### `darken_object_delete(entity)`

Borra una entidad inmediatamente, ejecutando su destructor si lo tiene.

**Uso en el shmup:**
```c
// El jugador usa una bomba. Todos los proyectiles enemigos en
// pantalla se destruyen al instante.
void use_bomb(void)
{
    play_sfx(SFX_BOMB);
    screen_flash(3);

    DARKEN_FOREACH(&world,
    {
        if (ENTITY->tag == TAG_ENEMY_BULLET) {
            spawn_particle_explosion(ENTITY);   // Efecto visual
            darken_object_delete(ENTITY);       // Muere ahora, no al final del frame
        }
    });
}
```

Útil cuando necesitas feedback inmediato: destrucción masiva por bomba, colisión jugador-enemigo, recoger un power-up.

---

## 6. Iteración y renderizado

### `DARKEN_FOREACH(manager, código)`

Itera sobre todas las entidades activas. Define la variable `ENTITY` automáticamente.

**Uso en el shmup:**
```c
// Sistema de colisiones: balas del jugador contra enemigos
DARKEN_FOREACH(&world,
{
    if (ENTITY->tag != TAG_PLAYER_BULLET) continue;

    DARKEN_DATA(struct bullet_data, b, ENTITY);
    rect_t bullet_rect = {b->x - 2, b->y - 2, 4, 4};

    DARKEN_FOREACH(&world, {   // Bucle anidado: buscamos enemigos
        if (INNER_ENTITY->tag != TAG_ENEMY) continue;

        DARKEN_DATA(struct enemy_data, e, INNER_ENTITY);
        if (rects_overlap(bullet_rect, enemy_rect(e))) {
            e->hp -= b->damage;
            darken_object_delete(ENTITY);   // Borra la bala

            if (e->hp <= 0) {
                spawn_explosion(e->x, e->y);
                darken_object_delete(INNER_ENTITY);   // Borra el enemigo
            }
            break;   // Una bala no atraviesa (a menos que sea piercing)
        }
    });
});
```

Otro ejemplo: dibujar todas las entidades en orden:
```c
DARKEN_FOREACH(&world, {
    DARKEN_DATA(struct sprite_data, spr, ENTITY);
    draw_sprite(spr->gfx_id, spr->x, spr->y);
});
```

Como itera hacia atrás, las entidades creadas más recientemente se procesan primero. En un shmup esto es útil para el *z-ordering*: si spawneas primero el fondo estrellado, luego las naves, después las balas y al final las partículas, el `FOREACH` las dibujará en el orden inverso (partículas encima de balas, balas encima de naves).

---

## 7. Características del motor

| Característica                            | Qué significa para tu shmup                                                                                                                                                                                                   |
| ----------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Sin asignación dinámica**               | Todo el memoria se reserva al inicio. Nunca hay pausas por `malloc` en medio de una oleada con 80 balas en pantalla. Ideal para consolas retro o sistemas embebidos.                                                          |
| **Punteros estables a datos**             | Una vez spawneas una entidad, su dirección `entity->data` nunca cambia. Puedes guardar punteros cruzados entre entidades (un misil que sigue a su objetivo, un jefe que referencia a sus escudos) sin miedo a invalidaciones. |
| **O(1) en spawn, delete, pause y resume** | Spawneas 30 balas de un spread shot, borras 20 enemigos y pausas el mundo para un menú: todo es constante, sin bucles de búsqueda.                                                                                            |
| **Máquina de estados por callbacks**      | Cada entidad define su comportamiento como una cadena de funciones. Un enemigo puede pasar de `enter → attack → hurt → explode → delete` simplemente retornando el siguiente estado.                                          |
| **Tres zonas lógicas**                    | Activas, libres y pausadas conviven en el mismo array. Pausar el mundo para un menú de pausa es tan barato como mover punteros.                                                                                               |
| **Alineación a 4 bytes**                  | Pensado para arquitecturas donde el acceso desalineado es costoso o ilegal (Motorola 68000, algunos ARM). Los payloads se redondean automáticamente.                                                                          |
| **Destructor por entidad**                | Cada entidad puede llevar su propia función de limpieza. Un enemigo libera su sprite, una bala libera su sonido, una explosión guarda su puntuación en el disco.                                                              |
| **Iteración inversa segura**              | `darken_update` y `DARKEN_FOREACH` recorren de atrás adelante. Puedes borrar o pausar entidades durante la iteración sin corromper el bucle.                                                                                  |
| **Header-only**                           | `#define DARKEN_IMPLEMENTATION` en un solo archivo `.c`. Sin bibliotecas externas, sin CMake, sin dependencias.                                                                                                               |

---

## 8. Avisos y consejos de uso

### Sobre el diseño de estados

Cada callback de estado recibe `void *data` y devuelve `void *`. Ese valor de retorno **es tu próximo estado**. No es un código de error, no es un enum: es literalmente la función que se ejecutará en el siguiente frame.

**Consejo:** Diseña tus estados como si fueran *frames de una animación*. Cada llamada avanza un tick. Si necesitas un temporizador, guárdalo en `entity->usr` o en tu payload.

```c
void *state_enemy_flash(struct enemy_data *entity)
{
    if (--entity->usr == 0)
        return state_enemy_recover;   // Se acabó el frame de invencibilidad

    return DARKEN_LOOP;               // Sigue parpadeando
}
```

---

### Sobre `entity->tag` y `entity->usr`

Son dos campos públicos que Darken no toca. Úsalos para lo que necesites:

- **`tag` (32 bits):** Identificador de tipo. Define constantes como `TAG_PLAYER`, `TAG_ENEMY`, `TAG_PLAYER_BULLET`, `TAG_ENEMY_BULLET`, `TAG_POWERUP`. Te permite filtrar entidades en un `DARKEN_FOREACH` sin mirar el payload.
- **`usr` (16 bits):** Variable de uso general. Temporizadores, contadores de frames, índices de animación, flags de estado. Es parte de la entidad, no del payload, así que sobrevive aunque cambies el tipo de datos.

En un shmup, `tag` es especialmente útil para el sistema de colisiones: puedes descartar rápidamente entidades que no participan en una colisión concreta.

---

### Sobre los destructores

`entity->destructor` solo se ejecuta si es un puntero a función válido. Si no necesitas limpieza especial, déjalo a `NULL` (o a `0`).

**Consejo:** Si tu entidad reservó recursos externos (un canal de audio, un sprite en VRAM, una entrada en una tabla de colisiones espacial), el destructor es el lugar correcto para liberarlos. No lo hagas dentro del estado de borrado, porque `darken_object_delete()` también invoca el destructor.

```c
void destructor_explosion(struct particle_data *p)
{
    return_vram_sprite(p->gfx_slot);   // Devuelve el slot gráfico a tu pool
}
```

---

### Sobre pausar y reanudar

Cuando pausas una entidad, su puntero `data` sigue siendo válido. Esto es poderoso: puedes seguir leyendo sus coordenadas para dibujar el mundo congelado detrás del menú de pausa.

**Consejo:** No pauses entidades que estén en medio de una transición crítica. Si una bala enemigo pausa justo cuando ha iniciado su animación de spawn pero antes de volverse dañina, al reanudar continuará desde ese punto exacto. A veces eso es lo que quieres; otras veces, querrás resetear su estado al reanudar.

---

### Sobre el tamaño del payload

`DARKEN_STORAGE` fija el tamaño de `data[]` para **todas** las entidades del manager. Si tu shmup tiene entidades muy dispares (un jefe con 48 bytes de estado interno y una bala con solo 8 bytes de posición/velocidad), tienes dos opciones:

1. **Usar un payload único grande** que contenga un `union` de todos los tipos.
2. **Usar múltiples managers**: uno para el mundo (naves, jefes, power-ups) y otro para efectos de partículas (payload pequeño).

La segunda opción es más eficiente en memoria caché y permite iterar solo sobre lo que necesitas. En un shmup con muchas partículas, un manager dedicado para ellas mejora el rendimiento.

```c
DARKEN_STORAGE(game_entities, 64, sizeof(struct ship_data));
DARKEN_STORAGE(fx_entities, 256, sizeof(struct particle_data));
```

---

### Sobre `darken_spawn` y la inicialización

`darken_spawn` te da una entidad limpia de la zona libre, pero **no inicializa su contenido**. El payload puede contener basura de una entidad anterior que ocupó esa ranura.

**Consejo:** Siempre inicializa los campos críticos inmediatamente después del spawn. Al menos:

```c
darken_object e = darken_spawn(&world);
e->update = state_default;
e->destroy= NULL;
e->tag = 0;
e->usr = 0;
// Luego inicializa tu payload...
```

En un shmup donde spawneas decenas de balas por segundo, olvidar inicializar `vx` o `damage` provoca comportamientos erráticos difíciles de debuggear.

---

### Sobre `darken_reset`

`darken_reset` ejecuta los destructores de todas las entidades activas y devuelve el manager a cero. Es la forma correcta de cambiar de oleada, de escena o de reiniciar tras un Game Over.

**Consejo:** Si tienes entidades pausadas que quieres preservar entre escenas (por ejemplo, el estado del jugador entre oleadas), no uses `darken_reset` ciegamente. Muévelas a otro manager o guárdalas en variables globales antes de resetear.

---

### Sobre el renderizado y el orden de dibujo

`DARKEN_FOREACH` itera de atrás hacia adelante. Esto significa que las entidades creadas más recientemente se procesan primero.

En un shmup esto es útil para el *z-ordering*: si spawneas primero el fondo estrellado, luego las naves, después las balas y al final las partículas, el `FOREACH` las dibujará en el orden inverso (partículas encima de balas, balas encima de naves).

Si necesitas un orden diferente, ordena tu `pool` manualmente o usa múltiples managers por capa.

---

## Ejemplo mínimo completo: nave y un enemigo

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdio.h>

typedef struct
{
    int16_t x, y;
    int16_t hp;b
} actor;

void *state_player(actor *a)
{
    // ... leer input, mover a ...
    return DARKEN_LOOP;
}

void *state_enemy(actor *a)
{
    a->y += 2;   // Baja en línea recta
    if (a->y > 240) return DARKEN_DELETE;
    return DARKEN_LOOP;
}

void destructor_enemy(void *data)
{
    printf("¡Enemigo destruido!\n");
}

int main(void)
{
    darken world;
    DARKEN_STORAGE(shmup, 32, sizeof(actor));
    darken_init(&world, DARKEN_ARGS(shmup));

    darken_object player = darken_spawn(&world);
    DARKEN_DATA(actor, p, player);
    p->x = 120; p->y = 200; p->hp = 3;
    player->update = state_player;
    player->tag = 1;  // TAG_PLAYER

    darken_object enemy = darken_spawn(&world);
    DARKEN_DATA(actor, e, enemy);
    e->x = 120; e->y = -16; e->hp = 5;
    enemy->update = state_enemy;
    enemy->destroy= destructor_enemy;
    enemy->tag = 2;  // TAG_ENEMY

    while (1)
        darken_update(&world);

    darken_reset(&world);
    return 0;
}
```

---

*Darken no es un motor completo: no dibuja, no carga mapas, no reproduce sonido. Pero si tu juego puede expresarse como un conjunto de entidades que cambian de estado frame a frame, Darken es el esqueleto sobre el que construir todo lo demás.*
