# Darken 2.0 — El tutorial que no te hará llorar

> Oye, ¿te contaron que programar un entity system en C es un dolor de muelas? Mentira. Con Darken es más bien como armar un LEGO: piezas chiquitas, clicks satisfactorios, y al final tienes algo que se mueve en pantalla.

Vamos a hacer un jueguito de **naves y basura espacial**. Nada de arquitecturas enterprise ni 47 capas de abstracción. Una nave, unos asteroides, y un loop que hace *tick tick tick*.

---

## ¿Qué necesito saber antes de empezar?

Tres cosas nada más:

1. **El Manager** es como el dueño del antro. Decide quién entra, quién sale, y quién está en la pista de baile (activas) versus quién está en la barra de al lado tomando una pausa (pausadas).
2. **Las entidades** son tus objetos del juego: nave, asteroides, disparos, partículas... Cada una tiene un `state`, que no es más que una función que le dice "hacé esto cada frame".
3. **`de_manager_update()`** es el DJ. Cada vez que lo llamás, pasa por todas las entidades activas y les dice "dale, tocá tu tema".

Listo. Con eso ya podemos arrancar.

---

## Paso 1: Preparar la escena

Primero: reservamos memoria. Darken no te roba bytes a escondidas; vos le decís "che, reservame espacio para 50 entidades de tamaño X" y listo.

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdio.h>
#include <stdlib.h>

/* Nuestros datos de juego */
typedef struct {
    float x, y;
    float vx, vy;
    int   vida;
} Coso;

int main(void)
{
    /* Esto crea una struct anónima con:
       - pool[] : los punteros a entidades
       - data[] : la memoria REAL donde viven los Coso
    */
    DE_MANAGER_STORAGE(mundo, 50, sizeof(Coso));

    de_manager manager;
    de_manager_init(&manager, DE_MANAGER_ARGS(mundo));

    printf("Manager listo. Capacidad: %u\n", manager.capacity);
    return 0;
}
```

Compilalo:
```bash
gcc -std=gnu99 main.c -o jueguito && ./jueguito
```

Si ves `Manager listo. Capacidad: 50`, ya ganaste la mitad de la batalla.

---

## Paso 2: Crear entidades (la parte divertida)

`de_manager_new()` te da una entidad fresquita del horno. Si te devuelve `NULL`, es que no hay lugar (ya llenaste el antro).

```c
de_entity crear_nave(de_manager *m)
{
    de_entity e = de_manager_new(m);
    if (!e) {
        printf("No hay lugar para más naves, capo.\n");
        return NULL;
    }

    Coso *c = (Coso *)e->data;
    c->x = 40.0f;
    c->y = 20.0f;
    c->vx = 0.0f;
    c->vy = 0.0f;
    c->vida = 3;

    /* El tag es libre, lo usamos como queramos */
    e->tag = 1;  /* 1 = nave */

    return e;
}
```

Llamalo en tu `main`:

```c
    de_entity nave = crear_nave(&manager);
    if (nave) {
        Coso *c = (Coso *)nave->data;
        printf("Nave creada en (%.0f, %.0f) con %d vidas\n", c->x, c->y, c->vida);
    }
```

**¿Qué pasó acá?**
- `de_manager_new` agarró un slot del área libre y lo pasó al área activa.
- `e->data` apunta directo a tu `Coso`. No hay magia, no hay offsets raros. Es tu struct, punto.

---

## Paso 3: Darle vida con estados y `de_manager_update()`

Acá viene la magia. Cada entidad tiene un `state`: una función que recibe `void *data` y devuelve qué hacer después.

```c
void *estado_nave(void *data)
{
    Coso *c = (Coso *)data;

    /* Movimiento automático para el ejemplo */
    c->x += c->vx;
    c->y += c->vy;

    /* Rebote bobo contra bordes imaginarios */
    if (c->x < 0 || c->x > 80) c->vx *= -1;
    if (c->y < 0 || c->y > 24) c->vy *= -1;

    /* Seguí con este estado el próximo frame */
    return DE_STATE_LOOP;
}
```

Asignalo cuando creás la nave:

```c
    nave->state = estado_nave;
    ((Coso *)nave->data)->vx = 0.5f;
    ((Coso *)nave->data)->vy = 0.2f;
```

Y ahora el loop de juego:

```c
    for (int frame = 0; frame < 200; frame++) {
        de_manager_update(&manager);  /* ¡ESTO ES TODO! */

        /* Solo para ver que se mueve */
        Coso *c = (Coso *)nave->data;
        printf("\rFrame %3d | Nave en (%.1f, %.1f)", frame, c->x, c->y);
        fflush(stdout);
    }
    printf("\n");
