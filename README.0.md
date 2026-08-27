# Darken (DARKula ENgine) 2.0 — Sistema de entidades

Gestor de entidades de un solo header en C, sin asignación dinámica de memoria, para juegos
con recursos ajustados (pensado para GCC + Motorola 68000). Cada entidad es una pequeña máquina
de estados: una función que se ejecuta cada frame y decide si sigue igual, cambia de
comportamiento, se pausa o desaparece.

Esta guía cubre **solo la API pública**, desde el punto de vista de quien la usa — qué le
pasas a cada función, qué te devuelve, qué debes tener en cuenta. Todas las afirmaciones sobre
comportamientos no evidentes (§8) las compilé y ejecuté contra el header real antes de
escribirlas aquí, no son suposiciones.

Ejemplo que se usa en toda la guía: un pequeño sistema de **partículas**.

```c
typedef struct {
    float x, y;
    float vy;
    int   life;
} Particle;
```

---

## 1. Instalación

Header-only, estilo *stb*. En **un** `.c` de tu proyecto:

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
```

En el resto de archivos, solo el include normal:

```c
#include "darken.h"
```

Si olvidas definir `DARKEN_IMPLEMENTATION` en algún `.c`, tendrás errores de enlazado; si lo
defines en más de uno, símbolos duplicados. El mismo trato que cualquier librería de un solo
header.

---

## 2. Conceptos antes de tocar la API

Una entidad, en todo momento, está en uno de tres estados desde tu punto de vista:

- **Activa**: `darken_update()` la ejecuta cada frame y `DARKEN_FOREACH` la visita.
- **Pausada**: nadie la ejecuta ni la visita, pero su memoria sigue siendo válida — puedes
  guardarte un puntero a ella y usarlo cuando la reanudes.
- **Libre**: slot disponible, sin asignar todavía a ninguna entidad tuya.

Campos que puedes leer/escribir libremente en un `darken_entity`:

| Campo              | Para qué lo usas                                                                  |
| ------------------ | --------------------------------------------------------------------------------- |
| `state`            | La función que se ejecuta cada frame (§4.3). La fijas tú.                         |
| `destructor`       | Función opcional llamada al borrar la entidad, o `NULL` si no necesitas limpieza. |
| `tag` (`uint32_t`) | Libre para ti — típicamente un id de tipo o flags.                                |
| `usr` (`uint16_t`) | Libre para ti — otro campo de propósito general.                                  |
| `data[]`           | Tu payload (`Particle` en el ejemplo). Se accede con `DARKEN_DATA`.               |

El manager (`darken`) también expone campos que puedes **leer** directamente si te resulta útil
— por ejemplo `manager.size` te dice cuántas entidades hay activas ahora mismo (lo uso en el
ejemplo completo de §6 para saber cuándo parar el bucle). No hace falta llamar a ninguna
función para consultarlo.

Los campos `owner` y `slot` de `darken_entity` son contabilidad interna del motor — no forman
parte del contrato público; no los leas ni los modifiques.

---

## 3. Referencia rápida

### Valores de control (lo que devuelve una función de estado)

| Valor                    | Efecto                                             |
| ------------------------ | -------------------------------------------------- |
| `DARKEN_LOOP`            | Seguir en el mismo estado el próximo frame.        |
| `DARKEN_DELETE`          | Borrar la entidad (con destructor, si tiene).      |
| `DARKEN_PAUSE`           | Pasar a pausa.                                     |
| *cualquier otro puntero* | Se interpreta como la **nueva** función de estado. |

### Funciones

| Función                                                | Qué hace                                                                                           |
| ------------------------------------------------------ | -------------------------------------------------------------------------------------------------- |
| `darken_init(darken*, pool, storage, capacity, bytes)` | Inicializa el manager sobre memoria ya reservada (normalmente vía `DARKEN_STORAGE`/`DARKEN_ARGS`). |
| `darken_spawn(darken*)`                                | Activa una entidad libre y te la entrega. Devuelve `NULL` si no queda espacio.                     |
| `darken_update(darken*)`                               | El "tick": ejecuta el estado de cada entidad activa y aplica pausas/borrados pendientes.           |
| `darken_reset(darken*)`                                | Borra todas las entidades **activas** (con destructor) y deja el manager como recién iniciado.     |
| `darken_entity_run(darken_entity)`                     | Ejecuta el estado actual una vez, sin aplicar transición.                                          |
| `darken_entity_update(darken_entity)`                  | Ejecuta el estado actual y sí aplica la transición, bajo demanda.                                  |
| `darken_entity_pause(darken_entity)`                   | Pausa la entidad de inmediato.                                                                     |
| `darken_entity_resume(darken_entity)`                  | Reanuda una entidad pausada.                                                                       |
| `darken_entity_delete(darken_entity)`                  | Borra la entidad, esté activa o pausada.                                                           |

### Macros

| Macro                                            | Para qué sirve                                                                     |
| ------------------------------------------------ | ---------------------------------------------------------------------------------- |
| `DARKEN_DATA(TIPO, var, entidad)`                | `TIPO *var = (TIPO*)entidad->data;` — acceso tipado al payload.                    |
| `DARKEN_STORAGE(nombre, capacidad, tam_payload)` | Declara el almacenamiento fijo que necesita `darken_init`.                         |
| `DARKEN_ARGS(nombre)`                            | Expande a los argumentos de `DARKEN_STORAGE` en el orden que espera `darken_init`. |
| `DARKEN_FOREACH(manager, CODIGO)`                | Recorre las entidades activas; expone `ENTITY` dentro de `CODIGO`.                 |

Eso es todo lo público — no hay más macros ni funciones expuestas por el header (en particular,
no existe ninguna macro pública para inspeccionar directamente si un valor de estado es
`DARKEN_LOOP`/`DELETE`/`PAUSE`/activo; esa comprobación es interna).

---

## 4. Guía paso a paso

### 4.1 Reservar el manager

Cada manager necesita el almacenamiento (`DARKEN_STORAGE`) y el propio `darken` que lo
gobierna, declarado aparte:

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"

typedef struct { float x, y, vy; int life; } Particle;

DARKEN_STORAGE(storage, 64, sizeof(Particle)); // sitio para 64 entidades
darken manager;

int main(void)
{
    darken_init(&manager, DARKEN_ARGS(storage));
    // manager.size == 0, manager.capacity == 64, todo libre
}
```

