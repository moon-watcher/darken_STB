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
// por separado con su propio DARKEN_FOREACH_ZONE.
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

struct game_obj
{
    enum obj_kind kind;
    union
    {
        struct
        {
            s16 x, hp, stun_frames_left;
        } enemy;
        struct
        {
            s16 x, hp;
        } player;
        struct
        {
            s16 x, dx, ttl;
        } bullet;
        struct
        {
            s16 x, value;
        } bonus;
    } as;
};

/* ============================================================================
 * GAME_DISPATCH: la convención de ESTA app sobre el valor de retorno de
 * update() -- análoga a _DARKEN_UPDATE en darken.h original, pero aquí, no en
 * el motor
 * ============================================================================ */

#define GAME_DISPATCH()                                                      \
    do                                                                       \
    {                                                                        \
        darken_state_t _next = _entity->update(_entity->data);               \
        if ((uintptr_t)_next <= (uintptr_t)darken_free_zone(_entity->owner)) \
            darken_entity_set_zone(_entity->data, (int)(uintptr_t)_next);    \
        else                                                                 \
            _entity->update = _next;                                         \
    } while (0)

static void game_update(darken_t *ctx)
{
    DARKEN_FOREACH_ZONE(ctx, Z_ENEMIES, GAME_DISPATCH());
    DARKEN_FOREACH_ZONE(ctx, Z_PLAYERS, GAME_DISPATCH());
    DARKEN_FOREACH_ZONE(ctx, Z_ENEMY_BULLETS, GAME_DISPATCH());
    DARKEN_FOREACH_ZONE(ctx, Z_BONUS_ITEMS, GAME_DISPATCH());
}

/* ============================================================================
 * Enemigos: dos estados (caminando / aturdido), transición por FUNCIÓN,
 * ninguno de los dos cambia de zona -- el enemigo nunca sale de Z_ENEMIES
 * hasta que muere de verdad
 * ============================================================================ */

static void *enemy_stunned(void *data);

static void *enemy_walk(void *data)
{
    struct game_obj *obj = (struct game_obj *)data;
    obj->as.enemy.x++;
    kprintf("  [ENEMY]  slot=%d x=%d hp=%d", DARKEN_ENTITY(data)->slot, obj->as.enemy.x, obj->as.enemy.hp);

    if (obj->as.enemy.hp <= 0)
    {
        kprintf("  [ENEMY]  slot=%d muere -> libre", DARKEN_ENTITY(data)->slot);
        return (void *)(uintptr_t)DARKEN_FREE_ZONE(data); // sentinel de ZONA: fin de vida
    }

    if (obj->as.enemy.x >= 3)
    {
        obj->as.enemy.stun_frames_left = 2;
        kprintf("  [ENEMY]  slot=%d se aturde", DARKEN_ENTITY(data)->slot);
        return (void *)enemy_stunned; // sentinel de FUNCIÓN: cambia de estado, sigue en Z_ENEMIES
    }

    return (void *)(uintptr_t)Z_ENEMIES; // sentinel de ZONA (la propia): "continuar"
}

static void *enemy_stunned(void *data)
{
    struct game_obj *obj = (struct game_obj *)data;
    obj->as.enemy.stun_frames_left--;
    kprintf("  [ENEMY]  slot=%d aturdido, frames_left=%d", DARKEN_ENTITY(data)->slot, obj->as.enemy.stun_frames_left);

    if (obj->as.enemy.stun_frames_left <= 0)
    {
        kprintf("  [ENEMY]  slot=%d se recupera", DARKEN_ENTITY(data)->slot);
        return (void *)enemy_walk; // vuelve a caminar -- otra vez, sentinel de FUNCIÓN
    }

    return (void *)(uintptr_t)Z_ENEMIES; // sigue aturdido, sigue en su zona
}

/* ============================================================================
 * Jugador: un único estado en este demo, solo para mostrar la zona en acción
 * ============================================================================ */

