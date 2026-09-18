// user_demo.c — implementación de usuario sobre darken2.h
//
// Escenario de juguete: 3 zonas de usuario + la libre implícita.
//
//   Z_ACTIVE     -- se tickea cada frame como máquina de estados
//   Z_STUNNED    -- también se tickea (para poder contar el tiempo de aturdido
//                   y decidir cuándo volver a Z_ACTIVE), pero es una zona
//                   aparte para poder filtrarla en otros sitios (renderizado,
//                   colisiones, etc. -- no se ve en este demo)
//   Z_OFFSCREEN  -- NUNCA se tickea: equivalente al "paused" del Darken original,
//                   pero aquí es una decisión explícita del usuario, no un
//                   nombre reservado del motor
//
// Todo esto (cuántas zonas hay, cuáles se actualizan y cómo) es responsabilidad
// del usuario: darken2.h no impone ni un darken_update() global ni nombres de
// zona especiales más allá de la libre.

#include <stdint.h>


#define DARKEN_ZONES 3
enum
{
    Z_ACTIVE = 0,
    Z_STUNNED = 1,
    Z_OFFSCREEN = 2,
};

#include "darken2.h"

/* ============================================================================
 * Payload de usuario
 * ============================================================================ */

struct enemy
{
    int x;
    int stun_frames_left;
};

/* ============================================================================
 * El propio "_DARKEN_UPDATE" del usuario
 * ============================================================================
 * darken2.h ya no impone ninguna convención sobre lo que devuelve update():
 * solo ofrece darken_entity_set_zone(data, zona) como comando de movimiento.
 * Aquí, en la aplicación, decidimos que 0..DARKEN_FREE_ZONE se interpreta como
 * "zona destino" y cualquier otro valor como "nuevo puntero a callback" --
 * exactamente lo que hacía _DARKEN_UPDATE en darken.h original, solo que ahora
 * es una convención de ESTA app, no del motor. Otra app sobre el mismo
 * darken2.h podría inventarse una completamente distinta.
 */
#define GAME_DISPATCH()                                                   \
    do                                                                    \
    {                                                                     \
        darken_state_t _next = _entity->update(_entity->data);            \
        if ((uintptr_t)_next <= DARKEN_FREE_ZONE)                         \
            darken_entity_set_zone(_entity->data, (int)(uintptr_t)_next); \
        else                                                              \
            _entity->update = _next;                                      \
    } while (0)

/* ============================================================================
 * El propio "darken_update()" del usuario
 * ============================================================================
 * darken2.h ya no ofrece un darken_update() de propósito general (con N zonas
 * arbitrarias, el motor no puede saber cuáles hay que tickear). El usuario lo
 * compone aquí a partir de DARKEN_FOREACH_ZONE + GAME_DISPATCH, zona a zona.
 */
static void game_update(darken_t *ctx)
{
    DARKEN_FOREACH_ZONE(ctx, Z_ACTIVE, GAME_DISPATCH());
    DARKEN_FOREACH_ZONE(ctx, Z_STUNNED, GAME_DISPATCH());
    // Z_OFFSCREEN queda fuera a propósito: dormida hasta que algo externo
    // (colisión, evento de gameplay, etc.) la mueva explícitamente.
}

/* ============================================================================
 * Callbacks de estado (mismo idioma que darken.h original: devolver un
 * darken_state_t; aquí el valor se interpreta como índice de zona destino)
 * ============================================================================ */

static void *enemy_stunned(void *data);

static void *enemy_walk(void *data)
{
    struct enemy *e = (struct enemy *)data;
    e->x++;

    kprintf("  [ACTIVE] enemy slot=%u x=%d", DARKEN_ENTITY(data)->slot, e->x);

    if (e->x >= 3)
    {
        darken_entity_t self = DARKEN_ENTITY(data);
        kprintf("  [ACTIVE] slot=%u se aturde", self->slot);
        e->stun_frames_left = 2;
        // El valor de retorno SOLO mueve de zona. Si además queremos que la
        // próxima llamada ejecute otra lógica, hay que asignar update() a
        // mano, aquí mismo -- igual que ya se hace para cambiar de estado sin
        // cambiar de zona.
        self->update = enemy_stunned;
        return /* (void *)(uintptr_t) */ Z_STUNNED;
    }

    return /* (void *)(uintptr_t) */ Z_ACTIVE; // "continue", ya sin sentinela especial
}

