/**
 * darken2.h — MOCKUP de diseño, no producción todavía.
 *
 * Requiere que uint8_t/uint16_t/uint32_t/uintptr_t sean visibles ya (igual que
 * darken.h original: no incluye ningún header por sí mismo, ni siquiera
 * <stdint.h>). Bajo SGDK, <genesis.h> ya expone uint8_t/uint16_t/uint32_t
 * (los mapea a sus propios u8/u16/u32); solo falta uintptr_t, que SGDK no
 * define -- como el m68k del Genesis es de 32 bits, un simple
 * `typedef u32 uintptr_t;` antes de este include es exacto y evita tirar de
 * cualquier header ajeno a SGDK solo por ese tipo.
 *
 * A diferencia de la primera iteración de este mockup, YA NO hace falta
 * `#define DARKEN_ZONES N` antes del include: el número de zonas es un dato de
 * cada darken_t (campo `zones`), fijado al construirlo (DARKEN_DECLARE/ALLOC),
 * no una constante global de compilación. Dos ctx distintos en el mismo
 * programa pueden tener cada uno su propio número de zonas.
 *
 * DARKEN_MAX_ZONES sigue siendo una constante de compilación (por defecto 8,
 * redefinible antes del include): es solo el tamaño físico reservado para
 * bounds[] dentro de darken_t; `zones` (en tiempo de ejecución, por instancia)
 * dice cuántas de esas posiciones están realmente en uso para un ctx dado.
 *
 * Las zonas de usuario van de 0 a zones-1, en el orden en que el usuario las
 * declare. La zona libre no tiene índice propio: es siempre `zones` para ese
 * ctx (ver DARKEN_FREE_ZONE), justo después de la última zona de usuario,
 * hasta `capacity`.
 *
 * El motor NO interpreta el valor de retorno de update(): solo ofrece el
 * comando darken_entity_set_zone(data, zona) para moverla. Qué significa lo
 * que devuelve update() -- y cuándo llamar a ese comando -- es responsabilidad
 * de cada aplicación (ver GAME_DISPATCH en user_demo.c). Ahí, las zonas hacen
 * de sentinels (como DARKEN_CONTINUE/PAUSE/DELETE en darken.h original): un
 * valor de retorno pequeño (0..zona_libre) es una zona destino, cualquier otro
 * valor es un puntero a la siguiente función de estado.
 *
 * Pendiente de esta iteración (a propósito, fuera de alcance del mockup):
 *   - destroy() -- ya no lo gestiona el motor, es responsabilidad del usuario.
 *   - Validación en runtime de zonas/bounds -- cero checks, como el original.
 */

#pragma once

#ifndef DARKEN_MAX_ZONES
#define DARKEN_MAX_ZONES 8
#endif

_Static_assert(DARKEN_MAX_ZONES > 0 && DARKEN_MAX_ZONES <= 255, "darken2.h: DARKEN_MAX_ZONES fuera de rango");

// Callback de update: recibe solo el payload (data), nunca el handle directamente.
// Si hace falta el handle dentro del callback, se recupera con DARKEN_ENTITY(data).
typedef void *(*darken_state_t)();

typedef struct darken_entity_t *darken_entity_t;

typedef struct darken_t
{
    darken_entity_t *pool;
    uint8_t *storage;
    uint16_t capacity;
    uint16_t stride;
    uint16_t zones;                    // cuántas zonas de usuario tiene ESTE ctx (1..DARKEN_MAX_ZONES)
    uint16_t bounds[DARKEN_MAX_ZONES]; // solo bounds[0..zones-1] son significativos
} darken_t;

struct darken_entity_t
{
    uint16_t slot;         // Private: índice en pool[]
    uint16_t usr;          // User-defined
    darken_state_t update; // User-defined update callback
    uint32_t tag;          // User-defined
    darken_t *owner;       // Private: ctx propietario
    uint8_t data[];        // Payload
};

/* ============================================================================
 * Alineación / stride (igual que en darken.h original)
 * ============================================================================ */

#define _DARKEN_ALIGN(X, A) (((X) + (uintptr_t)(A) - 1) & ~((uintptr_t)(A) - 1))
#define _DARKEN_POOL_ALIGN __alignof__(darken_entity_t)
#define _DARKEN_ENTITY_ALIGN __alignof__(struct darken_entity_t)
#define _DARKEN_ENTITY_STRIDE(PAYLOAD) _DARKEN_ALIGN(sizeof(struct darken_entity_t) + (PAYLOAD), _DARKEN_ENTITY_ALIGN)

/* ============================================================================
 * Storage: construcción de un darken_t y su almacenamiento (mismo patrón que
 * darken.h original, con ZONES como parámetro adicional junto a CAPACITY)
 * ============================================================================ */

