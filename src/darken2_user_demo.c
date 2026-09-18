// user_demo.c — implementación de usuario sobre darken2.h, para SGDK (Sega
// Mega Drive / Genesis).
//
// Solo se usan headers de SGDK: <genesis.h> da u8/u16/u32, uint8_t/uint16_t/
// uint32_t (los mapea a los suyos), kprintf() y MEM_alloc()/MEM_free(). SGDK
// no define uintptr_t -- como el m68k del Genesis es de 32 bits, un
// `typedef u32 uintptr_t;` es exacto y evita tirar de cualquier header ajeno
// a SGDK solo por ese tipo.
//
// Un único ctx con 4 zonas de USUARIO, cada una una categoría de objeto de
// juego (no una etapa de vida):
//
//   Z_ENEMIES        -- enemigos
//   Z_PLAYERS        -- jugadores
//   Z_ENEMY_BULLETS  -- proyectiles enemigos
//   Z_BONUS_ITEMS    -- objetos de bonificación
//
// Todas viven en el mismo pool (mismo stride: el payload es una unión
// etiquetada, struct game_obj), pero cada categoría se recorre y actualiza
// por separado con su propio DARKEN_FOREACH.
//
// Dentro de cada categoría, las entidades usan DOS sentinels distintos en el
// valor de retorno de update() -- exactamente como en darken.h original, solo
// que ahí eran 3 constantes fijas (CONTINUE/PAUSE/DELETE) y aquí son "cuántas
// zonas tenga este ctx, más la libre":
//
//   - devolver una ZONA (0..zones, incluida la libre) mueve la entidad a esa
//     zona y NO toca su update() -- así se expresa tanto "quédate donde estás"
//     (devolver tu propia zona) como "muérete/expira" (devolver la libre).
//   - devolver una FUNCIÓN (un puntero de verdad, no un entero pequeño) NO
//     mueve de zona -- solo instala esa función como el próximo update(). Así
//     es como un enemigo pasa de "caminando" a "aturdido" sin salir nunca de
//     Z_ENEMIES.
//
// GAME_DISPATCH() es quien decide, mirando el valor devuelto, cuál de las dos
// cosas ha pasado -- eso es lo que en darken.h original hacía _DARKEN_UPDATE,
// y que ahora es responsabilidad de esta aplicación, no del motor.

#include <genesis.h>

typedef u32 uintptr_t; // SGDK no lo define; en m68k un puntero mide 32 bits

#define Z_ENEMIES 0
#define Z_PLAYERS 1
#define Z_ENEMY_BULLETS 2
#define Z_BONUS_ITEMS 3
#define GAME_ZONES 4 // cuántas zonas de usuario tiene ESTE ctx concreto

#include "darken2.h"

/* ============================================================================
 * Payload: una unión etiquetada, porque todas las entidades de este ctx
 * comparten el mismo stride
 * ============================================================================ */

enum obj_kind
{
    KIND_ENEMY,
    KIND_PLAYER,
    KIND_BULLET,
    KIND_BONUS,
};

struct enemy
{
    s16 x, hp, stun_frames_left;
};
struct player
{
    s16 x, hp;
};
struct bullet
{
    s16 x, dx, ttl;
};
struct bonus
{
    s16 x, value;
};

/* ============================================================================
 * GAME_DISPATCH: la convención de ESTA app sobre el valor de retorno de
 * update() -- análoga a _DARKEN_UPDATE en darken.h original, pero aquí, no en
 * el motor
 * ============================================================================ */

#define GAME_DISPATCH(E)                                              \
    do                                                                \
    {                                                                 \
        darken_state_t next = E->update(E->data);                     \
        if ((uintptr_t)next <= (uintptr_t)darken_free_zone(E->owner)) \
            darkenE_set_zone(E->data, (int)(uintptr_t)next);          \
        else                                                          \
            E->update = next;                                         \
    } while (0)

static void game_update(darken_t *ctx)
{
    DARKEN_FOREACH(ctx, Z_ENEMIES, GAME_DISPATCH(_entity));
    DARKEN_FOREACH(ctx, Z_PLAYERS, GAME_DISPATCH(_entity));
    DARKEN_FOREACH(ctx, Z_ENEMY_BULLETS, GAME_DISPATCH(_entity));
    DARKEN_FOREACH(ctx, Z_BONUS_ITEMS, GAME_DISPATCH(_entity));
}

/* ============================================================================
 * Enemigos: dos estados (caminando / aturdido), transición por FUNCIÓN,
 * ninguno de los dos cambia de zona -- el enemigo nunca sale de Z_ENEMIES
 * hasta que muere de verdad
 * ============================================================================ */

static void *enemy_stunned();

static void *enemy_walk(struct enemy *enemy)
{
    enemy->x++;
    kprintf("  [ENEMY]  slot=%d x=%d hp=%d", DARKEN_ENTITY(enemy)->slot, enemy->x, enemy->hp);

    if (enemy->hp <= 0)
    {
        kprintf("  [ENEMY]  slot=%d muere -> libre", DARKEN_ENTITY(enemy)->slot);

        return DARKEN_FREE_ZONE(enemy); // sentinel de ZONA: fin de vida
        // return (void *)(uintptr_t)DARKEN_FREE_ZONE(enemy); // sentinel de ZONA: fin de vida
    }

    if (enemy->x >= 3)
    {
        enemy->stun_frames_left = 2;
        kprintf("  [ENEMY]  slot=%d se aturde", DARKEN_ENTITY(enemy)->slot);
        return enemy_stunned; // sentinel de FUNCIÓN: cambia de estado, sigue en Z_ENEMIES
        // return (void *)enemy_stunned; // sentinel de FUNCIÓN: cambia de estado, sigue en Z_ENEMIES
    }

    return Z_ENEMIES;
    // return (void *)(uintptr_t)Z_ENEMIES; // sentinel de ZONA (la propia): "continuar"
}