static void *player_update(void *data)
{
    struct game_obj *obj = (struct game_obj *)data;
    kprintf("  [PLAYER] slot=%d hp=%d", DARKEN_ENTITY(data)->slot, obj->as.player.hp);
    return (void *)(uintptr_t)Z_PLAYERS; // continuar
}

/* ============================================================================
 * Bala enemiga: sin sub-estados, solo vuela hasta que expira -> libre
 * ============================================================================ */

static void *bullet_update(void *data)
{
    struct game_obj *obj = (struct game_obj *)data;
    obj->as.bullet.x += obj->as.bullet.dx;
    obj->as.bullet.ttl--;
    kprintf("  [BULLET] slot=%d x=%d ttl=%d", DARKEN_ENTITY(data)->slot, obj->as.bullet.x, obj->as.bullet.ttl);

    if (obj->as.bullet.ttl <= 0)
    {
        kprintf("  [BULLET] slot=%d expira -> libre", DARKEN_ENTITY(data)->slot);
        return (void *)(uintptr_t)DARKEN_FREE_ZONE(data);
    }

    return (void *)(uintptr_t)Z_ENEMY_BULLETS;
}

/* ============================================================================
 * Objeto de bonificación: se recoge (simulado) tras un par de frames -> libre
 * ============================================================================ */

static void *bonus_update(void *data)
{
    struct game_obj *obj = (struct game_obj *)data;
    obj->as.bonus.value--;
    kprintf("  [BONUS]  slot=%d value=%d", DARKEN_ENTITY(data)->slot, obj->as.bonus.value);

    if (obj->as.bonus.value <= 0)
    {
        kprintf("  [BONUS]  slot=%d recogido -> libre", DARKEN_ENTITY(data)->slot);
        return (void *)(uintptr_t)DARKEN_FREE_ZONE(data);
    }

    return (void *)(uintptr_t)Z_BONUS_ITEMS;
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
    DARKEN_DECLARE(storage, CAPACITY, GAME_ZONES, sizeof(struct game_obj));
    darken_t ctx = DARKEN_BIND(storage);
    darken_init(&ctx);

    darken_entity_t enemy = DARKEN_SPAWN(&ctx, Z_ENEMIES);
    enemy->update = enemy_walk;

    DARKEN_DATA(struct game_obj, data_enemy, enemy);
    data_enemy->kind = KIND_ENEMY;
    data_enemy->as.enemy.x = 0;
    data_enemy->as.enemy.hp = 5;
    data_enemy->as.enemy.stun_frames_left = 0;

    darken_entity_t player = DARKEN_SPAWN(&ctx, Z_PLAYERS);
    player->update = player_update;
    DARKEN_DATA(struct game_obj, data_player, player);
    data_player->kind = KIND_PLAYER;
    data_player->as.player.x = 0;
    data_player->as.player.hp = 3;

    darken_entity_t bullet = DARKEN_SPAWN(&ctx, Z_ENEMY_BULLETS);
    bullet->update = bullet_update;
    DARKEN_DATA(struct game_obj, data_bullet, bullet);
    data_bullet->kind = KIND_BULLET;
    data_bullet->as.bullet.x = 0;
    data_bullet->as.bullet.dx = 1;
    data_bullet->as.bullet.ttl = 3;

    darken_entity_t bonus = DARKEN_SPAWN(&ctx, Z_BONUS_ITEMS);
    bonus->update = bonus_update;
    DARKEN_DATA(struct game_obj, data_bonus, bonus);
    data_bonus->kind = KIND_BONUS;
    data_bonus->as.bonus.x = 0;
    data_bonus->as.bonus.value = 2;

    kprintf("zones para este ctx: %d (libre = %d)", ctx.zones, darken_free_zone(&ctx));

    while (TRUE)
    {
        game_update(&ctx);

        // siempre al final del frame
        SYS_doVBlankProcess();
    }

    return 0;
}
