#include <genesis.h>
#include "entity.h"
#include "player.h"
#include "enemy.h"
#include "coin.h"
#include "bullet.h"
#include "../systems/camera.h"

static uint16_t entity_tile(const GameEntity *e)
{
    switch (e->type)
    {
        case ENTITY_PLAYER: return TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE, 1);
        case ENTITY_ENEMY:  return TILE_ATTR_FULL(PAL2, FALSE, FALSE, FALSE, 1);
        case ENTITY_COIN:   return TILE_ATTR_FULL(PAL3, FALSE, FALSE, FALSE, 2);
        case ENTITY_BULLET: return TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE, 2);
        default:            return 0;
    }
}

void entity_render(GameEntity *e)
{
    if (e->flags & ENTITY_FLAG_HIDDEN)
        return;

    const int16_t sx = camera_world_to_screen_x(e->x);
    const int16_t sy = F16_toInt(e->y);

    if (sx < -16 || sx >= 336 || sy < -16 || sy >= 240)
        return;

    VDP_setTileMapXY(BG_B, entity_tile(e), sx >> 3, sy >> 3);
}

void entity_systems_update(void)
{
    DARKEN_FOREACH(&g_entity_manager,
        DARKEN_DATA(GameEntity, e, ENTITY);

        if (e->type == ENTITY_PLAYER)
            player_post_update(ENTITY, e);
        else if (e->type == ENTITY_ENEMY)
            enemy_post_update(ENTITY, e);
        else if (e->type == ENTITY_COIN)
            coin_post_update(ENTITY, e);
        else if (e->type == ENTITY_BULLET)
            bullet_post_update(ENTITY, e);
    );
}

void entity_destroy_sprite(GameEntity *entity)
{
    (void)entity;
}
