# Darken — Sistema de Entidades (darken.h)

**darken-2.0.0-dev** — Motor de entidades *header-only* para C, pensado originalmente
para GCC + Motorola 68000, pero usable en cualquier objetivo compatible con GNU C.

Darken gestiona un **pool de entidades de tamaño fijo**, repartido en tres zonas
contiguas dentro de un mismo array de punteros:

```
[ activas ][ libres ][ pausadas ]
0         size      paused      capacity
```

- **Activas** `[0, size)`: se actualizan cada frame y son visibles a `DARKEN_FOREACH`.
- **Libres** `[size, paused)`: slots reciclables, de aquí sale `darken_spawn()`.
- **Pausadas** `[paused, capacity)`: fuera del bucle de update; sus punteros a
  `entity->data` permanecen válidos mientras la entidad siga pausada.

La entidad en sí **nunca cambia de dirección de memoria** una vez asignada; lo único
que se mueve entre zonas es su *puntero* dentro de `pool[]`. Esto es lo que permite
guardar un puntero crudo a `entity->data` con seguridad, incluso mientras la entidad
es pausada, reanudada o reordenada internamente.

---

## Requisitos

- Compilador con extensiones GNU C (usa `__attribute__`).
- C99 o posterior (usa flexible array members).
- `#include <stdint.h>` (ya incluido por el header).
- Header-only: en **un** archivo `.c` define `DARKEN_IMPLEMENTATION` antes de incluirlo.

```c
// solo en un .c de tu proyecto:
#define DARKEN_IMPLEMENTATION
#include "darken.h"
```

---

## API pública (resumen)

### Tipos

| Tipo | Descripción |
|---|---|
| `darken` | Puntero opaco al manager (`darken *`). |
| `darken_entity` | Puntero opaco a una entidad (`struct darken_entity *`). |
| `darken_state` | `void *(*)()` — callback de estado/destructor. Recibe `entity->data` y devuelve el siguiente estado o un valor de control. |

### Valores de control (lo que puede devolver un `darken_state`)

| Valor | Efecto |
|---|---|
| `DARKEN_DELETE` | Elimina la entidad (invoca su destructor si tiene). |
| `DARKEN_LOOP` | Mantiene el estado actual sin cambios. |
| `DARKEN_PAUSE` | Pausa la entidad (sale del bucle de update). |
| *cualquier otro puntero* | Se interpreta como el **nuevo** `darken_state` de la entidad. |

### Funciones a nivel *manager*

| Función | Qué hace |
|---|---|
| `void darken_init(darken, darken_entity *pool, void *storage, uint16_t capacity, uint16_t bytes)` | Inicializa el manager sobre memoria ya reservada por el llamador. |
| `darken_entity darken_spawn(darken)` | Toma un slot libre y lo activa. Devuelve `NULL` si no queda espacio. |
| `void darken_update(darken)` | Recorre la zona activa: ejecuta estado, procesa pausas y borrados pendientes. |
| `void darken_reset(darken)` | Vacía el manager (ver ⚠️ en «Puntos flacos»). |

### Funciones a nivel *entidad*

| Función | Qué hace |
|---|---|
| `void darken_entity_run(darken_entity)` | Ejecuta el callback de estado **sin** aplicar su valor de retorno (no transiciona). |
| `void darken_entity_update(darken_entity)` | Ejecuta el callback y sí aplica el resultado (transiciona de estado). |
| `void darken_entity_pause(darken_entity)` | Mueve la entidad a la zona de pausadas. |
| `void darken_entity_resume(darken_entity)` | Devuelve la entidad a la zona activa (ver ⚠️ en «Puntos flacos»). |
| `void darken_entity_delete(darken_entity)` | Borra la entidad esté activa o pausada, invocando su destructor. |

### Macros de conveniencia