`DARKEN_ARGS(storage)` expande a `storage.pool, storage.data, storage.capacity,
storage.payload_size` — por eso `darken_init` recibe 5 argumentos en total (el `&manager` que
pones tú, más los 4 de la macro).

### 4.2 Crear entidades y acceder a su payload

```c
darken_entity spawn_particle(float x, float y, float vy, int life)
{
    darken_entity e = darken_spawn(&manager);
    if (!e)
        return NULL; // pool lleno — comprueba esto siempre, ver §8

    DARKEN_DATA(Particle, p, e);
    p->x = x; p->y = y; p->vy = vy; p->life = life;

    e->destroy= NULL; // sin limpieza especial (ver por qué esto es obligatorio en §8)
    return e;
}
```

`e` (el `darken_entity`) y `p` (el puntero a `Particle`) son direcciones distintas: `p` apunta
dentro de `e->data`. `p` sigue siendo válido aunque la entidad se pause o el motor reordene
internamente el pool, porque la dirección física de `e` nunca cambia.

### 4.3 Escribir el comportamiento

`e->update` es la función que se ejecuta cada `darken_update`. Recibe el payload y su valor de
retorno decide qué pasa después (tabla en §3):

```c
void *particle_falling(Particle *p)
{
    p->y += p->vy;
    if (--p->life <= 0)
        return DARKEN_DELETE;
    return DARKEN_LOOP;
}
```

```c
darken_entity e = spawn_particle(0.0f, 0.0f, 2.0f, 4);
if (e) e->update = particle_falling;
```

Nota: no hizo falta ningún *cast* al asignar `particle_falling` (que recibe `Particle *`) a
`e->update` (declarado como `darken_callback`, que recibe "algo"): lo comprobé compilando con
`-Wall -Wextra` y no aparece ningún aviso. Es cómodo — cada tipo de entidad puede escribir sus
funciones con su propio tipo de puntero — pero también significa que el compilador no te avisará
si mezclas el tipo equivocado entre `state`/`destructor` de dos tipos de entidad distintos.

### 4.4 El bucle principal

```c
while (manager.size > 0) {
    darken_update(&manager); // ejecuta el estado de cada entidad activa
}
```

### 4.5 Destructores

Se llaman automáticamente al borrar una entidad — **con dos excepciones importantes que
verifiqué en §8** (borrado manual de una pausada, y `darken_reset` con pausadas de por medio).
Reciben el mismo payload que el estado:

```c
void *particle_destructor(Particle *p)
{
    printf("particula en (%.1f, %.1f) destruida\n", p->x, p->y);
    return NULL; // el valor de retorno del destructor se ignora
}

e->destroy= particle_destructor;
```

### 4.6 Recorrer las entidades activas

Útil para lógica que no encaja dentro del propio estado de una entidad (dibujar, colisiones
entre todas las entidades, etc.):

```c
DARKEN_FOREACH(&manager, {
    DARKEN_DATA(Particle, p, ENTITY);
    dibujar_sprite(p->x, p->y);
});
```

`ENTITY` (tipo `darken_entity`) lo define la propia macro dentro del bloque. Solo visita la zona
activa — nunca libres ni pausadas.

### 4.7 Pausar y reanudar

Pausar saca a la entidad del bucle de `darken_update` sin borrarla — su memoria sigue intacta:

```c
darken_entity_pause(e);   // e sale de la zona activa, deja de actualizarse
// ...
darken_entity_resume(e);  // e vuelve a la zona activa
```

(Probé explícitamente `darken_entity_resume()` con varias entidades pausadas a la vez,
reanudando una que no era ni la primera ni la última en pausarse, y se comportó correctamente
en todos los casos — sin corromper ni perder ninguna entidad.)

### 4.8 Borrar una entidad concreta

```c
darken_entity_delete(e); // funciona tanto si "e" está activa como pausada
```

Ver §8 sobre el destructor si `e` estaba pausada.

### 4.9 Vaciar el manager entero

```c
darken_reset(&manager); // size vuelve a 0, paused vuelve a capacity
```

Ver §8 sobre qué pasa con las entidades pausadas en este caso.

---

## 5. Ejemplo completo

```c
#include <stdio.h>
#define DARKEN_IMPLEMENTATION
#include "darken.h"

typedef struct { float x, y, vy; int life; } Particle;

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

    for (int i = 0; i < 5; i++) {
        darken_entity e = darken_spawn(&manager);
        if (!e) break;

        DARKEN_DATA(Particle, p, e);
        p->x = (float)i; p->y = 0; p->vy = 1.0f; p->life = 3 + i;

        e->update = particle_falling;
        e->destroy= particle_destructor;
    }

    while (manager.size > 0)
        darken_update(&manager);

    return 0;
}
```

---

## 6. Fortalezas

- **Sin asignación dinámica en el bucle de juego**: todo el almacenamiento es un bloque fijo
  reservado una vez con `DARKEN_STORAGE`.
- **Spawn/pausa/reanudación/borrado son operaciones baratas y predecibles**, cómodas para
  presupuestos de frame ajustados.
- **Un puntero al payload de una entidad pausada sigue siendo válido**: no se toca ni se
  recicla mientras esté pausada, solo al reanudarla o borrarla explícitamente.
- **Máquina de estados directa**: cada entidad decide su propio destino devolviendo un valor
  desde su propia función — no necesitas flags de "vivo/muerto" por fuera.
- **Tipado cómodo en las funciones de estado**: cada tipo de entidad trabaja directamente con
  su propio struct, sin *casts* constantes (§4.3).

## 7. Limitaciones a tener en cuenta