static void *enemy_stunned(struct enemy *enemy)
{
    enemy->stun_frames_left--;
    kprintf("  [ENEMY]  slot=%d aturdido, frames_left=%d", DARKEN_ENTITY(enemy)->slot, enemy->stun_frames_left);

    if (enemy->stun_frames_left <= 0)
    {
        kprintf("  [ENEMY]  slot=%d se recupera", DARKEN_ENTITY(enemy)->slot);
        return enemy_walk; // vuelve a caminar -- otra vez, sentinel de FUNCIÓN
        // return (void *)enemy_walk; // vuelve a caminar -- otra vez, sentinel de FUNCIÓN
    }

    return Z_ENEMIES; // sigue aturdido, sigue en su zona
    // return (void *)(uintptr_t)Z_ENEMIES; // sigue aturdido, sigue en su zona
}

/* ============================================================================
 * Jugador: un único estado en este demo, solo para mostrar la zona en acción
 * ============================================================================ */

static void *player_update(struct player *player)
{
    kprintf("  [PLAYER] slot=%d hp=%d", DARKEN_ENTITY(player)->slot, player->hp);
    return Z_PLAYERS; // continuar
    // return (void *)(uintptr_t)Z_PLAYERS; // continuar
}

/* ============================================================================
 * Bala enemiga: sin sub-estados, solo vuela hasta que expira -> libre
 * ============================================================================ */

static void *bullet_update(struct bullet *bullet)
{
    bullet->x += bullet->dx;
    bullet->ttl--;
    kprintf("  [BULLET] slot=%d x=%d ttl=%d", DARKEN_ENTITY(bullet)->slot, bullet->x, bullet->ttl);

    if (bullet->ttl <= 0)
    {
        kprintf("  [BULLET] slot=%d expira -> libre", DARKEN_ENTITY(bullet)->slot);
        return DARKEN_FREE_ZONE(bullet);
        // return (void *)(uintptr_t)DARKEN_FREE_ZONE(bullet);
    }

    return Z_ENEMY_BULLETS;
    // return (void *)(uintptr_t)Z_ENEMY_BULLETS;
}

/* ============================================================================
 * Objeto de bonificación: se recoge (simulado) tras un par de frames -> libre
 * ============================================================================ */

static void *bonus_update(struct bonus *bonus)
{
    bonus->value--;
    kprintf("  [BONUS]  slot=%d value=%d", DARKEN_ENTITY(bonus)->slot, bonus->value);

    if (bonus->value <= 0)
    {
        kprintf("  [BONUS]  slot=%d recogido -> libre", DARKEN_ENTITY(bonus)->slot);
        return DARKEN_FREE_ZONE(bonus);
        // return (void *)(uintptr_t)DARKEN_FREE_ZONE(bonus);
    }

    return Z_BONUS_ITEMS;
    // return (void *)(uintptr_t)Z_BONUS_ITEMS;
}

/* ============================================================================
 * main: storage estático vía DARKEN_DECLARE + DARKEN_BIND. Firma y bucle
 * idiomáticos de SGDK: int main(bool hardReset) y un while(TRUE) que termina
 * en SYS_doVBlankProcess() en cada vuelta -- un programa de SGDK nunca
 * "vuelve" del main().
 * ============================================================================ */

#define CAPACITY 8

int darken2_user_demo_main(bool hardReset)
{
    DARKEN_DECLARE(storage, CAPACITY, GAME_ZONES, max(sizeof(struct enemy), max(sizeof(struct player), max(sizeof(struct bullet), sizeof(struct bonus)))));

    darken_t ctx = DARKEN_BIND(storage);
    darken_init(&ctx);

    darken_entity_t enemy = DARKEN_SPAWN(&ctx, Z_ENEMIES);
    darken_entity_t player = DARKEN_SPAWN(&ctx, Z_PLAYERS);
    darken_entity_t bullet = DARKEN_SPAWN(&ctx, Z_ENEMY_BULLETS);
    darken_entity_t bonus = DARKEN_SPAWN(&ctx, Z_BONUS_ITEMS);

    enemy->update = enemy_walk;
    player->update = player_update;
    bullet->update = bullet_update;
    bonus->update = bonus_update;

    DARKEN_DATA(struct enemy, data_enemy, enemy);
    DARKEN_DATA(struct player, data_player, player);
    DARKEN_DATA(struct bullet, data_bullet, bullet);
    DARKEN_DATA(struct bonus, data_bonus, bonus);

    *data_enemy = (struct enemy){0, 5, 0};
    *data_player = (struct player){0, 3};
    *data_bullet = (struct bullet){0, 1, 3};
    *data_bonus = (struct bonus){0, 0, 2};

    kprintf("zones para este ctx: %d (libre = %d)", ctx.zones, darken_free_zone(&ctx));

    while (TRUE)
    {
        game_update(&ctx);

        // siempre al final del frame
        SYS_doVBlankProcess();
    }

    return 0;
}