// Asignación dinámica: usar con malloc/calloc o un allocador propio.
//     darken_t m = DARKEN_ALLOC(malloc, 32, 4, sizeof(struct game_obj));
//     if (!m.pool || !m.storage) return;
//     darken_init(&m);
//     ...
//     DARKEN_FREE(free, &m);
//
// DARKEN_ALLOC() no gestiona fallos de asignación ni limpieza parcial.
// CAPACITY debe cumplir 1 <= CAPACITY <= 65535, ZONES debe cumplir
// 1 <= ZONES <= DARKEN_MAX_ZONES, y el stride calculado debe caber en uint16_t.
// Al ser una expresión (no una declaración), DARKEN_ALLOC() no puede forzar
// estas restricciones con _Static_assert -- responsabilidad del llamador.
#define DARKEN_ALLOC(ALLOC, CAPACITY, ZONES, PAYLOAD)                    \
    (darken_t)                                                           \
    {                                                                    \
        .pool = (ALLOC)((CAPACITY) * sizeof(darken_entity_t)),           \
        .storage = (ALLOC)((CAPACITY) * _DARKEN_ENTITY_STRIDE(PAYLOAD)), \
        .capacity = (CAPACITY),                                          \
        .stride = _DARKEN_ENTITY_STRIDE(PAYLOAD),                        \
        .zones = (ZONES),                                                \
    }

// Libera pool y storage reservados con DARKEN_ALLOC(). bounds[]/zones van
// embebidos en el propio darken_t: no hay nada más que liberar.
#define DARKEN_FREE(FREE, CTX)  \
    do                          \
    {                           \
        (FREE)((CTX)->pool);    \
        (FREE)((CTX)->storage); \
    } while (0)

// Declaración de almacenamiento estático (pila o global).
//     DARKEN_DECLARE(storage, 32, 4, sizeof(struct game_obj));
//     darken_t m = DARKEN_BIND(storage);
//     darken_init(&m);
//
// CAPACITY y ZONES se comprueban en tiempo de compilación via _Static_assert.
#define DARKEN_DECLARE(NAME, CAPACITY, ZONES, PAYLOAD)                                                            \
    struct                                                                                                        \
    {                                                                                                             \
        uint16_t capacity;                                                                                        \
        uint16_t stride;                                                                                          \
        uint16_t zones;                                                                                           \
        darken_entity_t pool[(CAPACITY)] __attribute__((aligned(_DARKEN_POOL_ALIGN)));                            \
        uint8_t data[(CAPACITY) * _DARKEN_ENTITY_STRIDE(PAYLOAD)] __attribute__((aligned(_DARKEN_ENTITY_ALIGN))); \
    } NAME = {                                                                                                    \
        .capacity = (CAPACITY),                                                                                   \
        .stride = _DARKEN_ENTITY_STRIDE(PAYLOAD),                                                                 \
        .zones = (ZONES),                                                                                         \
    }

// Inicialización estática/global con constantes de compilación. ZONES se pide
// explícito (no se puede derivar de STORAGE con sizeof() como capacity/stride,
// porque zones es un escalar, no el tamaño de un array) -- debe coincidir con
// el que se usó al declarar STORAGE.
#define DARKEN_INIT(STORAGE, ZONES)                                                            \
    (darken_t)                                                                                 \
    {                                                                                          \
        .pool = (STORAGE).pool,                                                                \
        .storage = (STORAGE).data,                                                             \
        .capacity = sizeof((STORAGE).pool) / sizeof(darken_entity_t),                          \
        .stride = sizeof((STORAGE).data) / (sizeof((STORAGE).pool) / sizeof(darken_entity_t)), \
        .zones = (ZONES),                                                                      \
    }

// Vinculación en tiempo de ejecución: locales, reasignación, cualquier contexto.
#define DARKEN_BIND(NAME)            \
    (darken_t)                       \
    {                                \
        .pool = (NAME).pool,         \
        .storage = (NAME).data,      \
        .capacity = (NAME).capacity, \
        .stride = (NAME).stride,     \
        .zones = (NAME).zones,       \
    }

/* ============================================================================
 * Núcleo: pertenencia a zona y movimiento entre zonas
 * ============================================================================ */

static inline void darken_swap(darken_entity_t pool[], uint16_t i, uint16_t j)
{
    if (i == j)
        return;

    darken_entity_t tmp = pool[i];
    pool[i] = pool[j];
    pool[j] = tmp;
    pool[i]->slot = i;
    pool[j]->slot = j;
}

static inline uint16_t _darken_zone_lo(darken_t *ctx, int zone)
{
    return zone > 0 ? ctx->bounds[zone - 1] : 0;
}

static inline uint16_t _darken_zone_hi(darken_t *ctx, int zone)
{
    return zone < ctx->zones ? ctx->bounds[zone] : ctx->capacity;
}

// Devuelve 0..zones-1 para zonas de usuario, o `zones` (== la libre para ese
// ctx) si la entidad está en la libre.
static inline int darken_entity_zone(darken_entity_t entity)
{
    darken_t *ctx = entity->owner;
    int z = 0;
    while (z < ctx->zones && entity->slot >= ctx->bounds[z])
        z++;
    return z;
}