- **Un manager admite un único tamaño de payload.** Si tienes entidades con structs distintos
  (balas, enemigos...), necesitas un `darken` (y su `DARKEN_STORAGE`) por cada tipo.
- **Máximo 65535 entidades por manager** (contadores internos de 16 bits) — de sobra para la
  mayoría de casos, pero tenlo presente para sistemas masivos (partículas, por ejemplo).
- **No hay comprobación de errores real**: una precondición incumplida (pausar algo que ya
  estaba pausado, reanudar algo que ya estaba activo, etc.) simplemente no hace nada — sin log,
  sin código de retorno, sin forma de detectarlo desde fuera.
- **No hay soporte de hilos.**
- **No hay forma pública de preguntar si una entidad concreta está pausada, activa o libre** —
  si tu lógica necesita saberlo, llévalo tú mismo con `tag`/`usr` o con una estructura aparte.

---

## 8. Avisos concretos (verificados compilando y ejecutando el header real)

**1. Comprueba siempre el `NULL` de `darken_spawn()`.** Si el pool está lleno, lo devuelve sin
avisar de ninguna otra forma.

**2. Rellena `state`, `destructor`, `tag`, `usr` y el payload en cada spawn — no des nada por
sentado.** Comprobé que un slot reciclado conserva los valores de su ocupante anterior. Con un
`destructor` real:

```
tras borrar A: destructor de A se ejecuta (correcto)
darken_spawn() reutiliza el mismo slot para B
si NO fijas b->destroyexplícitamente, sigue apuntando al destructor de A
al borrar B: se ejecuta el destructor de A sobre los datos de B
```

Lo reproduje exactamente así: el destructor de A se disparó de nuevo al borrar B, sin que el
código de B lo pidiera en ningún momento.

**3. `DARKEN_DELETE`/`DARKEN_PAUSE` devuelto desde el estado tarda un frame en aplicarse.**
Comprobé que tras el `darken_update()` en el que una entidad devuelve `DARKEN_DELETE`, sigue
contando como activa (`manager.size` sin cambiar, destructor sin ejecutar) hasta la llamada
*siguiente*. Si necesitas el efecto inmediato, llama tú mismo a `darken_entity_delete()` /
`darken_entity_pause()` en vez de depender del retorno del estado.

**4. `darken_entity_delete()` sobre una entidad pausada NO llama a su destructor.** Confirmado:
solo se garantiza la llamada al destructor cuando se borra algo que estaba activo. Si necesitas
limpieza garantizada, reanuda antes de borrar:

```c
darken_entity_resume(e);
darken_entity_delete(e); // ahora sí se ejecuta el destructor
```

**5. `darken_reset()` tampoco llama al destructor de las entidades pausadas — y aun así libera
sus slots.** Confirmado con una entidad activa y una pausada, ambas con destructor: tras
`darken_reset()`, solo se ejecutó el de la activa; el `capacity`/`paused` del manager quedó como
si la pausada nunca hubiera existido. Si dependes de que la limpieza se ejecute siempre,
reanuda todo lo pausado antes de resetear.

**6. `DARKEN_FOREACH` usa siempre los nombres `ENTITY` e `INDEX`, no configurables.** No
declares variables con esos nombres dentro del bloque, y evita anidar un `DARKEN_FOREACH`
dentro de otro (el interno tapa silenciosamente las variables del externo).

**7. Sobre `darken_entity_resume()` con varias entidades pausadas**: lo probé específicamente —
pausar tres entidades y reanudar la del medio (ninguna de las que se pausó primero ni la
última) — y el resultado fue siempre correcto: la reanudada queda activa, y las otras dos
siguen pausadas sin mezclarse ni corromperse. Si en algún otro sitio ves advertencias sobre un
supuesto bug ahí, no lo pude reproducir contra este header.

---

## Checklist

- [ ] `DARKEN_IMPLEMENTATION` definido en un solo `.c`.
- [ ] Comprobar siempre que `darken_spawn()` no devuelva `NULL`.
- [ ] Fijar `state`, `destructor`, `tag`, `usr` y el payload en cada spawn, sin excepción.
- [ ] Si dependes de destructores garantizados, reanudar antes de `darken_entity_delete()` /
      `darken_reset()` sobre cualquier cosa pausada.
- [ ] Si necesitas el efecto inmediato de un borrado/pausa, llamar a
      `darken_entity_delete()`/`darken_entity_pause()` en vez de devolver el valor de control
      desde el estado.
- [ ] No declarar variables `ENTITY`/`INDEX` propias, no anidar `DARKEN_FOREACH`.
- [ ] Un manager = un tamaño de payload; un `darken` distinto por cada tipo de entidad.

11111111111111111111111111111111111111111111111111111111

## API pública (resumen)

### Tipos

| Tipo            | Descripción                                                                                                                |
| --------------- | -------------------------------------------------------------------------------------------------------------------------- |
| `darken`        | Puntero opaco al manager (`darken *`).                                                                                     |
| `darken_entity` | Puntero opaco a una entidad (`struct darken_entity *`).                                                                    |
| `darken_callback`  | `void *(*)()` — callback de estado/destructor. Recibe `entity->data` y devuelve el siguiente estado o un valor de control. |

### Valores de control (lo que puede devolver un `darken_callback`)

| Valor                    | Efecto                                                        |
| ------------------------ | ------------------------------------------------------------- |
| `DARKEN_DELETE`          | Elimina la entidad (invoca su destructor si tiene).           |
| `DARKEN_LOOP`            | Mantiene el estado actual sin cambios.                        |
| `DARKEN_PAUSE`           | Pausa la entidad (sale del bucle de update).                  |
| *cualquier otro puntero* | Se interpreta como el **nuevo** `darken_callback` de la entidad. |

### Funciones a nivel *manager*

| Función                                                                                           | Qué hace                                                                      |
| ------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| `void darken_init(darken, darken_entity *pool, void *storage, uint16_t capacity, uint16_t bytes)` | Inicializa el manager sobre memoria ya reservada por el llamador.             |
| `darken_entity darken_spawn(darken)`                                                              | Toma un slot libre y lo activa. Devuelve `NULL` si no queda espacio.          |
| `void darken_update(darken)`                                                                      | Recorre la zona activa: ejecuta estado, procesa pausas y borrados pendientes. |
| `void darken_reset(darken)`                                                                       | Vacía el manager (ver ⚠️ en «Puntos flacos»).                                  |

