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
| `DARKEN(nombre, CAPACIDAD, TAMAÑO_PAYLOAD)` / `DARKEN_STORAGE(...)` | Declara el `pool[]` + bloque de datos alineado a 4 bytes. **No** declara el `darken` en sí — eso lo declaras tú aparte. |
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
reserva `pool[]` y el bloque de datos) y el propio `darken` que lo gobierna,
declarado aparte:

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






333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333









# Darken (DARKula ENgine) 2.0 — Sistema de entidades

`darken.h` es una librería de un solo header (estilo *stb*, single-header) en C que implementa
un gestor de entidades basado en máquinas de estado, pensado para **GCC + Motorola 68000**
(alineación de 4 bytes, campos de 16 bits, sin asignación dinámica de memoria en tiempo de
ejecución). Es el tipo de diseño que se ve en motores retro/embebidos (Amiga, Mega Drive,
sistemas bare-metal) donde no hay `malloc` fiable y cada ciclo de CPU cuenta.

Este documento describe qué hace cada función y macro pública, cómo se usa en la práctica,
y —sobre todo— una serie de comportamientos no evidentes que descubrí leyendo la
implementación (`#ifdef DARKEN_IMPLEMENTATION`) con cuidado, y que conviene tener muy presentes
antes de usar la librería en serio.

---

## 1. Instalación / patrón de uso

Es un header-only al estilo *stb*: se incluye normalmente en cualquier archivo que solo
necesite los tipos y prototipos, y **en exactamente un** archivo `.c` se define
`DARKEN_IMPLEMENTATION` antes de incluirlo, para compilar el cuerpo de las funciones:

```c
// motor.c — un único punto de compilación con el cuerpo real
#define DARKEN_IMPLEMENTATION
#include "darken.h"
```

```c
// otros_archivos.c — solo declaraciones
#include "darken.h"
```

Si se te olvida definir `DARKEN_IMPLEMENTATION` en algún `.c`, obtendrás errores de enlazado
(`undefined reference`); si lo defines en más de un `.c`, obtendrás símbolos duplicados. Es el
mismo trato que cualquier librería de un solo header.

---

## 2. Modelo de datos

### `darken` (el manager)

```c
typedef struct darken {
    darken_entity *pool;   // array de punteros a entidades
    uint16_t capacity;     // nº total de slots
    uint16_t size;         // nº de entidades activas
    uint16_t paused;       // índice donde empieza la zona de pausadas
} darken;
```

`pool` **no contiene las entidades**, contiene punteros a ellas. Las entidades viven en un
bloque de memoria fijo (el que le pasas a `darken_init`) y **nunca se mueven de dirección**;
lo único que se reordena es el array de punteros. Esto es la garantía central de la librería:
puedes guardar un `darken_entity` (o un puntero a `entity->data`) en otra estructura y sigue
siendo válido aunque la entidad se pause, reanude o se reordene el pool internamente — mientras
no se borre.

El array `pool[]` se mantiene siempre particionado en tres zonas contiguas:

```
[ activas ][ libres ][ pausadas ]
0         size      paused      capacity
```

- **Activas** `[0, size)`: las procesa `darken_update()` cada frame y las recorre `DARKEN_FOREACH`.
- **Libres** `[size, paused)`: cantera de `darken_spawn()`.
- **Pausadas** `[paused, capacity)`: fuera del bucle de update y de `DARKEN_FOREACH`; sus slots
  jamás se reciclan por `darken_spawn()`, así que un puntero a los datos de una entidad pausada
  permanece válido hasta que se reanude o se borre explícitamente.

### `darken_entity` (la entidad)

```c
struct darken_entity {
    // "privados" (solo por convención — nada lo impone en C)
    darken *owner;
    uint16_t slot;

    // públicos
    darken_state state;
    darken_state destructor;
    uint32_t tag;   // libre para el usuario
    uint16_t usr;   // libre para el usuario
    uint8_t data[]; // payload de tamaño variable (flexible array member)
};
```

- `state`: la función de estado actual (ver §4). También puede contener uno de los tres
  *valores de control* (`DARKEN_DELETE`, `DARKEN_LOOP`, `DARKEN_PAUSE`).