static void *enemy_stunned(void *data)
{
    struct enemy *e = (struct enemy *)data;
    darken_entity_t self = DARKEN_ENTITY(data);

    e->stun_frames_left--;
    kprintf("  [STUNNED] slot=%u frames_left=%d", self->slot, e->stun_frames_left);

    if (e->stun_frames_left <= 0)
    {
        kprintf("  [STUNNED] slot=%u se recupera", self->slot);
        self->update = enemy_walk;
        return /* (void *)(uintptr_t) */ Z_ACTIVE;
    }

    return /* (void *)(uintptr_t) */ Z_STUNNED;
}

static void *enemy_die_next_tick(void *data)
{
    darken_entity_t self = DARKEN_ENTITY(data);
    kprintf("  [ACTIVE] slot=%u se autodestruye -> libre", self->slot);
    return /* (void *)(uintptr_t) */ DARKEN_FREE_ZONE;
}

/* ============================================================================
 * main: storage manual (los constructores ALLOC/DECLARE están pausados en
 * esta iteración), unos spawns, y unos cuantos frames de game_update()
 * ============================================================================ */

#define CAPACITY 4

int darken2_user_demo_main(void)
{
    uint16_t stride = _DARKEN_ENTITY_STRIDE(sizeof(struct enemy));

    darken_entity_t pool_storage[CAPACITY];
    static uint8_t entity_storage[CAPACITY * _DARKEN_ENTITY_STRIDE(sizeof(struct enemy))]
        __attribute__((aligned(_DARKEN_ENTITY_ALIGN)));

    darken_t ctx = {
        .pool = pool_storage,
        .storage = entity_storage,
        .capacity = CAPACITY,
        .stride = stride,
    };

    darken_init(&ctx);

    darken_entity_t a = DARKEN_SPAWN(&ctx, Z_ACTIVE);
    darken_entity_t b = DARKEN_SPAWN(&ctx, Z_ACTIVE);
    darken_entity_t c = DARKEN_SPAWN(&ctx, Z_OFFSCREEN); // nace directamente dormido

    DARKEN_DATA(struct enemy, da, a);
    da->x = 0;
    da->stun_frames_left = 0;
    a->update = enemy_walk;

    DARKEN_DATA(struct enemy, db, b);
    db->x = 0;
    db->stun_frames_left = 0;
    b->update = enemy_die_next_tick;

    DARKEN_DATA(struct enemy, dc, c);
    dc->x = 0;
    dc->stun_frames_left = 0;
    c->update = enemy_walk; // nunca se llamará mientras siga en Z_OFFSCREEN

    kprintf("slots: a=%u b=%u c=%u (spawn order)", a->slot, b->slot, c->slot);

    for (int frame = 1; frame <= 5; frame++)
    {
        kprintf("-- frame %d -- (active=%d..%d, stunned=%d..%d, offscreen=%d..%d, free=%d..%d)",
               frame,
               0, ctx.bounds[Z_ACTIVE] - 1,
               ctx.bounds[Z_ACTIVE], ctx.bounds[Z_STUNNED] - 1,
               ctx.bounds[Z_STUNNED], ctx.bounds[Z_OFFSCREEN] - 1,
               ctx.bounds[Z_OFFSCREEN], ctx.capacity - 1);
        game_update(&ctx);
    }

    // Comprobación manual, fuera de un update(): mover `c` de vuelta a activo
    // sin pasar por ningún callback ni por GAME_DISPATCH -- demuestra
    // darken_entity_set_zone() como comando público de la app, invocable desde
    // cualquier sitio (aquí, desde fuera del bucle de update), no solo desde
    // dentro de la convención de retorno que definimos en GAME_DISPATCH.
    kprintf("-- despertando a c manualmente --");
    darken_entity_set_zone(c->data, Z_ACTIVE);
    kprintf("zona de c ahora: %d (Z_ACTIVE=%d)", darken_entity_zone(c), Z_ACTIVE);

    return 0;
}