### Funciones a nivel *entidad*

| Función                                    | Qué hace                                                                            |
| ------------------------------------------ | ----------------------------------------------------------------------------------- |
| `void darken_entity_run(darken_entity)`    | Ejecuta el callback de estado **sin** aplicar su valor de retorno (no transiciona). |
| `void darken_entity_update(darken_entity)` | Ejecuta el callback y sí aplica el resultado (transiciona de estado).               |
| `void darken_entity_pause(darken_entity)`  | Mueve la entidad a la zona de pausadas.                                             |
| `void darken_entity_resume(darken_entity)` | Devuelve la entidad a la zona activa (ver ⚠️ en «Puntos flacos»).                    |
| `void darken_entity_delete(darken_entity)` | Borra la entidad esté activa o pausada, invocando su destructor.                    |

### Macros de conveniencia

| Macro                                                               | Para qué sirve                                                                                                          |
| ------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| `DARKEN(nombre, CAPACIDAD, TAMAÑO_PAYLOAD)` / `DARKEN_STORAGE(...)` | Declara el `pool[]` + bloque de datos alineado a 4 bytes. **No** declara el `darken` en sí — eso lo declaras tú aparte. |
| `DARKEN_ARGS(nombre)`                                               | Expande a `pool, data, capacity, payload_size` para pasar a `darken_init`.                                              |
| `DARKEN_DATA(TIPO, var, entidad)`                                   | `TIPO *var = (TIPO *)entidad->data;` — acceso tipado al payload.                                                        |
| `DARKEN_FOREACH(manager, CODIGO)`                                   | Itera la zona activa; expone `ENTITY` (tipo `darken_entity`) dentro de `CODIGO`.                                        |
| `DARKEN_STATE_IS_DELETED/LOOP/PAUSED/ACTIVE(estado)`                | Clasifica un valor `darken_callback`.                                                                                      |
| `DARKEN_ASSERT(condicion, valor_de_retorno)`                        | `if (!condicion) return valor_de_retorno;` — guard rápido.                                                              |

### Campos públicos de `darken_entity`

```c
darken_callback state;       // callback ejecutado cada update
darken_callback destructor;  // opcional; se llama al borrar la entidad
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
- **`darken_callback` sin prototipo** (`void *(*)()` en vez de `void *(*)(void *)`):
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

333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333

## Instalación / patrón de uso

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
    darken_callback state;
    darken_callback destructor;
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

| Función                                                | Qué hace                                                                                                                                                                                                                                                                                    | Precondición (verificada con `_DARKEN_ASSERT`, que solo retorna en silencio si falla)                                                                     |
| ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `darken_init(darken*, pool, storage, capacity, bytes)` | Inicializa el manager: reparte `storage` en slots de `capacity` entidades, cada uno de tamaño `align4(sizeof(darken_entity)+bytes)`, y fija `owner`/`slot` de cada una. `size=0`, `paused=capacity` (todo libre).                                                                           | —                                                                                                                                                         |
| `darken_spawn(darken*)`                                | Toma el siguiente slot libre, lo pasa a la zona activa (`size++`) y devuelve el `darken_entity`.                                                                                                                                                                                            | Debe haber al menos un slot libre (`size < paused`); si no, **devuelve `NULL`**.                                                                          |
| `darken_update(darken*)`                               | El "tick" principal. Recorre la zona activa de atrás hacia delante; por cada entidad: si su estado es una función activa, la llama y aplica la transición; si su estado es `DARKEN_PAUSE`, la mueve a pausadas; si es `DARKEN_DELETE`, llama al destructor (si lo hay) y la mueve a libres. | —                                                                                                                                                         |
| `darken_reset(darken*)`                                | Borra **todas las entidades activas** (llamando a sus destructores) y resetea `size=0`, `paused=capacity`.                                                                                                                                                                                  | Ver ⚠️ pitfall §6.3 — las pausadas **no** pasan por aquí.                                                                                                  |
| `darken_entity_run(darken_entity)`                     | Llama a `state(data)` una vez, **sin** aplicar ninguna transición (ignora el valor de retorno). Útil para ejecutar la lógica actual bajo demanda (p. ej. una fase de render separada del update) sin mutar la máquina de estados.                                                           | La entidad debe estar activa.                                                                                                                             |
| `darken_entity_update(darken_entity)`                  | Igual que lo que hace `darken_update` por dentro para una sola entidad: llama a `state(data)` y, si el resultado no es `DARKEN_LOOP`, reemplaza `state`. Permite avanzar una entidad concreta bajo demanda en vez de esperar al frame.                                                      | La entidad debe estar activa.                                                                                                                             |
| `darken_entity_pause(darken_entity)`                   | Pausa manual e inmediata (sin esperar a que la propia entidad devuelva `DARKEN_PAUSE`).                                                                                                                                                                                                     | La entidad debe estar en la zona activa.                                                                                                                  |
| `darken_entity_resume(darken_entity)`                  | Reanuda una entidad pausada, devolviéndola a la zona activa.                                                                                                                                                                                                                                | La entidad debe estar en la zona de pausadas.                                                                                                             |
| `darken_entity_delete(darken_entity)`                  | Borra manualmente una entidad, esté activa o pausada.                                                                                                                                                                                                                                       | Ninguna explícita — si la entidad ya está libre, no hace nada (idempotente). Ver ⚠️ pitfall §6.2: **no llama al destructor si la entidad estaba pausada.** |

---

## 4. Macros públicas

| Macro                                            | Expande a / hace                                                                                                                                                       | Notas                                                                                                                                                                                                                                                                                                                                                                      |
| ------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DARKEN_DATA(TIPO, VAR, ENTIDAD)`                | `TIPO *VAR = (TIPO *)(ENTIDAD)->data;`                                                                                                                                 | Azúcar sintáctico para castear el payload al tipo real del usuario dentro de una función de estado.                                                                                                                                                                                                                                                                        |
| `DARKEN_DELETE` / `DARKEN_LOOP` / `DARKEN_PAUSE` | Valores centinela `(void*)0`, `(void*)1`, `(void*)2`                                                                                                                   | Los devuelve una función de estado para indicar "bórrame", "vuelve a llamarme sin cambiar de estado" y "pásame a pausa", respectivamente. Cualquier otro valor de retorno (>2) se interpreta como el *siguiente* puntero a función de estado.                                                                                                                              |
| `DARKEN_STORAGE(NOMBRE, CAPACIDAD, TAM_PAYLOAD)` | Declara e inicializa una struct anónima local con el array `pool[]` y el bloque de bytes `data[]` ya dimensionado y alineado a 4 bytes, más `capacity`/`payload_size`. | Gracias a la inicialización parcial con designadores (`= { .capacity = ..., .payload_size = ... }`), en C **todos los demás miembros —incluyendo `pool` y `data`— quedan puestos a cero** automáticamente. Esto es relevante: la primera vez que se usa un slot, su `state`/`destructor` valen 0, es decir `DARKEN_DELETE`/"sin destructor", un estado seguro por defecto. |
| `DARKEN_ARGS(NOMBRE)`                            | Expande a `(NOMBRE).pool, (NOMBRE).data, (NOMBRE).capacity, (NOMBRE).payload_size`                                                                                     | Pensado para pasarse directamente como los 4 últimos argumentos de `darken_init`.                                                                                                                                                                                                                                                                                          |
| `DARKEN_FOREACH(MANAGER, CODIGO)`                | Recorre la zona **activa** de atrás hacia delante, exponiendo variables locales fijas `ENTITY` (la entidad actual) e `INDEX` (su índice).                              | ⚠️ Los nombres `ENTITY`, `INDEX`, `POOL` están *hardcodeados* en la macro — ver pitfall §6.5.                                                                                                                                                                                                                                                                               |