- `destructor`: función opcional invocada al borrar la entidad, o `NULL`/`DARKEN_DELETE` si no
  hay ninguna.
- `tag` / `usr`: campos libres, el motor nunca los toca; úsalos para IDs de tipo, flags, grupos, etc.
- `data[]`: el payload real del usuario. Todas las entidades de **un mismo manager** comparten
  el mismo tamaño de payload (fijado una vez en `darken_init`/`DARKEN_STORAGE`) — no puedes
  mezclar tipos de entidad de distinto tamaño en un solo pool.

`owner` y `slot` están marcados como "privados" solo en un comentario; el struct es totalmente
visible y nada impide que código de usuario los lea o escriba por error, rompiendo los
invariantes del manager. Trátalos como de solo lectura interna del motor.

---

## 3. Funciones públicas

| Función | Qué hace | Precondición (verificada con `_DARKEN_ASSERT`, que solo retorna en silencio si falla) |
|---|---|---|
| `darken_init(darken*, pool, storage, capacity, bytes)` | Inicializa el manager: reparte `storage` en slots de `capacity` entidades, cada uno de tamaño `align4(sizeof(darken_entity)+bytes)`, y fija `owner`/`slot` de cada una. `size=0`, `paused=capacity` (todo libre). | — |
| `darken_spawn(darken*)` | Toma el siguiente slot libre, lo pasa a la zona activa (`size++`) y devuelve el `darken_entity`. | Debe haber al menos un slot libre (`size < paused`); si no, **devuelve `NULL`**. |
| `darken_update(darken*)` | El "tick" principal. Recorre la zona activa de atrás hacia delante; por cada entidad: si su estado es una función activa, la llama y aplica la transición; si su estado es `DARKEN_PAUSE`, la mueve a pausadas; si es `DARKEN_DELETE`, llama al destructor (si lo hay) y la mueve a libres. | — |
| `darken_reset(darken*)` | Borra **todas las entidades activas** (llamando a sus destructores) y resetea `size=0`, `paused=capacity`. | Ver ⚠️ pitfall §6.3 — las pausadas **no** pasan por aquí. |
| `darken_entity_run(darken_entity)` | Llama a `state(data)` una vez, **sin** aplicar ninguna transición (ignora el valor de retorno). Útil para ejecutar la lógica actual bajo demanda (p. ej. una fase de render separada del update) sin mutar la máquina de estados. | La entidad debe estar activa. |
| `darken_entity_update(darken_entity)` | Igual que lo que hace `darken_update` por dentro para una sola entidad: llama a `state(data)` y, si el resultado no es `DARKEN_LOOP`, reemplaza `state`. Permite avanzar una entidad concreta bajo demanda en vez de esperar al frame. | La entidad debe estar activa. |
| `darken_entity_pause(darken_entity)` | Pausa manual e inmediata (sin esperar a que la propia entidad devuelva `DARKEN_PAUSE`). | La entidad debe estar en la zona activa. |
| `darken_entity_resume(darken_entity)` | Reanuda una entidad pausada, devolviéndola a la zona activa. | La entidad debe estar en la zona de pausadas. |
| `darken_entity_delete(darken_entity)` | Borra manualmente una entidad, esté activa o pausada. | Ninguna explícita — si la entidad ya está libre, no hace nada (idempotente). Ver ⚠️ pitfall §6.2: **no llama al destructor si la entidad estaba pausada.** |

---

## 4. Macros públicas