| Macro | Para qué sirve |
|---|---|
| `DARKEN(nombre, CAPACIDAD, TAMAÑO_PAYLOAD)` / `DARKEN_STORAGE(...)` | Declara el `pool[]` + bloque de datos alineado a 4 bytes. **No** declara el `struct darken` en sí — eso lo declaras tú aparte. |
| `DARKEN_ARGS(nombre)` | Expande a `pool, data, capacity, payload_size` para pasar a `darken_init`. |
| `DARKEN_DATA(TIPO, var, entidad)` | `TIPO *var = (TIPO *)entidad->data;` — acceso tipado al payload. |
| `DARKEN_FOREACH(manager, CODIGO)` | Itera la zona activa; expone `ENTITY` (tipo `darken_entity`) dentro de `CODIGO`. |
| `DARKEN_STATE_IS_DELETED/LOOP/PAUSED/ACTIVE(estado)` | Clasifica un valor `darken_state`. |
| `DARKEN_ASSERT(condicion, valor_de_retorno)` | `if (!condicion) return valor_de_retorno;` — guard rápido. |

### Campos públicos de `darken_entity`

```c
darken_state state;       // callback ejecutado cada update
darken_state destructor;  // opcional; se llama al borrar la entidad
uint32_t     tag;          // libre para el usuario
uint16_t     usr;           // libre para el usuario
uint8_t      data[];        // payload de tamaño variable
```

---

## Puntos fuertes

- **Sin heap en caliente**: spawn, pausa y borrado son swaps O(1) sobre un array
  ya reservado — sin `malloc`/`free` por entidad, sin fragmentación.
- **Direcciones estables**: puedes guardarte un `Particle *p` obtenido vía
  `DARKEN_DATA` y seguir usándolo con seguridad aunque la entidad se pause o el
  pool se reordene internamente — solo cambia su *posición* en `pool[]`, no su
  dirección física.
- **Buena localidad de caché**: `darken_update` solo recorre `[0, size)`, nunca
  toca slots libres ni pausados.
- **Diseño consciente de la plataforma**: alineación a 4 bytes calculada en
  `_DARKEN_ENTITY_STRIDE`, preferencia por miembros de 16 bits — pensado en serio
  para 68K, no solo de nombre.
- **FSM compacta**: los valores de control (`DELETE`/`LOOP`/`PAUSE`) viajan en el
  valor de retorno del propio callback, sin necesitar un campo de estado aparte.

## Puntos flacos

- **⚠️ Bug confirmado en `darken_entity_resume()`**: si en la zona de pausadas hay
  más de una entidad y reanudas una que **no** está justo en el borde de esa zona,
  el algoritmo mueve a la entidad equivocada. Lo verifiqué compilando y ejecutando
  código real: al pausar 3 entidades y reanudar la del medio, esta se queda donde
  estaba (nunca vuelve a estar activa) y otra entidad pausada distinta termina
  marcada como activa por error. La causa es que la función reutiliza una
  variable local con la posición *antigua* de la entidad en el segundo `swap`, en
  vez de releer su posición actual (que sí cambió tras el primer `swap`). Si en tu
  proyecto pausas más de una entidad a la vez, evita `resume()` hasta corregirlo,
  o corrígelo tú mismo siguiendo el mismo patrón que usa `_DARKEN_ENTITY_PAUSE`
  (releer `entidad->slot` en cada paso en vez de cachearlo).
- **⚠️ Bug confirmado en `darken_reset()`**: reutiliza `DARKEN_FOREACH`, que solo
  recorre la zona activa por diseño (así debe ser para el update por frame). Pero
  eso significa que **las entidades pausadas nunca pasan por su destructor** al
  hacer reset — lo confirmé con un destructor que imprime un mensaje: con
  entidades pausadas presentes, el mensaje no aparece para ellas. Si el
  destructor libera recursos (memoria externa, handles, etc.), esto es una fuga
  silenciosa cada vez que reseteas el manager con algo pausado.
- **`darken_state` sin prototipo** (`void *(*)()` en vez de `void *(*)(void *)`):
  es sintaxis C válida pero de estilo K&R, y el compilador no puede verificar que
  tus callbacks reciban el argumento correcto.
- **Comparación de punteros con `>` para `DARKEN_STATE_IS_ACTIVE`**: formalmente
  es comportamiento no definido en C estándar comparar punteros que no pertenecen
  al mismo array. Funciona en la práctica en cualquier plataforma real (los
  punteros se tratan como enteros), pero conviene saber que la librería depende
  de eso.