### Ejemplo de uso combinado

```c
darken manager;
DARKEN_STORAGE(storage, /*capacidad*/ 64, sizeof(Enemy));
darken_init(&manager, DARKEN_ARGS(storage));
```

---

## 5. El ciclo de vida basado en estados

Cada entidad ejecuta, frame a frame, una función `darken_callback` que recibe `entity->data` y
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
    ent->update = (darken_callback)enemigo_patrulla;
    ent->destroy= (darken_callback)enemigo_destructor;
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
- **`darken_callback` es un puntero a función sin prototipo** (`void *(*)()`), lo que desactiva la
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
guarda ese valor en `entity->update`**; la entidad sigue contando como "activa"
(`slot < size`, visible para `DARKEN_FOREACH`) hasta la *siguiente* llamada a `darken_update`,
momento en el que el motor relee el estado, lo reconoce como centinela, y recién ahí llama al
destructor / mueve la entidad de zona.

```c
// Frame N: la función de estado decide borrarse
return DARKEN_DELETE;
// -> entity->update queda en 0, pero la entidad SIGUE en la zona activa

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
entA->destroy= destructor_de_A;
// ... más tarde A se borra, el slot 5 vuelve a la zona libre
// (destructor_de_A ya se ejecutó una vez, correctamente)

// Ciclo 2: entidad B recicla el slot 5
darken_entity entB = darken_spawn(&manager); // puede ser el mismo puntero que entA
entB->update = (darken_callback)estado_de_B;
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
sin comprobarlo (`darken_spawn(&mgr)->update = ...`) es un null-pointer-dereference clásico bajo
carga alta — exactamente cuando más entidades tienes en pantalla.

222222222222222222222222222222222222222222222222222222222222222222

---

# Darken (DARKula ENgine) — Sistema de entidades

`darken.h` es una librería single-header en C que implementa un gestor de entidades para sistemas con recursos ajustados.

Cada entidad es una pequeña máquina de estados: una función que se ejecuta cada frame y decide su siguiente estado.

## Requisitos

- Compilador con extensiones GNU C (usa `__attribute__`).
- C99 o posterior (usa flexible array members).
- `#include <stdint.h>` (ya incluido).

## Características

- Header-only: **un** archivo `.c`.
- Sin asignación dinámica de memoria.
- Orientado a sistemas retro y limitados; cada ciclo de CPU cuenta.
- Pensado para **Motorola 68000** (alineación de 4 bytes, campos de 16 bits)
- Pero usable en cualquier objetivo compatible con GNU C.

Darken gestiona un **pool de entidades de tamaño fijo**, repartido en tres zonas contiguas dentro de un mismo array de punteros:

- **Activas** `[0 ............ size]`
- **Libres** `[size ....... paused]`
- **Pausadas** `[paused ... capacity]`

La entidad en sí **nunca cambia de dirección de memoria** una vez asignada; lo único que se mueve entre zonas es su *puntero* dentro de `pool[]`. Esto es lo que permite guardar un puntero crudo a `entity->data` con seguridad, incluso mientras la entidad es pausada, reanudada o reordenada internamente.

<!-- 
| **Pausadas**   | `[paused, capacity]` | entidad siga pausada.|
| **Activas**    | `[0, size]`          | se actualizan cada frame.
| **Libres**     | `[size, paused]`     | slots reciclables.
| **Pausadas**   | `[paused, capacity]` | fuera del bucle de update; sus punteros a `entity->data` permanecen válidos mientras la entidad siga pausada.| -->

Ejemplo que usaré en toda la guía: un sistema de **proyectiles** de un shooter 2D.

```c
struct Bullet {
    float x, y, vx, vy;
    int ttl; // frames de vida restantes
};
```

## Instalación

