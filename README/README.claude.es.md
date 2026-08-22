# Darken — DARKula ENgine 2.0

**Sistema de entidades de cabecera única, en C, sin asignación dinámica de memoria en tiempo de ejecución, pensado para GCC/SGDK y el Motorola 68000 (Sega Mega Drive / Genesis).**

Darken no es un ECS de archetypes. Es un gestor de entidades de capacidad fija sobre memoria proporcionada por el usuario, más un pool de punteros compacto (`darksys`) para procesar datos en bloque. Toda la memoria se reserva en tiempo de compilación o de inicialización; Darken nunca llama a `malloc`, `free` ni `calloc`.

> Este documento describe el comportamiento **real** del código de `darken.h`, verificado leyendo cada función línea a línea y comprobando con casos de prueba compilados los puntos que resultaban ambiguos (marcados como tal en la sección [18](#18-peculiaridades-y-advertencias-importantes)).

---

## Tabla de contenidos

1. [Visión general y filosofía](#1-visión-general-y-filosofía)
2. [Requisitos y plataforma](#2-requisitos-y-plataforma)
3. [Instalación (cabecera única)](#3-instalación-cabecera-única)
4. [Convención de nombres](#4-convención-de-nombres)
5. [Conceptos fundamentales](#5-conceptos-fundamentales)
6. [Layout de memoria](#6-layout-de-memoria)
7. [Las tres zonas del manager](#7-las-tres-zonas-del-manager)
8. [API de entidad](#8-api-de-entidad)
9. [API de manager](#9-api-de-manager)
10. [API de sistema](#10-api-de-sistema)
11. [Macros públicas](#11-macros-públicas)
12. [Máquina de estados](#12-máquina-de-estados)
13. [Orden de actualización](#13-orden-de-actualización)
14. [Complejidad](#14-complejidad)
15. [Ejemplo completo](#15-ejemplo-completo)
16. [Reglas de seguridad](#16-reglas-de-seguridad)
17. [Invariantes internos](#17-invariantes-internos)
18. [Peculiaridades y advertencias importantes](#18-peculiaridades-y-advertencias-importantes)
19. [Qué es y qué no es Darken](#19-qué-es-y-qué-no-es-darken)
20. [Referencia rápida de la API](#20-referencia-rápida-de-la-api)

---

## 1. Visión general y filosofía

- **Sin `malloc` en runtime.** Toda la memoria la reserva el usuario, de forma estática o en el heap una única vez al arrancar.
- **Direcciones de entidad inmutables.** Una entidad, una vez colocada por `de_manager_init()`, nunca cambia de dirección física. Lo único que se reordena es el *puntero* a ella dentro de `manager->pool[]`.
- **Tres zonas lógicas en un único array de punteros:** activa, libre y pausada.
- **Las entidades pausadas conservan su dirección.** `de_manager_new()` nunca recicla un slot de la zona pausada, así que un puntero externo a `entity->data` sigue siendo válido mientras la entidad esté pausada o activa.
- **Iteración activa hacia atrás y segura para auto-modificación.** El manager recorre la zona activa en orden descendente, lo que permite borrar, pausar o reanudar la entidad *que se está visitando* sin invalidar el recorrido.
- **`darksys` es un pool plano de punteros**, agrupados de `params` en `params`, sin relación estructural con las entidades salvo la que el propio código de usuario decida darle.
- **Alineación a 4 bytes (longword)** en el stride de entidad y en el bloque de datos, orientada al Motorola 68000.

---

## 2. Requisitos y plataforma

- Compilador **GCC** (o compatible) con extensiones GNU C: el código usa `__attribute__((aligned(4)))` y *statement expressions* `({ ... })` dentro de `darksys_add`.
- Única dependencia: `<stdint.h>`.
- Objetivo declarado: **SGDK** (Sega Genesis Development Kit) sobre Motorola 68000, aunque el header compila igualmente en cualquier plataforma con GCC (se ha verificado en x86_64/Linux para este documento).
- El tipo `de_state` representa tanto punteros a función como valores de control mediante `void *`, y varias comparaciones de puntero (`S > (de_state)2`) no son estrictamente ISO C portables. Es una decisión de diseño consciente para el ABI de destino, no un descuido.

---

## 3. Instalación (cabecera única)

Darken sigue el patrón *single-header* al estilo STB. En **un único** archivo `.c`:

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
```

En el resto de archivos, simplemente:

```c
#include "darken.h"
```

No definir `DARKEN_IMPLEMENTATION` en más de una unidad de traducción (produciría símbolos duplicados en el enlazado).

---

## 4. Convención de nombres

| Prefijo | Significado                                                      |
| ------- | ---------------------------------------------------------------- |
| `de_*`  | Funciones y tipos públicos                                       |
| `DE_*`  | Macros públicas                                                  |
| `_de_*` | Funciones internas (uso interno del propio header)               |
| `_DE_*` | Macros internas, implementan la aridad variable de `DARKSYS_*` |

Los símbolos `_de_*` / `_DE_*` no forman parte de la API estable y no deben usarse directamente desde código de aplicación (ver [§18.3](#183-de_entity_swap-no-es-pública)).

---

## 5. Conceptos fundamentales

### 5.1 Entidad (`de_entity`)

```c
struct de_entity
{
    de_state  state;
    de_state  destructor;
    de_manager manager;
    uint16_t  slot;
    uint16_t  tag;
    uint8_t   data[];   /* flexible array member */
};

typedef struct de_entity *de_entity;   /* de_entity ES un puntero */
```

Nótese que `de_entity` ya es un tipo puntero (`typedef struct de_entity *de_entity;`), no una estructura por valor. Toda la API la recibe y devuelve por valor de puntero.

`data[]` es el payload específico de la aplicación, cuyo tamaño se fija una sola vez al inicializar el manager (`de_manager_init`), no por entidad individual.

### 5.2 Manager (`de_manager`)

```c
struct de_manager
{
    de_entity *pool;
    uint16_t  capacity;
    uint16_t  active;
    uint16_t  paused;
};
```

El manager no almacena las entidades: almacena un array de **punteros** a ellas (`pool`), dividido en tres zonas contiguas (ver [§7](#7-las-tres-zonas-del-manager)). Las entidades en sí viven en un bloque de bytes contiguo separado, proporcionado por el usuario.

### 5.3 Sistema (`darksys`)

```c
struct darksys
{
    void **pool;
    void **end;
    uint16_t capacity;
    uint16_t size;
    uint16_t params;
};
```

Un pool lineal de punteros `void*`, organizado en grupos de `params` elementos cada uno:

```text
Pool (params = 2):
[e0.a][e0.b][e1.a][e1.b][e2.a][e2.b] ...
```

`capacity` y `size` se expresan en **slots de puntero**, no en grupos: un sistema con 10 grupos de 3 parámetros tiene `capacity == 30`. El número de grupos ocupados es `size / params`. `end` apunta siempre un elemento más allá del último puntero usado.

Un `darksys` **no es propietario** de los objetos a los que apunta; solo guarda punteros. La vida de esos objetos es responsabilidad del código que los añadió.

### 5.4 Estado (`de_state`)

```c
typedef void *(*de_state)(void *);
```

Un callback de estado recibe `entity->data` (no la entidad completa) y devuelve, o bien otro puntero a función de estado, o bien uno de tres valores de control reservados:

| Macro             | Valor      | Significado               |
| ----------------- | ---------- | ------------------------- |
| `de_state_delete` | `(void*)0` | Solicitar borrado         |
| `DE_STATE_LOOP`   | `(void*)1` | Mantener el estado actual |
| `DE_STATE_PAUSE`  | `(void*)2` | Solicitar pausa           |

```c
#define _DE_STATE_IS_DELETED(S) ((S) == ((de_state)0))
#define _DE_STATE_IS_LOOP(S)    ((S) == ((de_state)1))
#define _DE_STATE_IS_PAUSED(S)  ((S) == ((de_state)2))
#define _DE_STATE_IS_ACTIVE(S)  ((S) >  ((de_state)2))
```

Cualquier valor de puntero mayor que `2` se considera un callback ejecutable. Esto asume que el enlazador nunca coloca código en las direcciones `0`–`2`, cierto en la práctica en el ABI de destino pero no garantizado por ISO C.

> **Nota de tipado:** aunque `de_state` se declara como `void *(*)(void *)`, es habitual en el propio código de Darken escribir los callbacks tomando directamente el tipo del payload (`struct MiComponente *`) en vez de `void *`, y asignarlos a `entity->state` sin *cast* explícito. Compila y funciona en el ABI de destino (GCC/68000), pero no es válido bajo ISO C estricto — es la misma flexibilidad ya asumida para `DE_STATE_*` y para las funciones generadas por `DARKSYS_ITERATOR` (ver [§10.3](#103-darksys_iterator)).

---

## 6. Layout de memoria

### 6.1 Entidad y stride

```c
#define _DE_ENTITY_STRIDE(PAYLOAD) _DE_ALIGN4(sizeof(struct de_entity) + (PAYLOAD))
#define _DE_ALIGN4(X) (((X) + 3U) & ~3U)
```

```text
[ state ][ destructor ][ manager ][ slot ][ tag ][ data[0..PAYLOAD) ][ padding ]
 void*     void*         de_manager* uint16 uint16   uint8_t[...]
```

El *stride* entre entidades consecutivas se calcula **una sola vez**, en `de_manager_init()`, y se reutiliza para precomputar todas las direcciones. Darken nunca recalcula la dirección de una entidad durante su operación normal.

El redondeo a 4 bytes se aplica al **stride completo**, no solo al inicio del bloque: cada entidad empieza en un múltiplo de 4 aunque `PAYLOAD` no lo sea. El 68000 solo exige alineación de palabra (2 bytes) para accesos *word*/*long*; el límite de 4 bytes es una decisión de diseño más estricta para mantener strides regulares y predecibles, no un mínimo impuesto por la CPU.

### 6.2 Manager: `pool[]` frente a almacenamiento de entidades

`DE_MANAGER_STORAGE` reserva **dos bloques separados**:

```c
#define DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)                                         \
    struct {                                                                                     \
        de_entity entities[(CAPACITY)];                                                          \
        uint8_t   data[(CAPACITY) * _DE_ENTITY_STRIDE((PAYLOAD_SIZE))] __attribute__((aligned(4))); \
        uint16_t  capacity;                                                                       \
        uint16_t  payload_size;                                                                   \
    } NAME = { .capacity = (CAPACITY), .payload_size = (PAYLOAD_SIZE) }
```

- `entities[CAPACITY]` — pese al nombre, **no** son las entidades: es el array de **punteros** (`de_entity` ya es puntero) que se pasa a `de_manager_init()` como parámetro `pool`.
- `data[...]` — el bloque de bytes contiguo donde viven físicamente las entidades, alineado a 4 bytes.

```c
#define de_manager_args(NAME) \
    (NAME).entities, (NAME).data, (NAME).capacity, (NAME).payload_size
```

que encaja exactamente con la firma:

```c
void de_manager_init(de_manager, de_entity *pool, void *storage,
                      uint16_t capacity, uint16_t payload_size);
```

El objeto generado por `DE_MANAGER_STORAGE` debe permanecer vivo y en la misma dirección durante toda la vida del manager: Darken no copia ni reserva almacenamiento sustituto.

### 6.3 Sistema: el pool

```c
#define DARKSYS_STORAGE(NAME, CAPACITY, PARAMS) \
    struct { void *pool[(CAPACITY) * (PARAMS)]; uint16_t capacity; uint16_t params; } \
    NAME = { .capacity = (CAPACITY), .params = (PARAMS) }
```

Aquí `CAPACITY` es número de **grupos**, no de punteros: `DARKSYS_STORAGE(s, 32, 3)` reserva `32 × 3 = 96` slots de puntero.

---

## 7. Las tres zonas del manager

`manager->pool[]` se organiza en tres zonas lógicas contiguas y **sin gaps**:

```text
índice:    0                    active           paused          capacity
           │                          │                       │                   │
           ▼                          ▼                       ▼                   ▼
           ┌──────────────────────────┬───────────────────────┬───────────────────┐
           │          ACTIVA          │         LIBRE          │       PAUSADA      │
           │   [0, active)      │ [active,         │ [paused,     │
           │                          │  paused)         │  capacity)         │
           └──────────────────────────┴───────────────────────┴───────────────────┘
```

- **Activa** — entidades procesadas por `de_manager_update()` y visitadas por `de_manager_foreach`, en orden descendente.
- **Libre** — slots de puntero sin entidad "en uso" desde el punto de vista lógico. `de_manager_new()` toma siempre `pool[active]`, el primer slot libre.
- **Pausada** — entidades vivas pero excluidas del bucle de actualización y de `de_manager_foreach`. `de_manager_new()` **nunca** asigna slots de esta zona: es justamente lo que garantiza que la dirección de una entidad pausada (y por tanto de `entity->data`) permanezca estable mientras siga pausada.

`entity->slot` es el índice **actual** de la entidad dentro de `pool[]`; no es un offset dentro del bloque de bytes de almacenamiento. Cuando dos entidades se intercambian de zona, sus direcciones físicas no se mueven: solo cambian `slot` y la posición dentro de `pool[]`.

---

## 8. API de entidad

```c
void *de_entity_exec(de_entity);
void *de_entity_update(de_entity);
void  de_entity_pause(de_entity);
void  de_entity_resume(de_entity);
void  de_entity_delete(de_entity);
void  de_entity_move_front(de_entity);
void  de_entity_move_back(de_entity);
```

### `void *de_entity_exec(de_entity e)`

```c
void *de_entity_exec(de_entity $) {
    de_state s = $->state;
    if (!_DE_STATE_IS_ACTIVE(s)) return 0;
    return s($->data);
}
```

Ejecuta el estado actual **sin** escribir nada en `e->state`. Si el estado no es un callback ejecutable (es `DELETE`, `LOOP` o `PAUSE`), devuelve `0` (`de_state_delete`) sin llamar a nada. Es puramente de lectura respecto a `entity->state`; el único efecto secundario posible es el que produzca el propio callback sobre `entity->data`.

### `void *de_entity_update(de_entity e)`

```c
void *de_entity_update(de_entity $) {
    de_state s = de_entity_exec($);
    if (!_DE_STATE_IS_LOOP(s)) $->state = s;
    return s;
}
```

Ejecuta el estado activo y guarda la transición devuelta, salvo que sea `DE_STATE_LOOP`. Pensada para avanzar manualmente una entidad **fuera** del bucle del manager (sin la lógica de pausa/borrado automática de `de_manager_update`).

> ⚠️ Ver [§18.1](#181-de_entity_update-sobre-una-entidad-inactiva) — llamarla sobre una entidad que **no** tiene actualmente un callback activo fuerza su estado a `de_state_delete`, no lo deja como estaba.

### `void de_entity_pause(de_entity e)`

No-op si la entidad no está en la zona activa. Si lo está: reduce `active` en 1 (rellenando el hueco con la última entidad activa si procede), reduce `paused` en 1 y coloca la entidad en ese nuevo límite. La entidad conserva su dirección física.

### `void de_entity_resume(de_entity e)`

No-op si la entidad no está en la zona pausada. Operación inversa a `de_entity_pause`: libera el borde izquierdo de la zona pausada, avanza `paused`, e inserta la entidad en el borde de la zona activa.

### `void de_entity_delete(de_entity e)`

No-op si la entidad ya está en la zona libre. En caso contrario:

1. Si existe `e->destructor`, se invoca con `e->data`. **El valor de retorno del destructor se ignora por completo — no puede cancelar el borrado.**
2. Si estaba pausada: se intercambia (si hace falta) con el primer elemento de la zona pausada y se incrementa `paused`.
3. Si estaba activa: se intercambia (si hace falta) con el último elemento de la zona activa y se decrementa `active`.

En ambos casos el slot liberado pasa a la zona libre en O(1), sin mover bytes de ninguna entidad. Tras borrar, el puntero `de_entity` debe tratarse como inválido: su almacenamiento puede reaparecer en la próxima llamada a `de_manager_new()`.

### `void de_entity_move_front(de_entity e)` / `void de_entity_move_back(de_entity e)`

Ambas son no-op si la entidad no está activa, o si ya ocupa la posición destino. Como el manager recorre la zona activa **hacia atrás**, mover una entidad al índice más alto (`move_front`) hace que se ejecute **antes** en la próxima actualización, y moverla al índice `0` (`move_back`) hace que se ejecute **después**. Ambas son simples intercambios de puntero, O(1).

---

## 9. API de manager

```c
void      de_manager_init(de_manager, de_entity *, void *, uint16_t, uint16_t);
de_entity de_manager_new(de_manager);
void      de_manager_update(de_manager);
void      de_manager_reset(de_manager);
```

### `de_manager_init(m, pool, storage, capacity, payload_size)`

Inicializa `pool`, `capacity`, pone `active = 0` y `paused = capacity` (todo empieza libre). Calcula el stride una vez y recorre `storage` en pasos de ese tamaño, rellenando `pool[i]` con la dirección de cada entidad y fijando `manager` y `slot` para cada una. **No** toca `state`, `destructor`, `tag` ni `data` — eso lo hace `de_manager_new()`.

### `de_entity de_manager_new(de_manager m)`

Devuelve `NULL` si la zona libre está vacía (`active >= paused`). Si hay hueco, toma `pool[active]`, incrementa `active`, y **reinicializa** los campos de control:

```c
e->state      = de_state_delete;
e->destructor = 0;
e->tag        = 0;
```

`data[]` **no se inicializa**: si se necesita en un estado conocido, es responsabilidad del usuario.

### `void de_manager_update(de_manager m)`

Recorre la zona activa hacia atrás, usando una instantánea de `active` tomada al empezar:

```c
uint16_t i = $->active;
while (i--) { /* procesa pool[i] */ }
```

Para cada entidad activa visitada:

- Si su `state` es un callback ejecutable, lo llama y guarda el resultado salvo que sea `DE_STATE_LOOP`.
- Si su `state` es exactamente `DE_STATE_PAUSE`, llama a `de_entity_pause(e)`.
- Si su `state` es exactamente `de_state_delete`, llama a `de_entity_delete(e)`.
- Si su `state` es exactamente `DE_STATE_LOOP` (asignado a mano, no devuelto por un callback), **no ocurre nada**: ninguna de las tres ramas anteriores se cumple. Por eso `DE_STATE_LOOP` solo tiene sentido como *valor de retorno* de un callback, nunca como valor asignado directamente a `entity->state`.

La traversal hacia atrás es segura frente a pausas/borrados de la entidad **actual** porque tanto `de_entity_pause` como `de_entity_delete` reducen la zona activa por su borde derecho — el lado que la iteración ya ha visitado — así que nunca hace falta releer `active` durante el bucle.

### `void de_manager_reset(de_manager m)`

```c
void de_manager_reset(de_manager $) {
    de_manager_foreach($, de_entity_delete(ENTITY));
    $->active = 0;
    $->paused = $->capacity;
}
```

Borra (con destructor incluido) **todas las entidades que estaban en la zona activa** en el momento de la llamada, y después reinicia `active` y `paused` a sus valores iniciales.

> ⚠️ Ver [§18.2](#182-de_manager_reset-no-destruye-las-entidades-pausadas) — las entidades pausadas **no** se visitan en este proceso y, por tanto, **sus destructores no se ejecutan**.

---

## 10. API de sistema

```c
void     darksys_init(darksys , void **, uint16_t, uint16_t);
uint16_t darksys_remove(darksys , void *);
```

### `darksys_init(s, storage, capacity_groups, params)`

Fija `pool = end = storage`, `size = 0`, `capacity = capacity_groups * params`, `params = params`. No hay validación de que `storage` tenga realmente ese tamaño.

### `uint16_t darksys_remove(darksys s, void *first)`

Busca, avanzando de `params` en `params`, el grupo cuyo **primer** puntero sea exactamente `first`. Si lo encuentra: reduce `size` en `params`, y si el grupo encontrado no era el último, copia el último grupo sobre la posición eliminada (compactación tipo *swap-remove*, por lo que **no preserva el orden**). Devuelve `1`. Si no lo encuentra tras recorrer todo el pool, devuelve `0`.

### `darksys_add` / `DARKSYS_FOREACH` (macros)

Ver [§11](#11-macros-públicas).

### 10.3 `DARKSYS_ITERATOR`

Genera una función con la forma:

```c
void *nombre(darksys system) {
    /* cuerpo de DARKSYS_FOREACH */
    return DE_STATE_LOOP;
}
```

pensada para instalarse directamente como `entity->state`. **Importante:** la función generada recibe `darksys `, no `void *`; formalmente no es compatible con `de_state` bajo ISO C estricto, aunque funciona en el ABI GNU C/68000 objetivo. Trátese como una conversión de puntero a función específica de plataforma, no como algo portable.

---

## 11. Macros públicas

### `_DE_ENTITY_STRIDE(PAYLOAD)`

Ver [§6.1](#61-entidad-y-stride).

### `DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)` / `de_manager_args(NAME)`

```c
struct de_manager mgr;
DE_MANAGER_STORAGE(mgr_storage, 64, sizeof(struct MyComponent));
de_manager_init(&mgr, de_manager_args(mgr_storage));
```

### `de_manager_foreach(M, CODE)`

```c
#define de_manager_foreach(M, CODE)         \
    do {                                    \
        uint16_t INDEX = (M)->active; \
        de_entity *POOL = (M)->pool;        \
        while (INDEX--) {                   \
            de_entity ENTITY = POOL[INDEX]; \
            CODE;                           \
        }                                   \
    } while (0)
```

Itera solo la zona activa, en orden descendente. Dentro de `CODE` quedan expuestas tres variables con nombre fijo:

- `INDEX` — índice actual (`uint16_t`).
- `POOL` — el array `manager->pool`.
- `ENTITY` — la entidad actual.

```c
de_manager_foreach(&g_manager, {
    if (ENTITY->tag == TAG_ENEMY)
        update_enemy(ENTITY);
});
```

**Regla de seguridad:** borrar, pausar o resumir `ENTITY` (la entidad visitada en la iteración actual) es seguro. Mutar una entidad **distinta** no lo es en general: si esa otra entidad todavía no ha sido visitada (índice menor que `INDEX`) y se borra/pausa, el hueco que deja se rellena con una entidad que **ya** fue visitada, la cual puede acabar procesándose una segunda vez en la misma pasada.

### `DARKSYS_STORAGE(NAME, CAPACITY, PARAMS)` / `darksys_args(NAME)`

```c
struct darksys sys;
DARKSYS_STORAGE(sys_storage, 32, 3);
darksys_init(&sys, darksys_args(sys_storage));
```

### `darksys_add(sys, ...)`

Añade un grupo de 1 a 5 punteros; el primer argumento es siempre el sistema:

```c
uint16_t ok = darksys_add(&physics, entity, velocity, position); /* 1 = éxito, 0 = lleno */
```

⚠️ El número de punteros pasado en **cada** llamada debe coincidir siempre con el `params` con el que se inicializó el sistema. Darken no lo valida: mezclar aridades en distintas llamadas sobre el mismo sistema desalinea silenciosamente el agrupamiento del pool, y tanto `DARKSYS_FOREACH` como `darksys_remove` asumen grupos de tamaño uniforme.

### `DARKSYS_FOREACH(sys, ...)`

De 0 a 5 variables de salida seguidas del bloque de código; el primer argumento es siempre el sistema:

```c
DARKSYS_FOREACH(&physics, struct Position *pos, struct Velocity *vel,
{
    pos->x += vel->x;
    pos->y += vel->y;
});
```

Las variables pueden declararse en el propio sitio de la llamada (como arriba: `struct Position *pos = pool[0];` es una asignación de `void*` a puntero tipado, válida en C) o ser variables ya existentes.

### `DARKSYS_ITERATOR(nombre, ...)`

Ver [§10.3](#103-darksys_iterator).

```c
DARKSYS_ITERATOR(sys_movement_f,
    struct Position *pos,
    struct Velocity *vel,
    {
        pos->x += vel->x;
        pos->y += vel->y;
    }
);
```

---

## 12. Máquina de estados

### Ciclo de vida típico

```text
                 de_manager_new()
                        │
                        ▼
                  state = de_state_delete
                        │  (el usuario asigna un callback)
                        ▼
                  ┌───────────┐
                  │  ACTIVA   │◄────────┐
                  └─────┬─────┘         │
                        │               │ de_entity_resume()
          ┌─────────────┼─────────────┐ │
          │             │             │ │
     otro estado   DE_STATE_PAUSE  de_state_delete
          │             │             │
          ▼             ▼             ▼
       ACTIVA        PAUSADA         LIBRE
                        │
                        └── de_entity_delete() ──► LIBRE
```

- **Creación:** `de_manager_new()` deja `state = de_state_delete`. La entidad no hace nada hasta que se le asigna un callback (valor de puntero `> 2`).
- **Actualización:** `de_manager_update()` ejecuta el callback activo. Su retorno decide la transición: otro callback, `LOOP` (sin cambios), `PAUSE` o `DELETE` (efectivas en la **siguiente** llamada a `de_manager_update`, no de inmediato).
- **Pausa/reanudación:** cambian de zona sin mover la entidad físicamente.
- **Borrado:** libera el slot; si hay destructor, se ejecuta antes, sin poder cancelar el borrado.

### Transiciones manuales frente a automáticas

Además de que un callback *devuelva* `DE_STATE_PAUSE` / `de_state_delete`, el usuario puede escribir directamente `entity->state = DE_STATE_PAUSE;` (o `DELETE`) en cualquier momento fuera del bucle del manager. `de_manager_update()` lo detectará y aplicará en su siguiente pasada. **No hagas esto con `DE_STATE_LOOP`**: como se explica en [§9](#void-de_manager_updatede_manager-m), un `state` fijado manualmente a `LOOP` no encaja en ninguna de las ramas de `de_manager_update()` y la entidad queda inerte (activa, pero sin ejecutar nada) hasta que se le asigne otro valor.

---

## 13. Orden de actualización

`de_manager_update()` y `de_manager_foreach` recorren la zona activa de mayor a menor índice:

```text
índice:     0     1     2     3     4
           ┌─────┬─────┬─────┬─────┬─────┐
           │ E0  │ E1  │ E2  │ E3  │ E4  │
           └─────┴─────┴─────┴─────┴─────┘
                                       ▲
                                  se visita
                                   primero
```

Orden real de ejecución: `E4 → E3 → E2 → E1 → E0`.

Por eso `de_entity_move_front()` (lleva la entidad al índice más alto) hace que se ejecute **antes** en la próxima pasada, y `de_entity_move_back()` (índice `0`) hace que se ejecute **después** — los nombres se refieren a la posición en el array, no al orden temporal en sentido intuitivo izquierda-derecha.

---

## 14. Complejidad

### Manager

| Operación              |                                                                                                        Complejidad |
| ---------------------- | -----------------------------------------------------------------------------------------------------------------: |
| `de_manager_init`      |                                                                                                        O(capacity) |
| `de_manager_new`       |                                                                                                               O(1) |
| `de_entity_pause`      |                                                                                                               O(1) |
| `de_entity_resume`     |                                                                                                               O(1) |
| `de_entity_delete`     |                                                                                                               O(1) |
| `de_entity_move_front` |                                                                                                               O(1) |
| `de_entity_move_back`  |                                                                                                               O(1) |
| `de_manager_update`    |                                                              O(active), sin contar el coste de los callbacks |
| `de_manager_reset`     | O(active) — **no** O(active + paused); ver [§18.2](#182-de_manager_reset-no-destruye-las-entidades-pausadas) |

### Sistema

| Operación           |                                                 Complejidad |
| ------------------- | ----------------------------------------------------------: |
| `darksys_add`     |                                                        O(1) |
| `DARKSYS_FOREACH` |                                                   O(grupos) |
| `darksys_remove`  | O(grupos) para la búsqueda + O(params) para la compactación |

---

## 15. Ejemplo completo

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"

struct player_data {
    int16_t x, y;
    int16_t speed;
};

void *player_move(void *raw) {
    struct player_data *data = (struct player_data *)raw;

    data->x += data->speed;

    if (data->x > 320)
        return de_state_delete;   /* salió de la pantalla */

    return DE_STATE_LOOP;
}

int main(void) {
    struct de_manager mgr;
    de_entity  player;

    /* 1. Almacenamiento estático para hasta 16 entidades */
    DE_MANAGER_STORAGE(storage, 16, sizeof(struct player_data));
    de_manager_init(&mgr, de_manager_args(storage));

    /* 2. Crear la entidad */
    player = de_manager_new(&mgr);
    if (!player) return 1;

    /* 3. Poblar payload y estado */
    struct player_data *data = (struct player_data *)player->data;
    data->x = 0;
    data->y = 100;
    data->speed = 2;

    player->state = player_move;
    player->tag   = 1; /* TAG_PLAYER */

    /* 4. Bucle de juego simplificado */
    for (int frame = 0; frame < 200; ++frame)
        de_manager_update(&mgr);

    /* 5. Limpieza total (solo entidades activas, ver §18.2) */
    de_manager_reset(&mgr);
    return 0;
}
```

### Uso combinado con un sistema

```c
struct vec2 { int16_t x, y; };

struct darksys sys;
struct vec2 positions[8];
struct vec2 velocities[8];

DARKSYS_STORAGE(sys_storage, 8, 2);
darksys_init(&sys, darksys_args(sys_storage));

for (int i = 0; i < 8; ++i)
    darksys_add(&sys, &positions[i], &velocities[i]);

DARKSYS_FOREACH(&sys, struct vec2 *pos, struct vec2 *vel,
{
    pos->x += vel->x;
    pos->y += vel->y;
});

darksys_remove(&sys, &positions[3]); /* elimina el grupo por su primer puntero */
```

*(Ambos fragmentos se han compilado y ejecutado para verificar este documento; el segundo se muestra tal cual con `gcc -std=gnu11`.)*

---

## 16. Reglas de seguridad

### Hacer

- Mantener viva la memoria de `DE_MANAGER_STORAGE` / `DARKSYS_STORAGE` durante toda la vida del manager/sistema correspondiente.
- Tratar `entity->data` como válido mientras la entidad esté activa o pausada.
- Usar siempre el puntero devuelto por `de_manager_new()`.
- Esperar que `darksys_remove()` reordene los grupos restantes (no preserva orden).
- Esperar que la iteración activa vaya siempre de mayor a menor índice.

### No hacer

- Liberar el almacenamiento del manager/sistema mientras haya entidades o grupos vivos.
- Reutilizar un `de_entity` después de `de_entity_delete()`.
- Asumir que el orden de las entidades activas es estable entre frames.
- Modificar una entidad **distinta** de la actual dentro de `de_manager_foreach` o `de_manager_update`, salvo que se sepa explícitamente que es seguro.
- Asignar `entity->state = DE_STATE_LOOP` a mano (solo tiene sentido como valor de retorno de un callback).
- Tratar la función generada por `DARKSYS_ITERATOR` como un `de_state` portable bajo ISO C estricto.
- Asumir que `de_manager_reset()` invoca los destructores de las entidades pausadas.

---

## 17. Invariantes internos

A lo largo de todas las operaciones del manager se mantiene:

```text
0 ≤ active ≤ paused ≤ capacity
```

Toda operación pública (`new`, `pause`, `resume`, `delete`) mueve como máximo uno de los dos límites (`active`, `paused`) en una unidad, o ambos a la vez en `pause`/`resume`, preservando siempre esta relación. Es lo que garantiza que las tres zonas nunca se solapen ni dejen huecos.

---

## 18. Peculiaridades y advertencias importantes

Estos tres puntos se han verificado compilando y ejecutando código contra el header (no son solo una lectura del comentario, sino un comportamiento comprobado).

### 18.1 `de_entity_update` sobre una entidad inactiva

```c
void *de_entity_update(de_entity $) {
    de_state s = de_entity_exec($);          /* devuelve 0 si $->state no es un callback */
    if (!_DE_STATE_IS_LOOP(s)) $->state = s;   /* 0 no es LOOP, así que SIEMPRE se escribe */
    return s;
}
```

Si en el momento de llamar a `de_entity_update()` el `state` de la entidad ya es uno de los valores de control (`de_state_delete`, `DE_STATE_LOOP` o `DE_STATE_PAUSE`, es decir, no es un callback ejecutable), `de_entity_exec()` devuelve `0`. Como `0` no es `DE_STATE_LOOP`, `de_entity_update()` escribe ese `0` en `entity->state`, es decir, **fuerza el estado a `de_state_delete`**, sea cual sea el valor de control que hubiera antes.

Comprobado en tiempo de ejecución: partiendo de `entity->state = DE_STATE_PAUSE` y llamando a `de_entity_update(entity)`, el estado resultante es `de_state_delete`, no `DE_STATE_PAUSE`.

**Implicación práctica:** `de_entity_update()` solo es segura/útil sobre una entidad que en ese instante tiene un callback activo asignado. No la uses como forma de "avanzar" una entidad que pueda estar en pausa o ya marcada para borrado — usa `de_entity_exec()` (que no escribe nada) o comprueba `_DE_STATE_IS_ACTIVE(entity->state)` antes de llamar.

### 18.2 `de_manager_reset` no destruye las entidades pausadas

```c
void de_manager_reset(de_manager $) {
    de_manager_foreach($, de_entity_delete(ENTITY));  /* solo recorre [0, active) */
    $->active = 0;
    $->paused = $->capacity;
}
```

`de_manager_foreach` itera exclusivamente `[0, active)`. Las entidades pausadas viven en `[paused, capacity)` y **nunca son visitadas** por este bucle. Tras el `FOREACH`, `de_manager_reset()` simplemente reescribe `paused = capacity`, reabsorbiendo toda la zona pausada en la zona libre **sin llamar a los destructores de las entidades que estaban pausadas**.

Comprobado en tiempo de ejecución: con una entidad activa y una pausada, ambas con destructor, `de_manager_reset()` invoca el destructor una sola vez (el de la entidad activa); el de la pausada no se ejecuta.

**Implicación práctica:** si tus entidades pausadas retienen recursos que deben liberarse (memoria fuera del propio bloque de Darken, handles, etc.), reanúdalas o bórralas explícitamente **antes** de llamar a `de_manager_reset()`. No asumas que el reset es una limpieza total equivalente a borrar una por una todas las entidades vivas.

### 18.3 `de_entity_swap` no es pública

La operación de intercambio de dos entidades existe en el código (`_de_entity_swap`), pero:

- Está declarada `static`, es decir, con enlace interno al archivo que define `DARKEN_IMPLEMENTATION` — no es visible ni siquiera con una declaración externa desde otra unidad de traducción.
- No aparece en la sección de prototipos públicos del header (solo `de_entity_exec`, `de_entity_update`, `de_entity_pause`, `de_entity_resume`, `de_entity_delete`, `de_entity_move_front`, `de_entity_move_back` están declaradas allí).

No existe una función pública `de_entity_swap()`. El intercambio de dos entidades cualesquiera no forma parte de la API pública actual; solo se usa internamente como implementación de `pause`, `resume`, `delete`, `move_front` y `move_back`.

### 18.4 Mismatch de aridad en `darksys_add` / `DARKSYS_FOREACH` / `DARKSYS_ITERATOR`

Ninguna de estas macros comprueba en tiempo de compilación ni de ejecución que el número de punteros usado coincida con el `params` con el que se inicializó el sistema. Un desajuste no produce un error visible: simplemente descoloca el agrupamiento interno del pool de forma silenciosa, y las lecturas posteriores (`DARKSYS_FOREACH`, `darksys_remove`) leerán punteros de otro grupo. Es responsabilidad exclusiva del usuario mantener la aridad constante para un mismo `darksys`.

---

## 19. Qué es y qué no es Darken

> Un gestor de ciclo de vida de entidades de capacidad fija, más un sistema compacto de procesamiento de punteros.

Darken **no** es un ECS de archetypes al uso. No ofrece:

- registro de componentes en tiempo de ejecución ni IDs de tipo;
- migración de archetypes;
- queries automáticas;
- asignación dinámica de memoria;
- reflexión ni serialización;
- soporte multihilo.

Es una decisión deliberada: Darken da los mecanismos de bajo nivel (memoria predecible, capacidad fija, movimiento de punteros en vez de bytes, ciclo de vida explícito) para que el código de juego construya sobre ellos la arquitectura que necesite, sin imponer una.

```text
   memoria predecible
            +
     capacidad fija
            +
  movimiento de punteros, no de bytes
            +
    ciclo de vida explícito
            +
  eficiencia orientada al 68000
            =
          Darken
```

El manager es dueño del ciclo de vida y del orden. La entidad es dueña de su estado y su payload. El sistema es dueño de listas compactas de punteros. Esa separación es la que mantiene el núcleo pequeño.

---

## 20. Referencia rápida de la API

### Tipos

```c
typedef void *(*de_state)(void *);

typedef struct de_entity *de_entity;   /* ya es un puntero */
typedef struct de_manager de_manager;
typedef struct darksys  darksys;

struct de_entity  { de_state state, destructor; de_manager manager; uint16_t slot, tag; uint8_t data[]; };
struct de_manager { de_entity *pool; uint16_t capacity, active, paused; };
struct darksys  { void **pool, **end; uint16_t capacity, size, params; };
```

### Funciones — entidad

| Función                                | Descripción                                                                                   |
| -------------------------------------- | --------------------------------------------------------------------------------------------- |
| `void *de_entity_exec(de_entity)`      | Ejecuta el estado activo sin escribir `entity->state`                                         |
| `void *de_entity_update(de_entity)`    | Ejecuta y guarda la transición; ver [§18.1](#181-de_entity_update-sobre-una-entidad-inactiva) |
| `void de_entity_pause(de_entity)`      | Mueve a la zona pausada (no-op si no está activa)                                             |
| `void de_entity_resume(de_entity)`     | Mueve a la zona activa (no-op si no está pausada)                                             |
| `void de_entity_delete(de_entity)`     | Borra, ejecuta destructor si existe (no-op si ya está libre)                                  |
| `void de_entity_move_front(de_entity)` | Adelanta la ejecución en la próxima pasada                                                    |
| `void de_entity_move_back(de_entity)`  | Retrasa la ejecución en la próxima pasada                                                     |

*(`_de_entity_swap` es interna, `static`, no forma parte de la API pública — ver [§18.3](#183-de_entity_swap-no-es-pública).)*

### Funciones — manager

| Función                                                                    | Descripción                                                                             |
| -------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| `void de_manager_init(de_manager*, de_entity*, void*, uint16_t, uint16_t)` | Inicializa manager sobre almacenamiento del usuario                                     |
| `de_entity de_manager_new(de_manager*)`                                    | Crea entidad; `NULL` si no hay capacidad                                                |
| `void de_manager_update(de_manager*)`                                      | Ejecuta la zona activa, hacia atrás                                                     |
| `void de_manager_reset(de_manager*)`                                       | Vacía el manager; ver [§18.2](#182-de_manager_reset-no-destruye-las-entidades-pausadas) |

### Funciones — sistema

| Función                                                       | Descripción                                         |
| ------------------------------------------------------------- | --------------------------------------------------- |
| `void darksys_init(darksys*, void**, uint16_t, uint16_t)` | Inicializa sistema sobre almacenamiento del usuario |
| `uint16_t darksys_remove(darksys*, void*)`                | Elimina grupo por su primer puntero; `1`/`0`        |

### Macros

| Macro                                              | Descripción                                                  |
| -------------------------------------------------- | ------------------------------------------------------------ |
| `_DE_ENTITY_STRIDE(PAYLOAD)`                        | Stride alineado a 4 bytes para un payload dado               |
| `DE_MANAGER_STORAGE(NAME, CAPACITY, PAYLOAD_SIZE)` | Declara almacenamiento estático de manager                   |
| `de_manager_args(NAME)`                            | Expande a los 4 argumentos de `de_manager_init`              |
| `de_manager_foreach(M, CODE)`                      | Itera la zona activa hacia atrás (`INDEX`, `POOL`, `ENTITY`) |
| `DARKSYS_STORAGE(NAME, CAPACITY, PARAMS)`        | Declara almacenamiento estático de sistema                   |
| `darksys_args(NAME)`                             | Expande a los 3 argumentos de `darksys_init`               |
| `darksys_add(sys, ...)`                          | Añade grupo de 1–5 punteros                                  |
| `DARKSYS_FOREACH(sys, ...)`                      | Itera grupos, 0–5 variables de salida                        |
| `DARKSYS_ITERATOR(nombre, ...)`                  | Genera `void *nombre(darksys*)` a partir de un `FOREACH`   |

---

**Licencia / autoría:** no especificada en el código fuente proporcionado — añádela aquí según corresponda al proyecto.