- **`darken_entity_run()` y `darken_entity_update()`** se parecen mucho pero
  tienen semántica distinta (una ignora el valor de retorno del callback, la otra
  lo aplica), y esa diferencia no está explicada dentro del propio header.
- **`DARKEN` y `DARKEN_STORAGE` son alias idénticos** — no hay razón aparente
  para tener dos nombres para la misma macro.
- **`DARKEN(...)` no crea el manager completo**: solo reserva `pool[]` + datos.
  Aún necesitas declarar tú mismo un `darken manager;` y pasarlo a mano a
  `darken_init` — algo que el nombre corto de la macro no deja claro a primera
  vista.
- Capacidad limitada a `uint16_t` (65535 entidades) — razonable para el target
  original de recursos limitados, pero sin ningún `static_assert` que avise si
  pides una capacidad o un payload demasiado grandes.

---

## Cómo se usa

### 1. Configuración mínima

Cada manager necesita dos cosas: el almacenamiento (`DARKEN_STORAGE`, que
reserva `pool[]` y el bloque de datos) y el propio `struct darken` que lo
gobierna, declarado aparte:

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"

typedef struct {
    float x, y;
    float vy;
    int   life;
} Particle;

// Reserva sitio para 64 entidades, cada una con un payload "Particle"
DARKEN_STORAGE(storage, 64, sizeof(Particle));

// El manager en sí — tú lo declaras, la macro no lo hace por ti
darken manager;

int main(void)
{
    darken_init(&manager, DARKEN_ARGS(storage));
    // manager ya está listo: size=0, paused=64, capacity=64
    return 0;
}
```

`DARKEN_ARGS(storage)` expande a `storage.pool, storage.data, storage.capacity,
storage.payload_size` — por eso `darken_init` recibe 5 argumentos en total (el
`&manager` que pones tú, más los 4 que trae la macro).

### 2. Crear entidades y acceder a su payload

`darken_spawn` te da un `darken_entity` (el "handle"); `DARKEN_DATA` te da un
puntero tipado a su payload:

```c
darken_entity spawn_particle(float x, float y, float vy, int life)
{
    darken_entity e = darken_spawn(&manager);
    if (!e)
        return NULL; // pool lleno (size == paused)

    DARKEN_DATA(Particle, p, e);
    p->x = x;
    p->y = y;
    p->vy = vy;
    p->life = life;

    e->tag = 42;          // libre para lo que quieras (identificar tipo, etc.)
    e->destructor = NULL;  // sin destructor por ahora

    return e;
}
```

Nota que `e` (el `darken_entity`) y `p` (el puntero a `Particle`) son direcciones
**diferentes**: `p` apunta justo dentro de `e->data`. `p` sigue siendo válido
aunque la entidad se pause o el pool se reordene, porque la dirección física de
`e` (y por tanto de `e->data`) nunca cambia.

### 3. Definir el comportamiento con una máquina de estados

`e->state` es el callback que se ejecuta cada `darken_update`. Recibe
**directamente el payload** (`void *data`, ya apuntando a tu struct, no la
entidad), y su valor de retorno decide qué pasa después:

```c
void *particle_falling(void *data)
{
    Particle *p = (Particle *)data;

    p->y += p->vy;
    p->life--;

    if (p->y >= 10.0f)
        return particle_landed;   // <- transición: cambia el estado

    if (p->life <= 0)
        return DARKEN_DELETE;     // <- se borra este frame

    return DARKEN_LOOP;           // <- sigue en "particle_falling"
}