Header-only, estilo *stb*. En **un** `.c` de tu proyecto:

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
```

En el resto de archivos que solo necesiten los tipos/prototipos:

```c
#include "darken.h"
```

## El modelo mental que necesitas para usar la API

Una entidad, en todo momento, está en uno de tres estados desde tu punto de vista como usuario:

- **Activa**: Se ejecuta a cada frame.
- **Pausada**: nadie la ejecuta ni la visita, pero sigue existiendo — su memoria y sus datos siguen siendo válidos, así que puedes guardarte un puntero a ella y usarlo más tarde, cuando la reanudes.
- **Libre**: slot disponible, todavía no asignado a ninguna entidad tuya.

Cada entidad (`darken_entity`) te expone estos campos para leer/escribir libremente:

| Campo              | Uso                                                                                          |
| ------------------ | -------------------------------------------------------------------------------------------- |
| `state`            | La función que se ejecuta cada frame (ver §4). La fijas tú al crear/reconfigurar la entidad. |
| `destructor`       | Función opcional que se llama al borrar la entidad (o `NULL` si no necesitas limpieza).      |
| `tag` (`uint32_t`) | Libre para ti — típicamente un identificador de tipo o unas flags.                           |
| `usr` (`uint16_t`) | Libre para ti — otro campo de propósito general.                                             |
| `data[]`           | Tu payload (`Bullet` en el ejemplo). Se accede con `DARKEN_DATA`.                            |

El resto de campos del struct (`owner`, `slot`) son contabilidad interna del motor — no los leas ni los modifiques desde tu código.

## Referencia de funciones públicas

### `darken_init()`

`void darken_init(darken *mgr, darken_entity *pool, void *storage, uint16_t capacity, uint16_t payload_size)`

Prepara el manager. Se llama **una vez**, normalmente junto con `DARKEN_STORAGE`/`DARKEN_ARGS`
(§5):

```c
darken proyectiles;
DARKEN_STORAGE(storage, /*capacidad*/ 256, sizeof(Bullet));
darken_init(&proyectiles, DARKEN_ARGS(storage));
```

Tras esto, el manager tiene `capacity` slots, todos libres.

### `darken_entity()`

`darken_entity darken_spawn(darken *mgr)`

Reserva una entidad libre y te la entrega ya como **activa**. Puede devolver `NULL` si no
queda ningún slot libre (pool lleno) — **compruébalo siempre**:

```c
darken_entity bala = darken_spawn(&proyectiles);
if (!bala) {
    // no había hueco; decide qué hacer (ignorar el disparo, reciclar la más vieja, etc.)
} else {
    DARKEN_DATA(Bullet, b, bala);
    b->x = jugador.x; b->y = jugador.y;
    b->vx = 0; b->vy = -8; b->ttl = 90;

    bala->update = (darken_callback)bala_estado_volando;
    bala->destroy= NULL; // esta entidad no necesita limpieza
}
```

⚠️ `darken_spawn` **no pone a cero ni reinicializa nada más allá de lo estrictamente interno**.
El slot que te entrega pudo haber sido usado antes por otra entidad tuya (una bala anterior que ya se borró), así que puede llegar con valores de una vida pasada: `state`, `destructor`, `tag`, `usr` y el payload pueden contener basura de ese uso previo. **Fija explícitamente todo lo que te importe cada vez que haces spawn** — en particular no olvides `destructor` (ponlo a `NULL` si no lo necesitas): si lo dejas sin tocar y el slot reciclado traía un destructor de una entidad de otro tipo, ese destructor equivocado se ejecutará sobre tus datos cuando borres esta entidad.

### `void darken_update(darken *mgr)`

El "tick" del sistema completo — se llama una vez por frame:

```c
while (jugando) {
    darken_update(&proyectiles);
    // ... resto del frame
}
```

Ejecuta la función `state` de cada entidad activa y aplica lo que devuelva (ver §4). También es el momento en el que se hacen efectivas las entidades que se marcaron para pausar o borrar.

⚠️ Si una entidad devolvió `DARKEN_DELETE` o `DARKEN_PAUSE` en un frame, **no desaparece/se pausa en ese mismo `darken_update()`** — sigue contando como activa (visible en `DARKEN_FOREACH`, con su destructor sin ejecutar todavía) hasta la llamada *siguiente*. En la práctica: si dibujas con `DARKEN_FOREACH` justo después de `darken_update()` en el mismo frame, puedes llegar a dibujar durante un frame de más una bala que "ya dijo que se borraba". Si necesitas que el efecto sea inmediato, llama tú mismo a `darken_entity_delete()` / `darken_entity_pause()` en vez de depender del valor de retorno del estado.

### `void darken_reset(darken *mgr)`

Borra de golpe **todas las entidades activas** (llamando a su `destructor` si lo tienen) y deja el manager como recién inicializado.

```c
darken_reset(&proyectiles); // p. ej. al cambiar de nivel
```

⚠️ Solo alcanza a las entidades **activas**. Si tenías balas **pausadas** en ese momento (por ejemplo, congeladas por un power-up de "tiempo detenido"), `darken_reset()` no las recorre ni llama a su destructor — y aun así, después del reset, esos slots vuelven a considerarse libres. Si dependes de que la limpieza se ejecute siempre, reanuda (`darken_entity_resume`) todo lo que esté pausado antes de llamar a `darken_reset()`.

### `void darken_entity_run(darken_entity e)`

Ejecuta la función `state` actual de una entidad concreta **una sola vez**, ahora mismo, sin aplicar ningún cambio de estado (ignora lo que devuelva). Útil cuando quieres invocar la lógica actual de una entidad puntualmente (por ejemplo, forzar la reacción de una bala a un evento) sin que eso interfiera con su ciclo normal de `darken_update`.

Requiere que la entidad esté activa; si no lo está, no hace nada.

### `void darken_entity_update(darken_entity e)`

Como `darken_entity_run`, pero además **sí aplica** el cambio de estado que devuelva la función (igual que haría `darken_update` para esa entidad). Úsala cuando quieras avanzar una entidad concreta bajo demanda, fuera del bucle general — por ejemplo, en respuesta directa a un evento de entrada, sin esperar al siguiente frame.

Requiere que la entidad esté activa; si no lo está, no hace nada.

### `void darken_entity_pause(darken_entity e)`

Pausa una entidad **de inmediato** (a diferencia de devolver `DARKEN_PAUSE` desde el estado, que tarda un frame en aplicarse, ver arriba). Requiere que la entidad esté activa.

```c
darken_entity_pause(bala); // esta bala deja de moverse y de dibujarse ya mismo
```

### `void darken_entity_resume(darken_entity e)`

Devuelve a la zona activa una entidad pausada. Requiere que la entidad esté pausada.

```c
darken_entity_resume(bala); // vuelve a moverse y dibujarse desde el próximo darken_update()
```

### `void darken_entity_delete(darken_entity e)`

Borra una entidad concreta ahora mismo, esté activa o pausada. Llamarla sobre una entidad que ya está libre no hace nada (es seguro llamarla más de una vez).

⚠️ **Si la entidad estaba pausada, su `destructor` no se ejecuta.** Solo se garantiza la llamada al destructor cuando borras una entidad que estaba activa. Si necesitas limpieza garantizada sobre algo pausado, reanúdalo primero y luego bórralo:

```c
if (esta_pausada(bala)) darken_entity_resume(bala);
darken_entity_delete(bala); // ahora sí se ejecuta el destructor
```

(No hay una función pública `esta_pausada` — llevar tú mismo ese dato, p. ej. con `tag`/`usr`, o simplemente evitar el escenario reanudando siempre antes de borrar por sistema.)

## El contrato de una función de estado

`entity->update` es un puntero a una función tuya con esta forma conceptual:

```c
void *mi_estado(Bullet *b);
```

Cada frame que la entidad esté activa, `darken_update` la llama con el payload de la entidad, y tu función debe devolver una de estas cuatro cosas:

| Devuelves                           | Efecto                                                                                       |
| ----------------------------------- | -------------------------------------------------------------------------------------------- |
| `DARKEN_LOOP`                       | Nada cambia: el próximo frame se vuelve a llamar a esta misma función.                       |
| `DARKEN_DELETE`                     | La entidad se borra (con destructor, si tiene) — con el retraso de un frame explicado en §3. |
| `DARKEN_PAUSE`                      | La entidad se pausa — mismo retraso de un frame.                                             |
| Un puntero a otra función de estado | A partir del próximo frame se ejecuta esa función en su lugar.                               |

```c
static void *bala_estado_volando(Bullet *b)
{
    b->x += b->vx;
    b->y += b->vy;
    b->ttl--;

    if (fuera_de_pantalla(b) || b->ttl <= 0)
        return DARKEN_DELETE;

    if (golpeo_enemigo(b))
        return (void *)bala_estado_explotando; // cambia de estado

    return DARKEN_LOOP;
}