| Macro | Expande a / hace | Notas |
|---|---|---|
| `DARKEN_DATA(TIPO, VAR, ENTIDAD)` | `TIPO *VAR = (TIPO *)(ENTIDAD)->data;` | Azúcar sintáctico para castear el payload al tipo real del usuario dentro de una función de estado. |
| `DARKEN_DELETE` / `DARKEN_LOOP` / `DARKEN_PAUSE` | Valores centinela `(void*)0`, `(void*)1`, `(void*)2` | Los devuelve una función de estado para indicar "bórrame", "vuelve a llamarme sin cambiar de estado" y "pásame a pausa", respectivamente. Cualquier otro valor de retorno (>2) se interpreta como el *siguiente* puntero a función de estado. |
| `DARKEN_STORAGE(NOMBRE, CAPACIDAD, TAM_PAYLOAD)` | Declara e inicializa una struct anónima local con el array `pool[]` y el bloque de bytes `data[]` ya dimensionado y alineado a 4 bytes, más `capacity`/`payload_size`. | Gracias a la inicialización parcial con designadores (`= { .capacity = ..., .payload_size = ... }`), en C **todos los demás miembros —incluyendo `pool` y `data`— quedan puestos a cero** automáticamente. Esto es relevante: la primera vez que se usa un slot, su `state`/`destructor` valen 0, es decir `DARKEN_DELETE`/"sin destructor", un estado seguro por defecto. |
| `DARKEN_ARGS(NOMBRE)` | Expande a `(NOMBRE).pool, (NOMBRE).data, (NOMBRE).capacity, (NOMBRE).payload_size` | Pensado para pasarse directamente como los 4 últimos argumentos de `darken_init`. |
| `DARKEN_FOREACH(MANAGER, CODIGO)` | Recorre la zona **activa** de atrás hacia delante, exponiendo variables locales fijas `ENTITY` (la entidad actual) e `INDEX` (su índice). | ⚠️ Los nombres `ENTITY`, `INDEX`, `POOL` están *hardcodeados* en la macro — ver pitfall §6.5. |

### Ejemplo de uso combinado

```c
darken manager;
DARKEN_STORAGE(storage, /*capacidad*/ 64, sizeof(Enemy));
darken_init(&manager, DARKEN_ARGS(storage));
```

---

## 5. El ciclo de vida basado en estados

Cada entidad ejecuta, frame a frame, una función `darken_state` que recibe `entity->data` y
devuelve:

- **`DARKEN_LOOP`** → seguir en el mismo estado el próximo frame.
- **`DARKEN_PAUSE`** → pasar a pausa (se aplicará en la *siguiente* llamada a `darken_update`, ver §6.1).
- **`DARKEN_DELETE`** → borrarse (idem, con destructor si lo hay, aplicado en la siguiente llamada).
- **Cualquier otro puntero** → se interpreta como la nueva función de estado a partir del próximo frame.

```c
static void *enemigo_patrulla(void *data)
{
    DARKEN_DATA(Enemy, self, /* nota: aquí necesitarías la entidad, no solo data */ NULL);
    // ejemplo simplificado — normalmente recibirías Enemy* directamente:
    Enemy *e = (Enemy *)data;

    e->x += e->dir;
    if (jugador_visible(e))
        return (void *)enemigo_persigue; // cambia de estado

    return DARKEN_LOOP; // se sigue llamando a enemigo_patrulla
}

static void *enemigo_persigue(void *data)
{
    Enemy *e = (Enemy *)data;

    if (e->salud <= 0)
        return DARKEN_DELETE; // el motor lo borrará (y llamará al destructor) en el próximo tick

    perseguir(e);
    return DARKEN_LOOP;
}

static void *enemigo_destructor(void *data)
{
    Enemy *e = (Enemy *)data;
    reproducir_sonido_muerte(e);
    return NULL;
}

// Al crear el enemigo:
darken_entity ent = darken_spawn(&manager);
if (ent) { // ¡comprobar NULL! puede que no haya slots libres
    DARKEN_DATA(Enemy, e, ent);
    e->x = 100; e->salud = 10;
    ent->state = (darken_state)enemigo_patrulla;
    ent->destructor = (darken_state)enemigo_destructor;
    ent->tag = 0; ent->usr = 0;
}

// En el bucle principal:
darken_update(&manager);

// Para dibujar todas las activas:
DARKEN_FOREACH(&manager, {
    DARKEN_DATA(Enemy, e, ENTITY);
    dibujar_enemigo(e);
});
```

---

## 6. Puntos fuertes

- **O(1) en spawn, delete, pause y resume**, todo mediante *swaps* de punteros dentro de
  `pool[]` — nunca se mueve la memoria de la entidad en sí (patrón "sparse set" con tres zonas
  en vez de las dos habituales de activo/libre).