void *particle_landed(void *data)
{
    Particle *p = (Particle *)data;

    if (--p->life <= 0)
        return DARKEN_DELETE;

    return DARKEN_LOOP;
}
```

Y al crear la entidad, le asignas el estado inicial:

```c
darken_entity e = spawn_particle(0.0f, 0.0f, 2.0f, 4);
e->state = particle_falling;
```

### 4. El bucle principal

```c
int frame = 0;
while (manager.size > 0 && frame < 1000)
{
    darken_update(&manager);   // ejecuta el estado de cada entidad activa,
                                // aplica pausas y borrados que hayan pedido
    frame++;
}
```

`darken_update` recorre `[0, size)` de atrás hacia adelante (para que los swaps
de borrado/pausa no salten ninguna entidad), y por cada una:
- si su `state` es un callback "activo" (> `DARKEN_PAUSE`), lo ejecuta y aplica
  el resultado;
- si su `state` es exactamente `DARKEN_PAUSE`, la mueve a la zona pausada;
- si su `state` es exactamente `DARKEN_DELETE`, la borra (llamando antes a su
  destructor, si tiene).

### 5. Destructores

Se ejecutan automáticamente al borrar una entidad (por `DARKEN_DELETE` desde su
propio estado, o por `darken_entity_delete` desde fuera). Reciben el mismo
payload que el estado:

```c
void *particle_destructor(void *data)
{
    Particle *p = (Particle *)data;
    printf("particula en (%.1f, %.1f) destruida\n", p->x, p->y);
    return NULL; // el valor de retorno del destructor se ignora
}

// al crearla:
e->destructor = particle_destructor;
```

### 6. Iterar manualmente con `DARKEN_FOREACH`

Útil para lógica que no encaja en el propio estado de la entidad — por ejemplo,
detección de colisiones entre todas las entidades activas:

```c
DARKEN_FOREACH(&manager, {
    Particle *p = (Particle *)ENTITY->data;
    printf("tag=%u en (%.1f, %.1f), vida=%d\n", ENTITY->tag, p->x, p->y, p->life);
});
```

Dentro del bloque, `ENTITY` (tipo `darken_entity`) queda disponible automáticamente
— lo define la propia macro. Solo visita la zona activa, nunca libres ni
pausadas.

### 7. Pausar y reanudar

Pausar una entidad la saca del bucle de `darken_update` sin borrarla — sus datos
siguen intactos y su dirección de memoria no cambia:

```c
darken_entity_pause(e);   // e sale de la zona activa

// ... más adelante ...

darken_entity_resume(e);  // e vuelve a la zona activa
```

> ⚠️ Como se explica en «Puntos flacos», `darken_entity_resume` tiene un bug
> confirmado cuando hay **más de una** entidad pausada a la vez y reanudas una
> que no es la última en pausarse. Con una sola entidad pausada en cada momento
> funciona correctamente (lo verifiqué); con varias, revisa/corrige la función
> antes de confiar en ella en producción.

### 8. Borrar entidades desde fuera del bucle de estado

```c
darken_entity_delete(e); // funciona tanto si "e" está activa como pausada
```

### 9. Vaciar el manager entero

```c
darken_reset(&manager); // size vuelve a 0, paused vuelve a capacity
```

> ⚠️ Ver «Puntos flacos»: si tenías entidades pausadas en el momento del reset,
> **sus destructores no se ejecutarán**. Si tus destructores liberan recursos
> externos, asegúrate de pausar nada (o de reanudar/borrar todo explícitamente)
> antes de llamar a `darken_reset`, hasta que ese comportamiento se corrija.

### 10. Ejemplo completo (arriba hacia abajo)

```c
#include <stdio.h>
#define DARKEN_IMPLEMENTATION
#include "darken.h"

typedef struct {
    float x, y, vy;
    int   life;
} Particle;

DARKEN_STORAGE(storage, 64, sizeof(Particle));
darken manager;

void *particle_falling(Particle *p)
{
    p->y += p->vy;
    if (--p->life <= 0)
        return DARKEN_DELETE;
        
    return DARKEN_LOOP;
}

void *particle_destructor(Particle *p)
{
    printf("particula en (%.1f, %.1f) destruida\n", p->x, p->y);
    return NULL;
}

int main(void)
{
    darken_init(&manager, DARKEN_ARGS(storage));

    for (int i = 0; i < 5; i++)
    {
        darken_entity e = darken_spawn(&manager);
        DARKEN_DATA(Particle, p, e);
        p->x = (float)i; p->y = 0; p->vy = 1.0f; p->life = 3 + i;
        e->state = particle_falling;
        e->destructor = particle_destructor;
    }

    while (manager.size > 0)
        darken_update(&manager);

    return 0;
}
```

Este ejemplo (y todos los anteriores) fue compilado y ejecutado contra
`darken.h` para confirmar que el comportamiento descrito es real y no solo
teórico.
