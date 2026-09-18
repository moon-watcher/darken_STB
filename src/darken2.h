/**
 * darken2.h — MOCKUP de diseño, no producción todavía.
 *
 * Requiere, definidos por el usuario ANTES de este #include:
 *
 *   DARKEN_ZONES   -- número de zonas "de usuario" (>0). La zona libre NO cuenta
 *                     aquí: vive implícita justo después de la última zona de
 *                     usuario, hasta `capacity`.
 *   Z_...          -- un enum/defines con las zonas de usuario, valores 0..DARKEN_ZONES-1,
 *                     ordenadas de izquierda a derecha tal y como se quiera que
 *                     queden dentro de pool[].
 *
 * También requiere que uint8_t/uint16_t/uint32_t/uintptr_t sean visibles ya
 * (igual que darken.h original: no incluye <stdint.h> por sí mismo).
 *
 * Pendiente de esta iteración (a propósito, fuera de alcance del mockup):
 *   - Macros constructoras de storage (ALLOC/DECLARE/INIT/BIND) -- pausadas.
 *   - destroy() -- ya no lo gestiona el motor, es responsabilidad del usuario.
 *   - Validación en runtime de zonas/bounds -- cero checks, como el original.
 *
 * El motor NO interpreta el valor de retorno de update(): solo ofrece el
 * comando darken_entity_set_zone(data, zona) para moverla. Qué significa lo
 * que devuelve update() -- y cuándo llamar a ese comando -- es responsabilidad
 * de cada aplicación (ver GAME_DISPATCH en user_demo.c).
 */

#pragma once

_Static_assert(DARKEN_ZONES > 0, "darken2.h: DARKEN_ZONES debe estar definido y ser > 0 antes de este include");

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
    uint16_t bounds[DARKEN_ZONES]; // bounds[z] = frontera derecha de la zona z (usuario)
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
 * Zona libre implícita
 * ============================================================================ */

// La libre no tiene índice propio en el enum del usuario ni entrada dedicada en
// bounds[]: es, por definición, la zona DARKEN_ZONES (la que va justo después de
// la última zona de usuario). DARKEN_FREE_ZONE es solo azúcar de lectura.
#define DARKEN_FREE_ZONE DARKEN_ZONES

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
    return zone < DARKEN_ZONES ? ctx->bounds[zone] : ctx->capacity;
}

// Devuelve 0..DARKEN_ZONES-1 para zonas de usuario, o DARKEN_ZONES (== DARKEN_FREE_ZONE)
// si la entidad está en la libre.
static inline int darken_entity_zone(darken_entity_t entity)
{
    darken_t *ctx = entity->owner;
    int z = 0;
    while (z < DARKEN_ZONES && entity->slot >= ctx->bounds[z])
        z++;
    return z;
}

// Recupera el handle a partir de un puntero a su payload (lo único que un
// update() recibe). Mismo idioma offsetof-via-null-pointer que darken.h original.
#define DARKEN_ENTITY(DATA) ((darken_entity_t)((uint8_t *)(DATA) - (uintptr_t)&((darken_entity_t)0)->data))

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
// `target` (0..DARKEN_ZONES-1 para zonas de usuario, DARKEN_FREE_ZONE para la
// libre). No-op si ya está ahí.
//
// El motor NO decide cuándo llamar a esto ni qué significa el valor que
// devuelve un update() -- eso (la relación entre "lo que retorna la entidad" y
// "a qué zona se mueve") lo establece cada aplicación en su propia
// implementación, análogo a _DARKEN_UPDATE en darken.h original pero ahora
// fuera del motor. Ver GAME_DISPATCH en user_demo.c para un ejemplo.
static inline void darken_entity_set_zone(void *data, int target)
{
    _darken_move_zone(DARKEN_ENTITY(data), target);
}

/* ============================================================================
 * Alta de entidades
 * ============================================================================ */

// El usuario indica a qué zona nace la entidad. La cantera siempre es la libre.
#define DARKEN_SPAWN(CTX, ZONE) ({                                                \
    darken_t *_ctx = (CTX);                                                       \
    uint16_t _free_start = _ctx->bounds[DARKEN_ZONES - 1];                        \
    darken_entity_t _e = (_free_start < _ctx->capacity) ? _ctx->pool[_free_start] \
                                                        : (darken_entity_t)0;     \
    if (_e)                                                                       \
        _darken_move_zone(_e, (ZONE));                                            \
    _e;                                                                           \
})

/* ============================================================================
 * Iteración
 * ============================================================================ */

// Recorre UNA zona (0..DARKEN_ZONES, incluyendo la libre) en reversa -- igual que
// el DARKEN_FOREACH original, pero sobre la zona que se le indique explícitamente.
// Ya no hay una zona "activa" implícita: si el usuario quiere tickear varias
// zonas, llama a esto una vez por zona.
#define DARKEN_FOREACH_ZONE(CTX, ZONE, CODE)             \
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
// si se descarta la población actual a propósito.
static inline void darken_init(darken_t *ctx)
{
    for (int z = 0; z < DARKEN_ZONES; z++)
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

// Ya no llama a destroy() (responsabilidad del usuario, ver cabecera del archivo):
// resetear es solo devolver todas las fronteras a 0, sin swaps -- O(DARKEN_ZONES)
// en vez de O(n).
static inline void darken_reset(darken_t *ctx)
{
    for (int z = 0; z < DARKEN_ZONES; z++)
        ctx->bounds[z] = 0;
}