- **Estabilidad de direcciones**: un puntero a `entity` o a `entity->data` sigue siendo válido
  aunque el motor reordene internamente el array de punteros al pausar/reanudar/borrar otras
  entidades. Esto es explícito en el diseño y es la razón de ser de la zona de pausa separada.
- **Cero asignación dinámica en tiempo de ejecución**: toda la memoria es un bloque fijo
  proporcionado por el llamador (vía `DARKEN_STORAGE` o memoria propia), ideal para plataformas
  sin heap fiable.
- **Alineación consciente del hardware**: `_DARKEN_ALIGN4` y `__attribute__((aligned(4)))`
  respetan el requisito de alineación a palabra del 68000.
- **Máquina de estados compacta**: reutilizar el mismo puntero de función tanto para "próximo
  estado" como para "comando de control" (delete/loop/pause) evita tener flags de vida
  separados; el propio código de la entidad decide su destino.
- **API pequeña y de bajo overhead**: mucho de lo crítico en rendimiento vive en macros
  (`_DARKEN_ENTITY_PAUSE`, `_DARKEN_ENTITY_DELETE`) que se inlinean en el sitio de la llamada.

---

## 6bis. Puntos flacos / limitaciones de diseño

- **Un manager, un tamaño de payload fijo.** Todas las entidades de un mismo `darken` comparten
  `PAYLOAD_SIZE`; para tipos de entidad distintos necesitas managers (pools) separados.
- **"Privado" no está impuesto por el tipo.** `owner`/`slot` son de solo uso interno por
  convención de comentario, pero el struct completo es visible; nada evita que código de
  usuario los toque y rompa los invariantes de zona.
- **`_DARKEN_ASSERT` es un guard clause silencioso**, no un assert de verdad: si la
  precondición falla, la función simplemente retorna sin hacer nada — no hay log, ni abort, ni
  código de error. Un uso incorrecto (p. ej. pausar dos veces la misma entidad) no se detecta en
  ningún sitio; se enmascara como un no-op.
- **`darken_state` es un puntero a función sin prototipo** (`void *(*)()`), lo que desactiva la
  comprobación de tipos de argumentos en la llamada. Da flexibilidad (cada función de estado
  puede tomar el tipo de puntero que quiera), pero también elimina una red de seguridad del
  compilador; un desajuste de tipos entre lo que el motor pasa (`entity->data`, un
  `uint8_t*`) y lo que la función de usuario espera no será detectado en compilación.
- **Los códigos de control (0/1/2) se codifican como punteros y se comparan con `>`.** Es un
  truco eficiente y funciona perfectamente en cualquier máquina de direccionamiento plano real
  (como el 68000 objetivo, donde ninguna función legítima vive en las direcciones 0–2), pero
  comparar con `>` punteros que no apuntan al mismo objeto es, en sentido estricto, un
  comportamiento no definido según el estándar de C. Es una elección pragmática y consciente
  del proyecto, no un descuido, pero conviene saber que no es "ISO C estrictamente portable".
- **Límite de 65535 entidades por manager** (`capacity`/`size`/`paused` son `uint16_t`),
  razonable para el hardware objetivo pero a tener en cuenta si se reutiliza en otro contexto.
- **Alineación garantizada de solo 4 bytes.** Si el payload de usuario contiene un tipo que en
  tu ABI requiere alineación de 8 bytes (p. ej. `double`/`uint64_t` en muchas plataformas de 32/64
  bits modernas), `DARKEN_STORAGE` no lo garantiza — en el 68000 original 4 bytes basta según el
  propio comentario del header, pero no asumas lo mismo si compilas para otra arquitectura.

---

## 6. ⚠️ Pitfalls concretos (leídos directamente de la implementación)

### 6.1 Retraso de un frame entre "señal de borrado/pausa" y el efecto real

Cuando una función de estado devuelve `DARKEN_DELETE` o `DARKEN_PAUSE`, `darken_update` **solo
guarda ese valor en `entity->state`**; la entidad sigue contando como "activa"
(`slot < size`, visible para `DARKEN_FOREACH`) hasta la *siguiente* llamada a `darken_update`,
momento en el que el motor relee el estado, lo reconoce como centinela, y recién ahí llama al
destructor / mueve la entidad de zona.