```

### ¿Qué hace `de_manager_update()` por dentro?

Recorre las entidades activas **de atrás para adelante** (sí, en reversa como *Tenet*). Para cada una:

1. Llama a `entity->state(entity->data)`.
2. Si devuelve `DE_STATE_LOOP`, no toca nada. La entidad sigue viva y con el mismo estado.
3. Si devuelve `DE_STATE_DELETE`, la mata ahí nomás.
4. Si devuelve `DE_STATE_PAUSE`, la saca de la pista de baile.
5. Si devuelve otra función, le cambia el estado a esa nueva función.

> **¿Por qué de atrás para adelante?** Porque si una entidad se borra a sí misma (o a otras), los índices del array no se rompen. Es una genialidad simple que te salva de un montón de dolores de cabeza.

---

## Paso 4: Meter un `de_system` (porque pintó)

Los managers gestionan la vida de las cosas. Los **systems** procesan datos de forma cache-friendly. Vamos a hacer un sistema de **partículas de motor** para la nave.

Primero, necesitamos un system. Es un array plano de punteros. Si le decís que tiene 2 parámetros, guarda grupos de 2 punteros seguidos: `(ptr1, ptr2), (ptr1, ptr2)...`

```c
/* Sistema de partículas: guarda (posición, velocidad) */
DE_SYSTEM_STORAGE(particulas, 100, 2);

/* Función que actualiza todas las partículas */
void *actualizar_particulas(de_system sys)
{
    /* DE_SYSTEM_FOREACH_2 desempaqueta 2 punteros por grupo */
    DE_SYSTEM_FOREACH_2(sys, float *px, float *py, {
        *px += (rand() % 3 - 1) * 0.1f;  /* jitter en X */
        *py += 0.3f;                      /* caen */
    });
    return DE_STATE_LOOP;
}
```

En el `main`, inicializalo y usalo:

```c
    de_system motor;
    de_system_init(&motor, DE_SYSTEM_ARGS(particulas));

    /* Cada vez que queremos una partícula, hacemos: */
    float *pos_x = &((Coso *)nave->data)->x;
    float *pos_y = &((Coso *)nave->data)->y;
    DE_SYSTEM_ADD(&motor, pos_x, pos_y);  /* guardamos punteros a la posición de la nave */
```

Y en el loop:

```c
    for (int frame = 0; frame < 200; frame++) {
        de_manager_update(&manager);
        actualizar_particulas(&motor);

        /* ... imprimir cosas ... */
    }
```

### ¿Qué onda el `de_system`?

Es una caja de punteros planos. No sabe de entidades, no sabe de managers. Solo sabe que tiene `params` punteros por grupo y un montón de grupos. Iterar es ultra rápido porque todo está pegadito en memoria.

> **Truco:** Si tenés un sistema de renderizado, un sistema de física, y un sistema de sonido, cada uno puede tener su propio `de_system` apuntando a los datos que le importan. Nada de "buscar componentes por ID". Punteros directos, amigo.

---

## Paso 5: Spawnear enemigos y matarlos

Vamos a crear asteroides que se destruyen solos al salir de pantalla.

```c
void *estado_asteroide(void *data)
{
    Coso *c = (Coso *)data;
    c->y += c->vy;

    if (c->y > 25.0f)
        return DE_STATE_DELETE;  /* Chau, asteroide */

    return DE_STATE_LOOP;
}

de_entity crear_asteroide(de_manager *m)
{
    de_entity e = de_manager_new(m);
    if (!e) return NULL;

    Coso *c = (Coso *)e->data;
    c->x = rand() % 80;
    c->y = 0.0f;
    c->vx = 0.0f;
    c->vy = 0.1f + (rand() % 5) / 10.0f;
    c->vida = 1;
    e->tag = 2;  /* 2 = asteroide */
    e->state = estado_asteroide;

    return e;
}
```

Y en el loop, spawnear cada 30 frames:

```c
    for (int frame = 0; frame < 300; frame++) {
        if (frame % 30 == 0) crear_asteroide(&manager);

        de_manager_update(&manager);

        printf("\rFrame %3d | Activas: %2u | Libres: %2u",
               frame, manager.size, manager.paused - manager.size);
        fflush(stdout);
    }