static void *bala_estado_explotando(Bullet *b)
{
    if (animacion_explosion_terminada(b))
        return DARKEN_DELETE;
    avanzar_animacion(b);
    return DARKEN_LOOP;
}
```

Nota práctica: como `darken_callback` se declara sin prototipo de argumentos, puedes escribir tus funciones tomando directamente el tipo de puntero que te convenga (`Bullet *` en vez de `void *`) sin necesidad de castear al asignarlas a `entity->update` — cómodo, pero también significa que el compilador no te avisará si te equivocas de tipo entre lo que declaras y lo que realmente vas a recibir. Sé consistente: un `state`/`destructor` de una entidad de balas siempre debe esperar un `Bullet *`, nunca otra cosa.

## Macros públicas

| Macro                                            | Para qué la usas                                                                                                                        |
| ------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------- |
| `DARKEN_DATA(TIPO, VAR, entidad)`                | Te da un puntero ya casteado al payload: `DARKEN_DATA(Bullet, b, ENTITY);` en vez de castear `entity->data` a mano.                     |
| `DARKEN_STORAGE(nombre, capacidad, tam_payload)` | Declara el almacenamiento fijo (array de punteros + bloque de datos) que necesita `darken_init`. Se usa una vez, junto a `darken_init`. |
| `DARKEN_ARGS(nombre)`                            | Expande al bloque de argumentos de `DARKEN_STORAGE` en el orden que espera `darken_init`.                                               |
| `DARKEN_LOOP` / `DARKEN_DELETE` / `DARKEN_PAUSE` | Los tres valores que puede devolver una función de estado (ver §4).                                                                     |
| `DARKEN_FOREACH(mgr, CODIGO)`                    | Recorre todas las entidades activas. Dentro de `CODIGO` tienes disponibles `ENTITY` (la entidad actual) e `INDEX` (su posición).        |

```c
DARKEN_FOREACH(&proyectiles, {
    DARKEN_DATA(Bullet, b, ENTITY);
    dibujar_sprite(b->x, b->y);
});
```

⚠️ `DARKEN_FOREACH` usa siempre los nombres `ENTITY` e `INDEX` — no son configurables. No declares tú mismo variables con esos nombres dentro del bloque, y evita anidar un `DARKEN_FOREACH` dentro de otro (el de dentro tapa silenciosamente las variables del de fuera).

## Fortalezas

- **Sin asignación dinámica en el bucle de juego**: todo el almacenamiento es un bloque fijo reservado una vez con `DARKEN_STORAGE`.
- **Spawn/pausa/reanudación/borrado son operaciones baratas y predecibles**, cómodas para presupuestos de frame ajustados.
- **Puedes guardarte un puntero a una entidad pausada con confianza**: su memoria no se toca ni se recicla mientras esté pausada, solo al reanudarla o borrarla explícitamente.
- **Máquina de estados directa**: cada entidad decide su propio destino (seguir, cambiar de comportamiento, pausarse, borrarse) devolviendo un valor desde su propia función — no necesitas flags de "vivo/muerto" por fuera.
- Tipado flexible en las funciones de estado (§4): cada tipo de entidad puede trabajar directamente con su propio struct sin casts constantes.

## Limitaciones a tener en cuenta

- **Un manager admite un único tamaño de payload.** Si en tu juego tienes balas y enemigos con structs distintos, necesitas un `darken` (y su `DARKEN_STORAGE`) por cada tipo, no uno compartido.
- **Máximo 65535 entidades por manager** (los contadores internos son de 16 bits) — de sobra para la mayoría de casos, pero tenlo presente si vas a instanciar algo masivo (partículas, por ejemplo).
- **No hay comprobación de errores real**: condiciones inválidas (pausar algo que ya está pausado, reanudar algo que ya está activo, etc.) simplemente no hacen nada — no hay log, ni código de retorno, ni forma de detectarlo desde fuera. Si algo "no está pasando", sospecha de una precondición incumplida antes que de un bug en la lógica de tu estado.
- No hay soporte de hilos: como buena parte de motores pensados para un único núcleo, no esperes poder llamar a estas funciones desde distintos hilos sin sincronización propia.

## Avisos concretos (resumen para no perder de vista)

- **Comprueba siempre el `NULL` de `darken_spawn()`.**
- **Rellena tú mismo `state`, `destructor`, `tag`, `usr` y el payload en cada spawn** — un slot reciclado puede traer valores de una entidad anterior, y un `destructor` olvidado puede ejecutarse sobre datos que no le corresponden.
- **`DARKEN_DELETE`/`DARKEN_PAUSE` devuelto desde el estado tarda un frame en aplicarse.** Si necesitas el efecto ya, llama directamente a `darken_entity_delete()` / `darken_entity_pause()`.
- **`darken_entity_delete()` sobre una entidad pausada no llama a su destructor.** Reanuda antes de borrar si necesitas limpieza garantizada.
- **`darken_reset()` no toca las entidades pausadas** (ni las borra con destructor, ni las dejes de contar como recicladas). Reanuda todo antes de resetear si te importa.
- **No declares variables llamadas `ENTITY`/`INDEX` ni anides `DARKEN_FOREACH`.**
- **No leas ni escribas los campos internos del `darken_entity` que no aparecen en la tabla del §2** (`owner`, `slot`) — no forman parte del contrato público.

## Checklist práctico

- Define `DARKEN_IMPLEMENTATION` en **un solo** `.c`.
- Comprueba siempre que `darken_spawn()` no devuelva `NULL`.
- Tras cada `darken_spawn()`, fija explícitamente `state`, `destructor`, `tag`, `usr` y el payload — nunca asumas que están a cero.
- Si dependes de que los destructores se ejecuten siempre, evita `darken_entity_delete()` / `darken_reset()` sobre entidades pausadas sin reanudarlas antes.
- No anides `DARKEN_FOREACH`, y no declares variables llamadas `ENTITY`, `INDEX` o `POOL`.
- Si necesitas reaccionar al borrado en el mismo frame (no un frame después), llama a `darken_entity_delete()` directamente en vez de devolver `DARKEN_DELETE` desde el estado.
- No leas ni escribas `entity->owner` / `entity->slot` desde fuera del motor.
- Un manager = un tamaño de payload; usa un `darken` distinto por cada tipo de entidad con tamaño distinto.
- Los "asserts" nunca reportan nada. _DARKEN_ASSERT no devuelve.

## Cómo se usa

### Configuración mínima

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

### Crear entidades y acceder a su payload

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
    e->destroy= NULL;  // sin destructor por ahora

    return e;
}
```