// Recupera el handle a partir de un puntero a su payload (lo único que un
// update() recibe). Mismo idioma offsetof-via-null-pointer que darken.h original.
#define DARKEN_ENTITY(DATA) ((darken_entity_t)((uint8_t *)(DATA) - (uintptr_t)&((darken_entity_t)0)->data))

// El índice de la zona libre de un ctx concreto -- ya no es una constante
// global (cada ctx puede tener un `zones` distinto), así que hace falta
// resolverlo por instancia. DARKEN_FREE_ZONE(DATA) es la versión cómoda para
// usar dentro de un callback update(), que solo tiene `data` en la mano.
static inline int darken_free_zone(darken_t *ctx)
{
    return (int)ctx->zones;
}

#define DARKEN_FREE_ZONE(DATA) darken_free_zone(DARKEN_ENTITY(DATA)->owner)

// Privado: el movimiento real, sobre el handle. La API pública opera sobre
// `data` (ver darken_entity_set_zone) porque es lo único que un callback
// update() tiene en la mano.
static inline void _darken_move_zone(darken_entity_t entity, int target)
{
    darken_t *ctx = entity->owner;
    int cur = darken_entity_zone(entity);

    while (cur < target)
        darken_swap(ctx->pool, entity->slot, --ctx->bounds[cur++]);
    while (cur > target)
        darken_swap(ctx->pool, entity->slot, ctx->bounds[--cur]++);
}

// Único comando de movimiento que expone el motor. Mueve la entidad a la zona
// `target` (0..zones-1 para zonas de usuario de su ctx, o el valor de
// darken_free_zone()/DARKEN_FREE_ZONE(DATA) para la libre). No-op si ya está ahí.
//
// El motor NO decide cuándo llamar a esto ni qué significa el valor que
// devuelve un update() -- eso lo establece cada aplicación en su propia
// implementación (ver GAME_DISPATCH en user_demo.c).
static inline void darken_entity_set_zone(void *data, int target)
{
    _darken_move_zone(DARKEN_ENTITY(data), target);
}

/* ============================================================================
 * Alta de entidades
 * ============================================================================ */

// El usuario indica a qué zona nace la entidad. La cantera siempre es la libre.
#define DARKEN_SPAWN(CTX, ZONE) ({                                                     \
    darken_t *_ctx = (CTX);                                                            \
    uint16_t _free_start = _ctx->bounds[_ctx->zones - 1];                              \
    darken_entity_t _e = (_free_start < _ctx->capacity) ? _ctx->pool[_free_start] : 0; \
    if (_e)                                                                            \
        _darken_move_zone(_e, (ZONE));                                                 \
    _e;                                                                                \
})

/* ============================================================================
 * Iteración
 * ============================================================================ */

// Recorre UNA zona (0..zones, incluyendo la libre) en reversa sobre la zona que
// se le indique explícitamente.
// Ya no hay una zona "activa" implícita: si el usuario quiere tickear varias
// zonas, llama a esto una vez por zona.
#define DARKEN_FOREACH(CTX, ZONE, CODE)                  \
    do                                                   \
    {                                                    \
        darken_t *_ctx = (CTX);                          \
        int _zone = (ZONE);                              \
        uint16_t _lo = _darken_zone_lo(_ctx, _zone);     \
        uint16_t _hi = _darken_zone_hi(_ctx, _zone);     \
        uint16_t _index = _hi;                           \
        if (_index > _lo)                                \
        {                                                \
            darken_entity_t *_pool = _ctx->pool;         \
            while (_index-- > _lo)                       \
            {                                            \
                darken_entity_t _entity = _pool[_index]; \
                CODE;                                    \
            }                                            \
        }                                                \
    } while (0)

// Declara un puntero tipado al payload de una entidad.
#define DARKEN_DATA(TYPE, VAR, ENTITY) TYPE *VAR = (TYPE *)(ENTITY)->data;

/* ============================================================================
 * Ciclo de vida del ctx
 * ============================================================================ */

// Debe llamarse solo sobre storage sin usar / no inicializado, o tras darken_reset()
// si se descarta la población actual a propósito. Asume ctx->capacity, ->stride,
// ->zones y ->storage ya rellenos (por las macros de storage de arriba).
static inline void darken_init(darken_t *ctx)
{
    for (int z = 0; z < ctx->zones; z++)
        ctx->bounds[z] = 0;

    uint16_t i = ctx->capacity;
    uint8_t *storage = ctx->storage;

    while (i--)
    {
        ctx->pool[i] = (darken_entity_t)storage;
        ctx->pool[i]->owner = ctx;
        ctx->pool[i]->slot = i;

        storage += ctx->stride;
    }
}

// Ya no llama a destroy() (responsabilidad del usuario): resetear es solo
// devolver todas las fronteras a 0, sin swaps -- O(zones) en vez de O(n). No
// toca ->zones: el número de zonas de un ctx no cambia con reset, solo su
// población.
static inline void darken_reset(darken_t *ctx)
{
    for (int z = 0; z < ctx->zones; z++)
        ctx->bounds[z] = 0;
}