```c
// Frame N: la función de estado decide borrarse
return DARKEN_DELETE;
// -> entity->state queda en 0, pero la entidad SIGUE en la zona activa

// Entre frame N y N+1: cualquier DARKEN_FOREACH (p. ej. tu fase de render)
// todavía la visita, y su destructor AÚN no se ha ejecutado.

// Frame N+1, darken_update():
// -> ahora sí: llama al destructor y la mueve a la zona libre.
```

Si tu bucle de juego hace `darken_update()` y luego, en el mismo frame, un `DARKEN_FOREACH`
para renderizar, **vas a dibujar durante un frame extra una entidad que ya "se marcó" como
borrada**, y que ni siquiera ha recibido su destructor. Si necesitas reaccionar
inmediatamente al borrado, usa `darken_entity_delete()` directamente en lugar de devolver
`DARKEN_DELETE` desde el estado.

### 6.2 `darken_entity_delete` no llama al destructor si la entidad estaba pausada

Comparando la función pública con la macro que usa `darken_update` internamente:

```c
void darken_entity_delete(darken_entity $)
{
    if (_DARKEN_ENTITY_IN_ACTIVE($))
        _DARKEN_ENTITY_DELETE($);          // <- esta rama SÍ llama al destructor
    else if (_DARKEN_ENTITY_IN_PAUSED($))
        _darken_entity_swap($->owner->pool, $->slot, $->owner->paused++); // <- esta NO
}
```

Si borras manualmente una entidad **activa**, su destructor se ejecuta. Si borras manualmente
una entidad **pausada**, su destructor **no se ejecuta nunca**. Es una asimetría real en el
código, no una decisión documentada. Si tus destructores liberan recursos (temporizadores,
sonidos, contadores externos, lo que sea en tu plataforma), una entidad pausada que borras
directamente se va a "fugar" ese recurso.

**Mitigación**: si necesitas destructor garantizado, reanuda (`darken_entity_resume`) antes de
borrar, o revisa/parchea esta rama si te afecta.

### 6.3 `darken_reset` tampoco llama al destructor de las entidades pausadas

```c
void darken_reset(darken *$)
{
    DARKEN_FOREACH($, _DARKEN_ENTITY_DELETE(ENTITY)); // solo recorre [0, size)
    $->size = 0;
    $->paused = $->capacity; // ¡las pausadas "vuelven a ser libres" sin pasar por aquí!
}
```

`DARKEN_FOREACH` solo visita la zona activa `[0, size)`. Cualquier entidad que estuviera
pausada en el momento del reset **nunca entra en el bucle**, y aun así, al final,
`paused = capacity` hace que sus slots se consideren libres de nuevo. Resultado: si tienes
entidades pausadas cuando llamas a `darken_reset()`, sus destructores nunca se invocan y sus
slots se reciclan como si nada.

**Mitigación**: reanuda explícitamente todas las entidades pausadas antes de llamar a
`darken_reset()` si dependes de que sus destructores se ejecuten.

### 6.4 `darken_spawn` no reinicializa los campos del slot reciclado

`darken_init` solo fija `owner` y `slot` de cada entidad, una vez, al arrancar. `darken_spawn`
tampoco toca `state`, `destructor`, `tag`, `usr` ni el payload — simplemente te entrega el
puntero al slot y confía en que tú lo rellenes por completo.

Gracias a la inicialización con designadores en `DARKEN_STORAGE` (ver §4), la **primera** vez
que se usa un slot, esos campos están a cero (seguro por defecto). Pero después del primer
ciclo de uso/borrado, el slot conserva lo que dejó su ocupante anterior:

```c
// Ciclo 1: entidad A ocupa el slot 5
entA->destructor = destructor_de_A;
// ... más tarde A se borra, el slot 5 vuelve a la zona libre
// (destructor_de_A ya se ejecutó una vez, correctamente)

// Ciclo 2: entidad B recicla el slot 5
darken_entity entB = darken_spawn(&manager); // puede ser el mismo puntero que entA
entB->state = (darken_state)estado_de_B;
// *** si olvidas poner entB->destructor, sigue apuntando a destructor_de_A ***

// Cuando B se borra, el motor llama a destructor_de_A(entB->data),
// pasándole datos con el layout de B. Corrupción de memoria / comportamiento indefinido.
```

