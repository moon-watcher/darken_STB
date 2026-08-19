# Apéndices Darken 2.0

## 🚀 Apéndice A: Hola Mundo en 30 líneas

El mínimo programa que crea una entidad, le pone un estado, y la hace contar hasta 5 antes de suicidarse.

```c
#define DARKEN_IMPLEMENTATION
#include "darken.h"
#include <stdio.h>

typedef struct { int contador; } Datos;

void *contar(void *data) {
    Datos *d = data;
    printf("Contador: %d\n", d->contador++);
    return d->contador > 5 ? DE_STATE_DELETE : DE_STATE_LOOP;
}

int main(void) {
    DE_MANAGER_STORAGE(m, 10, sizeof(Datos));
    de_manager manager;
    de_manager_init(&manager, DE_MANAGER_ARGS(m));

    de_entity e = de_manager_new(&manager);
    ((Datos *)e->data)->contador = 1;
    e->state = contar;

    while (manager.size > 0) {
        de_manager_update(&manager);
    }

    printf("Se borró solita. Magia.\n");
    return 0;
}
```

Compilá:
```bash
gcc -std=gnu99 hola.c -o hola && ./hola
```

**Salida esperada:**
```
Contador: 1
Contador: 2
Contador: 3
Contador: 4
Contador: 5
Contador: 6
Se borró solita. Magia.
```

Eso es todo. Una entidad, un estado, un loop. El resto son detalles.

---

## 💀 Apéndice B: Errores comunes y cómo no romper todo

### 1. "Me olvidé del `#define DARKEN_IMPLEMENTATION`"
Sin esto, el compilador te tira `undefined reference` en todas las funciones (`de_manager_update`, `de_entity_delete`, etc.). La mitad del header son macros y tipos; la otra mitad (las funciones con cuerpo) solo aparece si definís esa macro **antes** del `#include`, en **exactamente un** archivo `.c`.

```c
/* Bien */
#define DARKEN_IMPLEMENTATION
#include "darken.h"

/* Mal: lo pusiste en el .h o en ningún lado */
```

### 2. "`DE_MANAGER_FOREACH` + `de_entity_delete` = 💥"
El macro `DE_MANAGER_FOREACH` itera con un `while (INDEX--)` sobre el pool. Si adentro llamás `de_entity_delete(ENTITY)`, el manager hace un swap con la última entidad activa y achica `size`. Pero el foreach ya había cacheado `INDEX` y `POOL` al inicio. Resultado: podés saltearte entidades o visitar la misma dos veces.

**Regla de oro:** Nunca borres adentro de un `DE_MANAGER_FOREACH`. Si querés matar algo manualmente, hacelo **después** del foreach, o usá `de_manager_update()` y que el propio `state` devuelva `DE_STATE_DELETE`.

### 3. "Creo 50 entidades y de repente `de_manager_new` devuelve `NULL`"
El manager tiene tres zonas: `[activas][libres][pausadas]`. `de_manager_new` solo coge del medio. Si pausaste 40 entidades y tenés capacidad 50, solo te quedan 10 slots libres. Si `size == paused`, el antro está lleno.

**Fix:** Revisá `manager.paused - manager.size` para ver cuántos slots libres te quedan. Si pausás mucho, eventualmente te quedás sin lugar para crear cosas nuevas.

### 4. "Guardo un puntero a `entity->data` y después la entidad explota"
Los punteros a `data[]` son **estables solo para entidades pausadas**. Si la entidad está activa y `de_entity_delete` o `de_manager_update` la destruye, ese puntero que guardaste en tu `de_system` o en otra entidad ahora apunta a un cadáver (o peor, a otra entidad que ocupó ese slot).

**Fix:** Si un `de_system` necesita apuntar a datos de forma segura, asegurate de que la entidad esté **pausada** (`de_entity_pause`). O, mejor, regenerá los punteros del system cada frame desde el manager.

### 5. "`de_system_remove` no encuentra nada"
`de_system_remove(sys, ptr)` busca un grupo cuyo **primer** puntero (`pool[i]`) sea igual a `ptr`. Si guardaste `(pos, vel)` y querés borrar buscando por `vel`, no va a funcionar. Buscá siempre por el primer elemento del grupo.

### 6. "Mi estado devuelve `0` y la entidad no se borra"
`0` es `DE_STATE_DELETE`, pero si tu función devuelve `NULL` (que es `0`) sin querer, la entidad muere. Si tu estado devuelve un puntero a otra función, asegurate de que no sea `NULL`, `1` ni `2`, porque esos son los valores mágicos del engine.

### 7. "`de_manager_new` me da entidades con basura en `data[]`"
Darken **no inicializa tu payload**. Te da la entidad con `state = DE_STATE_DELETE`, `destructor = 0`, `tag = 0`, pero el `data[]` tiene lo que quedó del anterior inquilino. Si no inicializás tus campos, vas a tener valores random.

### 8. "Uso `uint16_t` para todo y se me desborda el índice"
El `slot`, `size`, `capacity`, etc. son `uint16_t`. Si hacés `slot - 1` cuando `slot` es `0`, te vas al carajo (underflow a 65535). El engine ya maneja esto internamente, pero si tocas índices a mano, cuidado con los bordes.

### 9. "Pauso una entidad y sigue apareciendo en mi `DE_MANAGER_FOREACH`"
`DE_MANAGER_FOREACH` solo recorre la zona activa `[0, size)`. Si pausaste algo, se movió a `[paused, capacity)`. No debería aparecer... a menos que estés iterando a mano sobre `pool[]` sin fijarte en los límites.

### 10. "Mi `de_system` tiene `capacity` raro"
`de_system_init` recibe `capacity_groups` (cuántos grupos querés) y `params` (cuántos punteros por grupo). La capacidad **total** de punteros es `groups * params`. Si reservás `DE_SYSTEM_STORAGE(sys, 10, 3)`, tenés lugar para 10 grupos de 3 punteros, no para 30 grupos.

---

**Resumen para no olvidar:**

| ✅ Hacé esto | ❌ No hagas esto |
|---|---|
| `#define DARKEN_IMPLEMENTATION` en un solo `.c` | Olvidarte y llorar con linker errors |
| Borrar entidades devolviendo `DE_STATE_DELETE` desde el `state` | Llamar `de_entity_delete` adentro de un `DE_MANAGER_FOREACH` |
| Pausar entidades si querés punteros estables a su `data` | Guardar punteros a `data` de entidades activas |
| Inicializar tu payload después de `de_manager_new` | Asumir que `data[]` viene en cero |
| Usar `de_system_remove` buscando por el **primer** puntero del grupo | Buscar por el segundo, tercero, etc. |

Con estas reglas, Darken es prácticamente imposible de romper. O bueno, más difícil. Un poco. 🎮
