# Darken 2.0 — Tutorial Progresivo

> **Darken** (DARKula ENgine) es un entity system en C para proyectos donde el control manual de la memoria y el rendimiento en arquitecturas clásicas (¡hola, Motorola 68000!) importan.
>
> Usa extensiones GNU C (`__attribute__`, statement expressions) y un modelo de **callbacks de estado**.

---

## Tabla de contenidos

1. [Conceptos clave (lee esto primero)](#1-conceptos-clave)
2. [Paso 0: Tu primera entidad](#2-paso-0-tu-primera-entidad)
3. [Paso 1: Darle vida con estados](#3-paso-1-darle-vida-con-estados)
4. [Paso 2: El Sistema de Renderizado](#4-paso-2-el-sistema-de-renderizado)
5. [Paso 3: Fábrica de enemigos](#5-paso-3-fábrica-de-enemigos)
6. [Paso 4: ¡Disparos!](#6-paso-4-disparos)
7. [Paso 5: Colisiones y destrucción](#7-paso-5-colisiones-y-destrucción)
8. [Paso 6: Pausa estratégica](#8-paso-6-pausa-estratégica)
9. [Paso 7: Limpieza con destructores](#9-paso-7-limpieza-con-destructores)
10. [API Completa de referencia](#10-api-completa-de-referencia)

---

## 1. Conceptos clave

Antes de tocar código, entiende estas tres ideas. Todo lo demás es azúcar sintáctico.

### El Manager y sus 3 zonas

El `de_manager` es un array de **punteros** a entidades. Las entidades reales viven en un bloque de memoria contiguo que tú reservas. El manager solo reordena *punteros*.

```
[ activas ][ libres ][ pausadas ]
0       size     paused   capacity
```

- **Activas** `[0, size)`: Se actualizan cada frame. Aquí creas y destruyes libremente.
- **Libres** `[size, paused)`: Slots vacíos. `de_manager_new()` coge de aquí.
- **Pausadas** `[paused, capacity)`: Fuera del loop. **Crucial:** una entidad pausada nunca se mueve de memoria, así que es seguro tener punteros externos a su `data[]`.

### Estados como callbacks

Cada entidad tiene un `state` que es un puntero a función:

```c
void *mi_estado(void *data);
```

La función recibe los datos de la entidad y devuelve qué hacer a continuación:

| Valor de retorno | Significado |
|------------------|-------------|
| `DE_STATE_LOOP`  | Sigue en este estado el próximo frame |
| `DE_STATE_DELETE`| Destruir la entidad |
| `DE_STATE_PAUSE` | Sacar del loop activo (pausar) |
| Cualquier otro `de_state` | Transicionar a ese nuevo estado |

### Systems: flat-packed arrays

Un `de_system` es un pool plano de punteros. Si tienes un sistema de "posición + velocidad", guardas pares de punteros `(pos*, vel*)` contiguos. Iterar es cache-friendly y trivial.

---

## 2. Paso 0: Tu primera entidad

Nuestro juego será *Orbit Defender*: una nave en el centro que dispara asteroides. Empecemos definiendo los datos de la nave.

### Estructura del proyecto

```
orbit_defender/
├── darken.h
└── main.c
```

### `main.c` — La base

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdio.h>

/* ============================================================
 * DATOS DEL JUEGO
 * ============================================================ */

typedef struct { float x, y; } Vec2;

typedef struct
{
    Vec2 pos;
    Vec2 vel;
    int hp;
} Nave;

/* ============================================================
 * PASO 0: Setup del manager y creación de la nave
 * ============================================================ */

int main(void)
{
    /* 1. Reservamos memoria para 32 entidades con payload sizeof(Nave).
     *    La macro DE_MANAGER_STORAGE crea una struct anónima con:
     *    - pool[]: array de punteros a entidades
     *    - data[]: bloque de memoria real para los datos
     */
    DE_MANAGER_STORAGE(mundo, 32, sizeof(Nave));

    /* 2. Inicializamos el manager. Los argumentos los sacamos con otra macro. */
    de_manager manager;
    de_manager_init(&manager, DE_MANAGER_ARGS(mundo));

    /* 3. Creamos nuestra primera entidad. */
    de_entity nave = de_manager_new(&manager);
    if (!nave) {
        printf("No hay slots libres!\n");
        return 1;
    }

    /* 4. Accedemos a su payload y lo inicializamos. */
    Nave *datos = (Nave *)nave->data;
    datos->pos = (Vec2){ 40.0f, 12.0f };  /* centro de una pantalla 80x24 */
    datos->vel = (Vec2){ 0.0f, 0.0f };
    datos->hp  = 3;

    printf("Nave creada en slot %u, posicion (%.1f, %.1f)\n",
           nave->slot, datos->pos.x, datos->pos.y);

    return 0;
}
```

Compila con:
```bash
gcc -std=gnu99 main.c -o orbit_defender
```

**¿Qué acaba de pasar?**
- `DE_MANAGER_STORAGE` reservó `32 * sizeof(struct de_entity + sizeof(Nave))` bytes alineados a 4.
- `de_manager_new()` nos dio la primera entidad del *free zone*, moviendo el límite `size` hacia la derecha.
- `nave->data` es un array flexible (`uint8_t data[]`) que apunta directamente al payload `Nave`.

---

## 3. Paso 1: Darle vida con estados

Una nave estática es aburrida. Vamos a hacer que se mueva con las teclas (simuladas) usando un **estado**.

```c
/* Estado de la nave: se ejecuta cada frame */
void *nave_actualizar(void *data)
{
    Nave *n = (Nave *)data;

    /* Simulamos input: se mueve hacia la derecha automáticamente */
    n->pos.x += n->vel.x;
    n->pos.y += n->vel.y;

    /* Rebote simple en los bordes */
    if (n->pos.x > 78.0f || n->pos.x < 1.0f) n->vel.x *= -1;
    if (n->pos.y > 22.0f || n->pos.y < 1.0f) n->vel.y *= -1;

    /* Seguimos en este estado el próximo frame */
    return DE_STATE_LOOP;
}
```

Y en `main`, después de crear la nave:

```c
    datos->vel = (Vec2){ 0.5f, 0.3f };

    /* Asignamos el estado inicial */
    nave->state = nave_actualizar;

    /* Simulamos 60 frames de juego */
    for (int frame = 0; frame < 60; ++frame) {
        de_manager_update(&manager);
    }
```

### ¿Qué hace `de_manager_update`?

Recorre **solo la zona activa** `[0, size)` de atrás hacia adelante:
1. Llama a `entity->state(entity->data)`.
2. Si el estado devuelve `DE_STATE_LOOP`, no toca `entity->state`.
3. Si devuelve otro valor (incluido `DE_STATE_DELETE`), actualiza el estado.

> **Nota:** El recorrido inverso permite que una entidad se borre a sí misma (o a otras) sin romper la iteración.

---

## 4. Paso 2: El Sistema de Renderizado

Iterar entidades una a una con `DE_MANAGER_FOREACH` está bien, pero para renderizar queremos un **sistema** especializado con punteros planos y cache-friendly.

Definimos un `de_system` que almacenará punteros a `Vec2` (posiciones) para dibujarlas.

```c
/* Sistema de renderizado: 1 parámetro (puntero a Vec2) */
DE_SYSTEM_STORAGE(render_sys, 64, 1);

/* Función que dibuja una entidad (simulado con printf) */
void *sistema_dibujar(de_system sys)
{
    /* DE_SYSTEM_FOREACH_1 desempaqueta 1 puntero por grupo */
    DE_SYSTEM_FOREACH_1(sys, Vec2 *pos, {
        printf("\033[%d;%dH*", (int)pos->y, (int)pos->x);
    });
    return DE_STATE_LOOP;
}
```

En `main`:

```c
    de_system renderer;
    de_system_init(&renderer, DE_SYSTEM_ARGS(render_sys));

    /* Cada vez que creamos una entidad visible, la registramos */
    DE_SYSTEM_ADD(&renderer, &datos->pos);

    /* En el loop de juego, antes o después de update: */
    sistema_dibujar(&renderer);
```

### ¿Por qué un System?

- **Manager**: gestiona el ciclo de vida (crear, destruir, pausar).
- **System**: procesa datos. Es un array plano de punteros a los componentes que te interesan.

Puedes tener múltiples sistemas: uno para física, otro para IA, otro para sonido. Cada uno con su propio `de_system`.

---

## 5. Paso 3: Fábrica de enemigos

Vamos a crear asteroides que aparecen desde arriba. Necesitamos **tags** para distinguir tipos.

```c
typedef enum { TAG_NAVE, TAG_ASTEROIDE } Tag;

typedef struct
{
    Vec2 pos;
    Vec2 vel;
    int hp;
    Tag tag;
} Entidad;  /* Renombramos: ahora es genérica */
```

Estado del asteroide:

```c
void *asteroide_actualizar(void *data)
{
    Entidad *e = (Entidad *)data;
    e->pos.y += e->vel.y;  /* caen */

    if (e->pos.y > 24.0f)
        return DE_STATE_DELETE;  /* se destruye al salir de pantalla */

    return DE_STATE_LOOP;
}
```

Función spawner:

```c
void spawn_asteroide(de_manager m, de_system renderer)
{
    de_entity a = de_manager_new(m);
    if (!a) return;

    Entidad *e = (Entidad *)a->data;
    e->pos = (Vec2){ rand() % 80, 0.0f };
    e->vel = (Vec2){ 0.0f, 0.2f + (rand() % 10) / 10.0f };
    e->hp  = 1;
    e->tag = TAG_ASTEROIDE;

    a->state = asteroide_actualizar;
    DE_SYSTEM_ADD(renderer, &e->pos);
}
```

En el loop de juego, spawnear cada ciertos frames:

```c
    for (int frame = 0; frame < 300; ++frame) {
        if (frame % 30 == 0) spawn_asteroide(&manager, &renderer);

        de_manager_update(&manager);   /* mueve todo */
        sistema_dibujar(&renderer);    /* dibuja todo */
    }
```

---

## 6. Paso 4: ¡Disparos!

La nave dispara proyectiles hacia arriba. Los proyectiles son entidades de corta vida.

```c
typedef struct { Vec2 pos; Vec2 vel; int vida; } Bala;

void *bala_actualizar(void *data)
{
    Bala *b = (Bala *)data;
    b->pos.y -= b->vel.y;
    b->vida--;

    if (b->pos.y < 0 || b->vida <= 0)
        return DE_STATE_DELETE;

    return DE_STATE_LOOP;
}

void disparar(de_manager m, Vec2 origen, de_system renderer)
{
    de_entity b = de_manager_new(m);
    if (!b) return;

    Bala *dat = (Bala *)b->data;
    dat->pos = origen;
    dat->vel = (Vec2){ 0.0f, 1.5f };
    dat->vida = 40;

    b->state = bala_actualizar;
    DE_SYSTEM_ADD(renderer, &dat->pos);
}
```

Modifica el estado de la nave para disparar automáticamente:

```c
void *nave_actualizar(void *data)
{
    Entidad *n = (Entidad *)data;
    n->pos.x += n->vel.x;
    /* ... rebotes ... */

    /* Disparo automático cada frame (para el ejemplo) */
    static int cooldown = 0;
    if (--cooldown <= 0) {
        cooldown = 10;
        disparar(n->owner, n->pos, &renderer);  /* necesitarías pasar renderer */
    }

    return DE_STATE_LOOP;
}
```

> **Truco:** Como `de_manager_update` itera de atrás hacia adelante, si un asteroide y una bala colisionan y ambos se marcan para borrar, no hay problema de índices inválidos.

---

## 7. Paso 5: Colisiones y destrucción

Implementemos un sistema de colisión simple: si una bala toca un asteroide, ambos mueren.

Usaremos `DE_MANAGER_FOREACH` para recorrer activas:

```c
void verificar_colisiones(de_manager m)
{
    DE_MANAGER_FOREACH(m, {
        if (ENTITY->state == DE_STATE_DELETE) continue;

        Entidad *e = (Entidad *)ENTITY->data;
        if (e->tag != TAG_ASTEROIDE) continue;

        /* Revisamos contra todas las balas */
        DE_MANAGER_FOREACH(m, {
            if (OTHER->state == DE_STATE_DELETE) continue;
            Bala *b = (Bala *)OTHER->data;

            /* Distancia simple */
            float dx = e->pos.x - b->pos.x;
            float dy = e->pos.y - b->pos.y;
            if (dx*dx + dy*dy < 4.0f) {
                de_entity_delete(ENTITY);
                de_entity_delete(OTHER);
                printf("¡BOOM!\n");
                break;
            }
        });
    });
}
```

Añade `verificar_colisiones(&manager)` en tu loop de juego, entre `update` y `draw`.

### `de_entity_delete` en profundidad

Cuando llamas `de_entity_delete(e)`:
1. Si tiene un `destructor` activo, lo ejecuta.
2. Si está en zona activa: intercambia con la última activa y reduce `size`.
3. Si está en zona pausada: intercambia con la primera pausada y aumenta `paused` (reduce la zona pausada).
4. El slot pasa a la zona libre.

---

## 8. Paso 6: Pausa estratégica

Imagina que queremos congelar asteroides cuando la nave usa un "escudo de tiempo". Pausar una entidad la saca del loop de update pero **no libera su memoria**.

```c
/* En algún momento del juego... */
de_entity algun_asteroide = ...;
de_entity_pause(algun_asteroide);  /* Congelado. Su data[] sigue válido. */
```

Más tarde:

```c
de_entity_resume(algun_asteroide);  /* Vuelve a la zona activa */
```

### ¿Por qué es esto potente?

Un `de_system` puede seguir apuntando a `entity->data` de una entidad pausada. Por ejemplo, un sistema de renderizado puede seguir dibujándola (como congelada en hielo) mientras la lógica deja de procesarla.

> **Regla de oro:** Nunca guardes punteros a `data[]` de entidades activas que puedan destruirse. Sí es seguro para entidades pausadas.

---

## 9. Paso 7: Limpieza con destructores

Cuando un asteroide explota, queremos spawnear partículas. El destructor se ejecuta **justo antes** de que la entidad sea movida a la zona libre.

```c
void *asteroide_destructor(void *data)
{
    Entidad *e = (Entidad *)data;
    printf("Asteroide en (%.0f,%.0f) destruido\n", e->pos.x, e->pos.y);
    /* Aquí podrías spawnear partículas, sumar puntaje, etc. */
    return 0;  /* el retorno del destructor se ignora */
}

/* Al crear el asteroide: */
a->destructor = asteroide_destructor;
```

Para limpiar todo al cerrar el juego:

```c
de_manager_reset(&manager);  /* Borra todas las activas y pausadas */
```

---

## 10. API Completa de referencia

### Tipos

```c
typedef void *(*de_state)(void *);
typedef struct de_entity *de_entity;
typedef struct de_manager *de_manager;
typedef struct de_system *de_system;
```

### Macros de almacenamiento

```c
DE_MANAGER_STORAGE(name, capacity, payload_size)  /* declara memoria estática */
DE_MANAGER_ARGS(name)                             /* argumentos para init */

DE_SYSTEM_STORAGE(name, capacity, params)         /* declara pool estático */
DE_SYSTEM_ARGS(name)                              /* argumentos para init */
```

### Macros de iteración

```c
DE_MANAGER_FOREACH(manager, code)   /* itera zona activa, define ENTITY */

DE_SYSTEM_ADD(system, ptr1, ...)    /* añade grupo de punteros */
DE_SYSTEM_FOREACH(system, a, b, code)  /* itera desestructurando params */
```

### Funciones de entidad

| Función | Descripción |
|---------|-------------|
| `void *de_entity_exec(e)` | Ejecuta el estado una vez manualmente |
| `void *de_entity_update(e)` | Ejecuta y actualiza el campo `state` |
| `uint16_t de_entity_pause(e)` | Mueve a zona pausada. Retorna 1 si tuvo éxito |
| `uint16_t de_entity_resume(e)` | Mueve a zona activa. Retorna 1 si tuvo éxito |
| `uint16_t de_entity_delete(e)` | Borra (llama destructor, mueve a libre) |
| `uint16_t de_entity_move_front(e)` | Último en el array activo (se dibuja encima) |
| `uint16_t de_entity_move_back(e)` | Primero en el array activo (se dibuja debajo) |

### Funciones de manager

| Función | Descripción |
|---------|-------------|
| `void de_manager_init(m, pool, storage, cap, bytes)` | Inicializa manager |
| `de_entity de_manager_new(m)` | Crea entidad en zona activa |
| `void de_manager_update(m)` | Ejecuta estados de todas las activas |
| `void de_manager_reset(m)` | Borra todo y reinicia zonas |

### Funciones de system

| Función | Descripción |
|---------|-------------|
| `void de_system_init(s, storage, cap_groups, params)` | Inicializa sistema |
| `uint16_t de_system_remove(s, first_ptr)` | Elimina grupo cuyo primer ptr coincide |

### Constantes de estado

```c
DE_STATE_DELETE   /* Destruir entidad */
DE_STATE_LOOP     /* Mantener estado actual */
DE_STATE_PAUSE    /* Pausar entidad */
```

### Macros de consulta de zona

```c
DE_ENTITY_IN_ACTIVE(e)
DE_ENTITY_IN_PAUSED(e)
DE_ENTITY_IN_FREE(e)
```

---

## Filosofía de diseño

1. **Sin allocators ocultos:** Tú controlas el bloque de memoria. Ideal para sistemas embebidos o donde `malloc` no es bienvenido.
2. **Punteros estables en pausa:** Los sistemas pueden cachear `&entity->data` de entidades pausadas sin miedo a invalidación.
3. **Estados como máquina:** Cada entidad es una pequeña máquina de estados. No hay "sistemas que procesan entidades por tipo", sino que cada entidad se gestiona a sí misma.
4. **68K-friendly:** Alineación a 4 bytes, preferencia por `uint16_t`, y estructuras compactas.

---

## Compilación

Requisitos: GCC con soporte para extensiones GNU.

```bash
gcc -std=gnu99 -O2 -Wall main.c -o orbit_defender
```

Para plataformas Motorola 68000 (cross-compile):

```bash
m68k-elf-gcc -std=gnu99 -O2 -m68000 ...
```

---

*¡Eso es todo! Ahora tienes una nave, asteroides, disparos, colisiones, pausas y explosiones. Desde aquí puedes añadir power-ups, oleadas, o incluso múltiples managers para capas de fondo y primer plano. Darken no impone arquitectura: te da las herramientas para que construyas la tuya.*