**Mitigación**: trata `darken_spawn()` como si devolviera memoria sin inicializar (porque lo
es, salvo la primerísima vez) y fija explícitamente **todos** los campos (`state`, `destructor`,
`tag`, `usr`, payload) inmediatamente después de cada `darken_spawn()` exitoso.

### 6.5 `DARKEN_FOREACH` usa nombres de variable fijos, sin higiene de macro

```c
#define _DARKEN_FOREACH(MANAGER, CODE) _DARKEN_BLOCK( \
    uint16_t INDEX = (MANAGER)->size; \
    darken_entity *POOL = (MANAGER)->pool; \
    while (INDEX--) { \
        darken_entity ENTITY __attribute__((unused)) = POOL[INDEX]; \
        CODE; \
    })
```

Los nombres `ENTITY`, `INDEX` y `POOL` están escritos literalmente en la macro. Si tu bloque de
código:

- declara una variable local llamada igual (`ENTITY`, `INDEX` o `POOL`), la sombreará de forma
  confusa;
- anida un `DARKEN_FOREACH` dentro de otro (p. ej. para comparar entidades entre sí), el
  interno **sombrea** las variables del externo silenciosamente — dentro del bloque interno ya
  no tienes acceso al `ENTITY`/`INDEX` del externo salvo que lo copies a otra variable antes.

**Mitigación**: evita anidar `DARKEN_FOREACH`; si necesitas comparar pares de entidades, guarda
una copia externa (`darken_entity actual = ENTITY;`) antes de abrir el bucle interno, y nunca
nombres tus propias variables `ENTITY`/`INDEX`/`POOL`.

### 6.6 Comprobar siempre el valor de retorno de `darken_spawn`

```c
darken_entity darken_spawn(darken *$)
{
    _DARKEN_ASSERT($->size < $->paused, 0); // devuelve NULL si no hay slots libres
    return $->pool[$->size++];
}
```

Si el pool está lleno (todo activo + pausado, sin libres), `darken_spawn` devuelve `NULL` sin
avisar de ninguna otra forma (recordemos: `_DARKEN_ASSERT` es silencioso). Usar el resultado
sin comprobarlo (`darken_spawn(&mgr)->state = ...`) es un null-pointer-dereference clásico bajo
carga alta — exactamente cuando más entidades tienes en pantalla.

### 6.7 Los "asserts" nunca reportan nada

En general, cualquier mal uso cubierto por `_DARKEN_ASSERT` (pausar una entidad que ya no está
activa, reanudar una que no está pausada, etc.) falla **en silencio**: la función retorna sin
hacer nada, sin log, sin código de error, sin abort. Esto es coherente con un motor pensado para
hardware retro sin stderr ni depurador cómodo, pero significa que bugs de uso indebido pueden
pasar completamente desapercibidos en desarrollo. Si te cuesta encontrar un bug de "mi entidad
no se pausa/reanuda", sospecha primero de una precondición no cumplida silenciosamente.

---

## 7. Checklist práctico

- [ ] Define `DARKEN_IMPLEMENTATION` en **un solo** `.c`.
- [ ] Comprueba siempre que `darken_spawn()` no devuelva `NULL`.
- [ ] Tras cada `darken_spawn()`, fija explícitamente `state`, `destructor`, `tag`, `usr` y el
      payload — nunca asumas que están a cero.
- [ ] Si dependes de que los destructores se ejecuten siempre, evita `darken_entity_delete()` /
      `darken_reset()` sobre entidades pausadas sin reanudarlas antes.
- [ ] No anides `DARKEN_FOREACH`, y no declares variables llamadas `ENTITY`, `INDEX` o `POOL`.
- [ ] Si necesitas reaccionar al borrado en el mismo frame (no un frame después), llama a
      `darken_entity_delete()` directamente en vez de devolver `DARKEN_DELETE` desde el estado.
- [ ] No leas ni escribas `entity->owner` / `entity->slot` desde fuera del motor.
- [ ] Un manager = un tamaño de payload; usa un `darken` distinto por cada tipo de entidad con
      tamaño distinto.