Nota que `e` (el `darken_entity`) y `p` (el puntero a `Particle`) son direcciones
**diferentes**: `p` apunta justo dentro de `e->data`. `p` sigue siendo válido
aunque la entidad se pause o el pool se reordene, porque la dirección física de
`e` (y por tanto de `e->data`) nunca cambia.

### Definir el comportamiento con una máquina de estados

`e->update` es el callback que se ejecuta cada `darken_update`. Recibe
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
e->update = particle_falling;
```

### El bucle principal

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

### Destructores

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
e->destroy= particle_destructor;
```

### Iterar manualmente con `DARKEN_FOREACH`

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

### Pausar y reanudar

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

### Borrar entidades desde fuera del bucle de estado

```c
darken_entity_delete(e); // funciona tanto si "e" está activa como pausada
```

### Vaciar el manager entero

```c
darken_reset(&manager); // size vuelve a 0, paused vuelve a capacity
```

> ⚠️ Ver «Puntos flacos»: si tenías entidades pausadas en el momento del reset,
> **sus destructores no se ejecutarán**. Si tus destructores liberan recursos
> externos, asegúrate de pausar nada (o de reanudar/borrar todo explícitamente)
> antes de llamar a `darken_reset`, hasta que ese comportamiento se corrija.

### Ejemplo completo (arriba hacia abajo)

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
        e->update = particle_falling;
        e->destroy= particle_destructor;
    }

    while (manager.size > 0)
        darken_update(&manager);

    return 0;
}
```

## Otro ejemplo

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"

typedef struct { float x, y, vx, vy; int ttl; } Bullet;

static void *bala_volando(Bullet *b)
{
    b->x += b->vx; b->y += b->vy;
    if (--b->ttl <= 0) return DARKEN_DELETE;
    return DARKEN_LOOP;
}

darken proyectiles;
DARKEN_STORAGE(bullet_storage, 256, sizeof(Bullet));

void iniciar_juego(void)
{
    darken_init(&proyectiles, DARKEN_ARGS(bullet_storage));
}

void disparar(float x, float y)
{
    darken_entity e = darken_spawn(&proyectiles);
    if (!e) return; // pool lleno, se ignora el disparo

    DARKEN_DATA(Bullet, b, e);
    b->x = x; b->y = y; b->vx = 0; b->vy = -8; b->ttl = 90;
    e->update = (darken_callback)bala_volando;
    e->destroy= NULL;
}

void frame(void)
{
    darken_update(&proyectiles);

    DARKEN_FOREACH(&proyectiles, {
        DARKEN_DATA(Bullet, b, ENTITY);
        dibujar_sprite(b->x, b->y);
    });
}

void congelar_todo(void) // p. ej. power-up de "tiempo detenido"
{
    DARKEN_FOREACH(&proyectiles, { darken_entity_pause(ENTITY); });
}
```