```

Fijate cómo `manager.size` va creciendo cuando aparecen asteroides y bajando cuando se destruyen. `de_manager_update()` se encarga de todo: llama los estados, borra lo que devuelve `DE_STATE_DELETE`, y reordena punteros para que no queden huecos.

---

## Paso 6: El gran final — Todo junto

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  /* para usleep */

typedef struct {
    float x, y;
    float vx, vy;
    int   vida;
} Coso;

/* ---------- ESTADOS ---------- */

void *estado_nave(void *data)
{
    Coso *c = (Coso *)data;
    c->x += c->vx;
    c->y += c->vy;
    if (c->x < 0 || c->x > 80) c->vx *= -1;
    if (c->y < 0 || c->y > 24) c->vy *= -1;
    return DE_STATE_LOOP;
}

void *estado_asteroide(void *data)
{
    Coso *c = (Coso *)data;
    c->y += c->vy;
    if (c->y > 25.0f) return DE_STATE_DELETE;
    return DE_STATE_LOOP;
}

/* ---------- AYUDANTES ---------- */

de_entity crear_nave(de_manager *m)
{
    de_entity e = de_manager_new(m);
    if (!e) return NULL;
    Coso *c = (Coso *)e->data;
    c->x = 40; c->y = 12; c->vx = 0.3f; c->vy = 0.15f; c->vida = 3;
    e->tag = 1;
    e->state = estado_nave;
    return e;
}

de_entity crear_asteroide(de_manager *m)
{
    de_entity e = de_manager_new(m);
    if (!e) return NULL;
    Coso *c = (Coso *)e->data;
    c->x = rand() % 80; c->y = 0;
    c->vx = 0; c->vy = 0.1f + (rand() % 5)/10.0f; c->vida = 1;
    e->tag = 2;
    e->state = estado_asteroide;
    return e;
}

/* ---------- MAIN ---------- */

int main(void)
{
    DE_MANAGER_STORAGE(mundo, 50, sizeof(Coso));
    de_manager manager;
    de_manager_init(&manager, DE_MANAGER_ARGS(mundo));

    /* Sistema de partículas del motor */
    DE_SYSTEM_STORAGE(particulas, 100, 2);
    de_system motor;
    de_system_init(&motor, DE_SYSTEM_ARGS(particulas));

    crear_nave(&manager);

    printf("=== ORBIT DEFENDER (modo terminal) ===\n\n");

    for (int frame = 0; frame < 200; frame++) {
        if (frame % 30 == 0) crear_asteroide(&manager);

        de_manager_update(&manager);

        /* Dibujito bobo en terminal */
        printf("\033[2J\033[H");  /* limpiar pantalla */
        printf("Frame: %d | Activas: %u | Libres: %u\n", frame, manager.size, manager.paused - manager.size);

        DE_MANAGER_FOREACH(&manager, {
            Coso *c = (Coso *)ENTITY->data;
            if (ENTITY->tag == 1)
                printf("\033[%d;%dH@", (int)c->y, (int)c->x);  /* nave */
            else
                printf("\033[%d;%dH#", (int)c->y, (int)c->x);  /* asteroide */
        });

        fflush(stdout);
        usleep(50000);  /* ~20 FPS */
    }

    printf("\n\n¡Fin! No explotaste. Eso ya es algo.\n");
    return 0;
}
```

Compilá y corrélos:

```bash
gcc -std=gnu99 main.c -o orbit && ./orbit
```

---

## Cheat sheet rápida

| Quiero... | Hago... |
|-----------|---------|
| Crear algo | `de_manager_new(&manager)` |
| Que se mueva/cambie cada frame | Le asigno un `state` y llamo `de_manager_update()` |
| Que desaparezca | El `state` devuelve `DE_STATE_DELETE` |
| Procesar muchos datos rápido | `de_system` + `DE_SYSTEM_FOREACH` |
| Saber si algo está activo | `DE_ENTITY_IN_ACTIVE(e)` |
| Resetear todo | `de_manager_reset(&manager)` |

---

## Y ahora, ¿qué?

Ya tenés el esqueleto. De acá podés:

- Agregar `de_entity_delete()` manualmente cuando un asteroide choque con la nave.
- Usar `e->destructor` para spawnear explosiones cuando algo muere.
- Pausar entidades con `de_entity_pause()` (útil para menú de pausa o efectos de congelamiento).
- Hacer más `de_system`: uno para sonido, otro para IA, otro para networking.

Darken no te dice cómo estructurar tu juego. Te da un manager que no se rompe, un system que itera rápido, y estados que son solo funciones. El resto lo inventás vos.

**A codear, campeón.** 🚀
