# Darken 2.0 — DARKula ENgine Entity System

Darken es un sistema de entidades en C diseñado para entornos de recursos limitados. Utiliza extensiones de GNU C (`__attribute__`, *statement expressions*) y no realiza asignación dinámica de memoria en tiempo de ejecución. El almacenamiento se proporciona estáticamente en tiempo de compilación o de inicialización.

`` --> concretamente para **GCC/SGDK** y el procesador **Motorola 68000** (Sega Mega Drive / Genesis)``

---

## Tabla de contenidos

1. [Filosofía y características](#filosofía-y-características)
2. [Requisitos y plataforma](#requisitos-y-plataforma)
3. [Conceptos fundamentales](#conceptos-fundamentales)
   - [Entidad (`de_entity`)](#entidad-de_entity)
   - [Manager (`de_manager`)](#manager-de_manager)
   - [Sistema (`darksys`)](#sistema-darksys)
   - [Estado (`de_state`)](#estado-de_state)
4. [Layout de memoria](#layout-de-memoria)
   - [Entidad](#layout-de-entidad)
   - [Manager](#layout-del-manager)
   - [Sistema](#layout-del-sistema)
5. [API de entidad](#api-de-entidad)
6. [API de manager](#api-de-manager)
7. [API de sistema](#api-de-sistema)
8. [Macros públicas](#macros-públicas)
9. [Máquina de estados y transiciones](#máquina-de-estados-y-transiciones)
10. [Reglas de seguridad y garantías](#reglas-de-seguridad-y-garantías)
11. [Ejemplo de uso](#ejemplo-de-uso)
12. [Notas de implementación](#notas-de-implementación)

---

## Filosofía y características

- **Sin `malloc` en runtime**: el usuario proporciona todos los bloques de memoria.
- **Direcciones de entidad inmutables**: una entidad, una vez creada, nunca se mueve físicamente en memoria. Solo se reordenan los *punteros* dentro del array del manager.
- **Tres zonas lógicas**: activa, libre y pausada. Las entidades pausadas permanecen en direcciones de memoria estables, lo que permite que sistemas externos (`darksys`) mantengan punteros seguros a `entity->data`.
- **Iteración segura hacia atrás**: el manager recorre las entidades activas en orden inverso, permitiendo borrar, pausar o resumir la entidad actual sin invalidar el recorrido.
- **Sistemas de datos planos**: un `darksys` es un pool contiguo de grupos de punteros, sin overhead de estructuras anidadas.
  
```--> **Alineación longword (4 bytes)**: obligatoria para accesos word/long eficientes en el Motorola 68000.```

---

## Requisitos y plataforma

- **Compilador**: GCC con soporte para extensiones GNU C.
- **Dependencias**: únicamente `<stdint.h>`.
- **Inclusión**: incluir `darken.h`. Para obtener las definiciones de las funciones, definir `DARKEN_IMPLEMENTATION` antes del `#include` en **un único** archivo de compilación.
  
``` --> - **Entorno objetivo**: SGDK (Sega Genesis Development Kit) o cualquier toolchain basado en GCC para M68000.```

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
```

---

## Conceptos fundamentales

### Entidad (`de_entity`)

Una entidad es el objeto base gestionado por el sistema. Es un contenedor que almacena:

- Un **estado actual** (`state`): puntero a función o un valor de control.
- Un **destructor** opcional (`destructor`): función de limpieza invocada antes de la eliminación.
- Una referencia a su **manager** (`manager`).
- Su **índice** actual en el array del manager (`slot`).
- Una **etiqueta** definida por el usuario (`tag`).
- Un **bloque de datos de usuario** (`data[]`): *flexible array member* cuyo tamaño se fija al inicializar el manager.

La entidad nunca se desplaza en memoria una vez inicializada. Esto es la base de la seguridad de punteros externos.

### Manager (`de_manager`)

El manager es el contenedor de entidades. Mantiene un array de punteros (`items`) dividido en tres zonas lógicas contiguas:

| Zona | Rango de índices | Descripción |
|------|------------------|-------------|
| **Activa** | `[0, active)` | Entidades que se actualizan y recorren cada frame. |
| **Libre** | `[active, paused)` | Slots sin asignar. `de_manager_new()` consume desde el borde izquierdo de esta zona. |
| **Pausada** | `[paused, capacity)` | Entidades fuera del bucle de actualización. Sus direcciones físicas son estables. |

### Sistema (`darksys`)

Un sistema es un pool lineal de punteros agrupados en bloques de `params` elementos. Cada bloque representa los datos asociados a un ítem procesado (por ejemplo, una entidad).

```
Pool (params = 3):
[e0.a][e0.b][e0.c][e1.a][e1.b][e1.c][e2.a][e2.b][e2.c]...
```

Los sistemas permiten iterar de forma homogénea sobre conjuntos de punteros a datos de usuario, sin necesidad de acceder a la entidad completa.

### Estado (`de_state`)

Tipo de función: `void *(*de_state)(void *)`.

Recibe `entity->data` y debe retornar:
- Un puntero a otra función de estado (transición de estado).
- `DE_STATE_LOOP` — mantener el estado actual.
- `DE_STATE_PAUSE` — pausar la entidad después de esta actualización.
- `de_state_delete` — eliminar la entidad después de esta actualización.

> **Nota de portabilidad**: Darken representa tanto punteros a función como valores de control mediante `void *`. Esta es una decisión intencional para el target GCC/M68000 y **no** es una interfaz ISO C estrictamente portable.

---

## Layout de memoria

### Layout de entidad

Cada entidad reside en una dirección fija dentro del bloque de almacenamiento proporcionado al manager. El stride entre entidades consecutivas se calcula como:

```
stride = _DE_ENTITY_STRIDE(payload_size)
       = _DE_ALIGN4(sizeof(struct de_entity) + payload_size)
```

Estructura en memoria:

```
[  state  ][destructor][ manager  ][  slot  ][  tag  ][    data[0..N]    ]
  void *     void *     de_manager*  uint16_t  uint16_t   uint8_t[...]
```

### Layout del manager

El manager no almacena las entidades directamente, sino un array de punteros a ellas. Las entidades mismas viven en un bloque de memoria contiguo proporcionado por el usuario.

```
manager->items (array de punteros):
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                     │                                     │                                     │
│            ZONA ACTIVA              │             ZONA LIBRE              │           ZONA PAUSADA              │
│     [0] [1] ... [active-1]    │ [active] ... [paused-1] │   [paused] ... [capacity-1]   │
│                                     │                                     │                                     │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

- **Zona Activa**: iterable con `de_manager_foreach`. Se recorre de atrás hacia adelante.
- **Zona Libre**: fuente de nuevas entidades mediante `de_manager_new()`.
- **Zona Pausada**: entidades excluidas de la iteración y actualización. `de_manager_new()` **nunca** asigna slots de esta zona, por lo que las direcciones `entity->data` permanecen válidas y estables.

### Layout del sistema

```
system->pool (array de void*):
┌──────────────────────────────────────────────────────────────────────┐
│ g0.p0 │ g0.p1 │ ... │ g0.pN │ g1.p0 │ g1.p1 │ ... │ g1.pN │ ...      │
│       Grupo 0 (params elementos)    │        Grupo 1                 │
└──────────────────────────────────────────────────────────────────────┘
                        ↑
                     system->end
```

- `capacity` = número total de slots de puntero (`capacity_groups * params`).
- `size` = slots usados (siempre múltiplo de `params`).
- `params` = número de punteros por grupo.

---

## API de entidad

### `void *de_entity_exec(de_entity e)`

Ejecuta la función de estado actual de la entidad **sin** almacenar su valor de retorno en `e->state`.

- Si el estado no es una función activa (`<= DE_STATE_PAUSE`), retorna `de_state_delete` (0).
- Útil para ejecución manual o inicialización.

### `void *de_entity_update(de_entity e)`

Ejecuta el estado activo y almacena la transición retornada en `e->state`.

- `DE_STATE_LOOP`: no modifica `e->state`.
- `DE_STATE_PAUSE` / `de_state_delete`: se almacenan en `e->state` como transiciones pendientes; serán procesadas por la siguiente llamada a `de_manager_update()`.
- Si la entidad no está activa, no modifica el estado y lo retorna tal cual.

### `void de_entity_pause(de_entity e)`

Mueve una entidad activa a la zona pausada.

1. Reduce `active` en 1.
2. Si la entidad no está en el borde derecho de la zona activa, la intercambia con la entidad que ocupa ese borde (mediante `de_entity_swap`).
3. Reduce `paused` en 1 y coloca la entidad en esa posición.

La entidad conserva su dirección física; solo cambia de zona lógica. Es seguro mantener punteros a `e->data` mientras permanezca pausada.

> No-op si la entidad no está activa.

### `void de_entity_resume(de_entity e)`

Mueve una entidad pausada de vuelta a la zona activa. Operación inversa de `de_entity_pause`.

1. Si la entidad no está en el borde izquierdo de la zona pausada, la intercambia con la que ocupa ese borde.
2. Incrementa `paused`.
3. Coloca la entidad en `active` e incrementa `active`.

> No-op si la entidad no está pausada.

### `void de_entity_delete(de_entity e)`

Elimina una entidad, esté activa o pausada.

1. Si la entidad ya está en la zona libre, no hace nada.
2. Si existe `e->destructor`, lo invoca pasándole `e->data`. **El valor de retorno del destructor se ignora; un destructor no puede cancelar la eliminación**.
3. Reorganiza el array:
   - Si estaba **pausada**: intercambia con el borde izquierdo de la zona pausada (si es necesario) e incrementa `paused`.
   - Si estaba **activa**: intercambia con el borde derecho de la zona activa (si es necesario) y decrementa `active`.
4. El slot liberado pasa a formar parte de la zona libre, disponible para `de_manager_new()`.

### `void de_entity_move_front(de_entity e)`

Mueve una entidad activa al índice más alto de la zona activa (`active - 1`).

Como `de_manager_update()` itera hacia atrás, esto hace que la entidad se ejecute **antes** (más temprano) en la siguiente actualización.

> No-op si la entidad no está activa o ya está en el frente.

### `void de_entity_move_back(de_entity e)`

Mueve una entidad activa al índice 0 de la zona activa.

Como la iteración es hacia atrás, esto hace que la entidad se ejecute **después** (más tarde) en la siguiente actualización.

> No-op si la entidad no está activa o ya está al fondo.

---

## API de manager

### `void de_manager_init(de_manager m, de_entity *items, void *storage, uint16_t capacity, uint16_t payload_size)`

Inicializa un manager con almacenamiento proporcionado por el usuario.

- `items`: array pre-asignado de `de_entity *` con al menos `capacity` elementos.
- `storage`: bloque de memoria contigua para las entidades. Debe tener al menos `capacity * _DE_ENTITY_STRIDE(payload_size)` bytes.
- `capacity`: número máximo de entidades.
- `payload_size`: tamaño en bytes del campo `data[]` de cada entidad.

Inicializa todas las entidades con:
- `manager = m`
- `slot = índice correspondiente`
- `active = 0`
- `paused = capacity` (todo es zona libre al inicio).

### `de_entity de_manager_new(de_manager m)`

Crea una nueva entidad tomando un slot de la zona libre.

- Retorna `NULL` (0) si no hay slots libres (`active >= paused`).
- Inicializa los campos de control:
  - `state = de_state_delete`
  - `destructor = NULL`
  - `tag = 0`
- **No inicializa** `data[]`. El usuario debe poblar el payload antes de activar la entidad.

### `void de_manager_update(de_manager m)`

Actualiza todas las entidades de la zona activa. Las entidades pausadas **nunca** son tocadas.

Algoritmo:
1. Itera **hacia atrás** desde `active - 1` hasta `0`.
2. Para cada entidad:
   - Si `state` es una función activa (`> DE_STATE_PAUSE`), la ejecuta. Si el retorno no es `DE_STATE_LOOP`, lo almacena en `e->state`.
   - Si `state == DE_STATE_PAUSE`, invoca `de_entity_pause(e)`.
   - Si `state == de_state_delete`, invoca `de_entity_delete(e)`.

> **Seguridad del bucle**: tanto `de_entity_pause` como `de_entity_delete` reducen la zona activa por su borde derecho, que es exactamente donde comienza esta iteración inversa. Por tanto, una entidad relocalizada durante el bucle ya había sido visitada; no es necesario releer `active` en cada iteración.

### `void de_manager_reset(de_manager m)`

Elimina **todas** las entidades activas y pausadas, restaurando el manager a su estado inicial vacío.

- Invoca `de_entity_delete(ENTITY)` para cada entidad activa (via `de_manager_foreach`).
- Restaura `active = 0` y `paused = capacity`.
- Los destructores de todas las entidades son llamados.

---

## API de sistema

### `void darksys_init(darksys s, void **storage, uint16_t capacity_groups, uint16_t params)`

Inicializa un sistema.

- `storage`: array pre-asignado de `void *` con al menos `capacity_groups * params` elementos.
- `capacity_groups`: número máximo de grupos que el pool puede almacenar.
- `params`: número de punteros por grupo.

### `uint16_t darksys_remove(darksys s, void *first)`

Elimina un grupo del sistema buscando coincidencia con su **primer puntero** (`first`).

- Si encuentra el grupo, mueve el último grupo del pool a la posición eliminada para mantener la contigüidad (compacción).
- Retorna `1` si se eliminó, `0` si no se encontró.

---

## Macros públicas

### Almacenamiento estático

#### `DE_MANAGER_STORAGE(name, capacity, payload_size)`

Declara e inicializa una estructura de almacenamiento estático para un manager, incluyendo:
- Un array de estructuras `de_entity`.
- Un buffer de datos alineado a 4 bytes.
- Campos `capacity` y `payload_size` para uso con `de_manager_args`.

**Ejemplo:**
```c
struct de_manager enemies;
DE_MANAGER_STORAGE(enemies_storage, 8, sizeof(struct MyComponent));
de_manager_init(&enemies, de_manager_args(enemies_storage));
```

#### `de_manager_args(name)`

Expande a los cuatro argumentos requeridos por `de_manager_init` a partir de una estructura `DE_MANAGER_STORAGE`.

#### `DARKSYS_STORAGE(name, capacity, params)`

Declara e inicializa una estructura de almacenamiento estático para un sistema.

**Ejemplo:**
```c
struct darksys sys;
DARKSYS_STORAGE(storage, 32, 3);
darksys_init(&sys, darksys_args(storage));
```

#### `darksys_args(name)`

Expande a los argumentos requeridos por `darksys_init`.

### Iteración

#### `de_manager_foreach(m, code)`

Itera la zona activa del manager en **orden inverso** (de `active - 1` a `0`).

Dentro del bloque `code` están disponibles las siguientes variables:
- `INDEX`: índice actual en el array (tipo `uint16_t`).
- `ITEMS`: puntero al array `m->items`.
- `ENTITY`: puntero a la entidad actual (`de_entity`).

**Ejemplo:**
```c
de_manager_foreach(my_manager, {
    if (ENTITY->tag == PLAYER_TAG)
        update_player(ENTITY);
});
```

> **Regla de seguridad**: borrar, pausar o resumir `ENTITY` (la entidad actualmente visitada) es seguro. Mutar una **entidad diferente** durante el bucle **no está garantizado** como seguro.

### Sistema — Añadir e iterar

#### `darksys_add(sys, ...)`

Añade un grupo de punteros al sistema. El primer argumento siempre es el puntero al sistema. Acepta de **1 a 5** punteros de datos adicionales.

Retorna `1` si tuvo éxito, `0` si el sistema está lleno.

**Ejemplo:**
```c
darksys_add(physics_system, entity_ptr, &velocity, &position);
```

#### `DARKSYS_FOREACH(sys, ...)`

Itera sobre los grupos del sistema. El primer argumento es el sistema. Acepta de **0 a 5** variables de salida (que recibirán los punteros del grupo actual) seguidas de un bloque de código.

**Ejemplo:**
```c
DARKSYS_FOREACH(physics_system, entity, velocity, position, {
    update_physics(entity, velocity, position);
});
```

#### `DARKSYS_ITERATOR(name, ...)`

Genera una función con la firma `void *name(darksys system)` que ejecuta un `DARKSYS_FOREACH` interno y retorna `DE_STATE_LOOP`.

Esto permite usar la función generada como un estado de entidad (`de_state`).

**Ejemplo:**
```c
DARKSYS_ITERATOR(physics_update, velocity, position, {
    entity->x += velocity->dx;
    entity->y += velocity->dy;
});
// physics_update puede asignarse a e->state
```

```--> **Nota**: la función generada recibe `darksys `, por lo que **no** es compatible con la firma `de_state(void *)` bajo ISO C estricto, aunque funciona en el target GNU C/M68000.```

### Constantes de estado

| Macro | Valor | Significado |
|-------|-------|-------------|
| `de_state_delete` | `(void *)0` | Eliminar la entidad. |
| `DE_STATE_LOOP` | `(void *)1` | Mantener estado actual. |
| `DE_STATE_PAUSE` | `(void *)2` | Pausar la entidad. |

Macros de comprobación:
- `_DE_STATE_IS_DELETED(s)` — ¿es 0?
- `_DE_STATE_IS_LOOP(s)` — ¿es 1?
- `_DE_STATE_IS_PAUSED(s)` — ¿es 2?
- `_DE_STATE_IS_ACTIVE(s)` — ¿es > 2? (es decir, un puntero de función válido).

### Utilidades

#### `_DE_ENTITY_STRIDE(payload_size)`

Calcula el stride entre entidades consecutivas, incluyendo alineación a 4 bytes.

```c
#define _DE_ENTITY_STRIDE(PAYLOAD) _DE_ALIGN4(sizeof(struct de_entity) + (PAYLOAD))
```

---

## Máquina de estados y transiciones

Darken implementa una máquina de estados implícita mediante punteros a función y valores mágicos de control.

### Ciclo de vida típico

1. **Creación**: `de_manager_new()` crea la entidad con `state = de_state_delete`. La entidad no se actualizará hasta que se le asigne un estado activo (`> DE_STATE_PAUSE`).
2. **Activación**: el usuario asigna un puntero a función a `e->state`.
3. **Actualización**: `de_manager_update()` ejecuta el estado. La función retorna:
   - **Otro puntero a función**: la entidad transiciona a ese nuevo estado.
   - **`DE_STATE_LOOP`**: se mantiene el estado actual (no se escribe en `e->state`).
   - **`DE_STATE_PAUSE`**: la entidad se mueve a la zona pausada al finalizar la actualización.
   - **`de_state_delete`**: la entidad se elimina al finalizar la actualización.
4. **Pausa**: la entidad permanece en memoria estable, fuera del bucle de actualización. Los sistemas pueden conservar punteros a `e->data`.
5. **Reanudación**: `de_entity_resume()` la devuelve a la zona activa.
6. **Eliminación**: `de_entity_delete()` libera el slot. Si hay destructor, se ejecuta primero.

### Transiciones manuales vs. automáticas

- **Automática**: el valor retornado por la función de estado durante `de_manager_update()`.
- **Manual**: el usuario puede escribir directamente `e->state = DE_STATE_PAUSE` (o `DELETE`). En la siguiente actualización del manager, se detectará y aplicará.

`de_entity_update()` (función de entidad individual) ejecuta el estado y almacena el resultado, pero **no** aplica las transiciones de pausa/eliminación inmediatamente; solo las deja pendientes en `e->state` para que `de_manager_update()` las procese.

---

## Reglas de seguridad y garantías

### Garantías de memoria

- Una entidad nunca cambia de dirección física después de `de_manager_init()`.
- `de_manager_new()` nunca reutiliza un slot de la zona pausada.
- Por tanto, cualquier puntero a `entity->data` es válido mientras la entidad esté pausada o activa, y solo se invalida cuando la entidad es eliminada (no cuando es pausada).

### Garantías de iteración

- `de_manager_foreach` y `de_manager_update` recorren hacia atrás.
- Es **seguro** borrar, pausar o resumir la entidad actual (`ENTITY`) dentro del cuerpo del bucle.
- No está garantizado que sea seguro modificar (borrar/pausar/resumir) una **entidad diferente** a la actual durante la iteración.

### Garantías de eliminación

- Un destructor, si existe, siempre se ejecuta antes de que la entidad sea liberada.
- Un destructor **no puede evitar** la eliminación. Su valor de retorno se ignora por completo.

### Garantías de alineación

- Todos los cálculos de stride usan alineación a 4 bytes (`_DE_ALIGN4`).
- El macro `DE_MANAGER_STORAGE` alinea el bloque de datos con `__attribute__((aligned(4)))`.
- Esto asegura que accesos word y longword en el M68000 sean siempre seguros y eficientes.

---

## Ejemplo de uso

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"

/* Datos de usuario para una entidad de jugador */
struct player_data {
    int x, y;
    int speed;
};

/* Estado: mover jugador */
void *player_move(struct player_data *data) {
    data->>x += data->>speed;

    if (data->>x > 320)
        return de_state_delete;  /* fuera de pantalla, eliminar */

    return DE_STATE_LOOP;
}

int main(void) {
    struct de_manager mgr;
    de_entity  ent;

    /* 1. Declarar e inicializar almacenamiento estático */
    DE_MANAGER_STORAGE(storage, 16, sizeof(struct player_data));
    de_manager_init(&mgr, de_manager_args(storage));

    /* 2. Crear una entidad */
    ent = de_manager_new(&mgr);
    if (!ent) return 1;

    /* 3. Configurar datos y estado */
    struct player_data *pd = (struct player_data *)ent->data;
    pd->x = 0;
    pd->y = 100;
    pd->speed = 2;
    ent->state = player_move;
    ent->tag   = 1;  /* TAG_PLAYER */

    /* 4. Bucle de juego simplificado */
    for (int frame = 0; frame < 200; ++frame) {
        de_manager_update(&mgr);
    }

    /* 5. Limpieza total */
    de_manager_reset(&mgr);
    return 0;
}
```

### Uso de sistemas

```c
struct darksys sys;
struct vec2 { int x, y; };
struct vec2 positions[8];
struct vec2 velocities[8];

DARKSYS_STORAGE(sys_storage, 8, 2);
darksys_init(&sys, darksys_args(sys_storage));

/* Registrar pares de punteros */
for (int i = 0; i < 8; ++i) {
    darksys_add(&sys, &positions[i], &velocities[i]);
}

/* Iterar y actualizar */
void *system_function(darksys sys) {
    DARKSYS_FOREACH(&sys,
        strurct vec2 *pos,
        strurct vec2 *vel,
        {
            pos->x += vel->x;
            pos->y += vel->y;
        }
    );
}

/* Eliminar un grupo buscando por su primer puntero */
darksys_remove(&sys, &positions[3]);
```

---

## Notas de implementación

- **Convención de nombres**: todo símbolo público utiliza el prefijo `de_` (funciones/tipos) o `DE_` (macros/constantes). Los símbolos internos utilizan `_de_` / `_DE_`.
- **Macros variádicas**: `_DE_ADD_NARGS` e `_DE_FOREACH_NARGS` utilizan el truco de conteo de argumentos mediante expansión de macro para soportar sobrecarga de aridad en `darksys_add`, `DARKSYS_FOREACH` y `DARKSYS_ITERATOR`.
- **Statement expressions**: `_darksys_add` usa la sintaxis `({ ... })` de GNU C para poder retornar un valor desde una macro compleja.
- **Sin gestión de errores de memoria**: si el almacenamiento proporcionado es insuficiente, el comportamiento es indefinido. El usuario es responsable de calcular correctamente los tamaños.
- **Capacidad 16-bit**: todos los contadores (`capacity`, `active`, `paused`, `size`) son `uint16_t` para optimizar el rendimiento en el Motorola 68000.
