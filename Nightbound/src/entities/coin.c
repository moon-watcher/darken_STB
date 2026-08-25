#include <genesis.h>
#include "coin.h"
#include "player.h"
#include "../game/game.h"
#include "../world/collision.h"

void *coin_state(void *data)
{
    GameEntity *c = (GameEntity *)data;
    ++c->timer;
    return DARKEN_LOOP;
}

static void *coin_destroy(void *data)
{
    GameEntity *c = (GameEntity *)data;
    c->flags |= ENTITY_FLAG_HIDDEN;
    return DARKEN_LOOP;
}

darken_entity coin_spawn(fix16 x, fix16 y)
{
    darken_entity entity = darken_spawn(&g_entity_manager);
    if (!entity)
        return NULL;

    DARKEN_DATA(GameEntity, c, entity);

    c->type = ENTITY_COIN;
    c->x = x;
    c->y = y;
    c->vx = 0;
    c->vy = 0;
    c->width = FIX16(7);
    c->height = FIX16(7);
    c->direction = 1;
    c->flags = 0;
    c->timer = 0;
    c->color = PAL3;

    entity->tag = ENTITY_COIN;
    entity->state = coin_state;
    entity->destructor = (darken_state)coin_destroy;

    return entity;
}

void coin_post_update(darken_entity entity, GameEntity *coin)
{
    if (g_player_entity &&
        DARKEN_ENTITY_IN_USED(g_player_entity))
    {
        DARKEN_DATA(GameEntity, player, g_player_entity);

        if (!(coin->flags & ENTITY_FLAG_DEAD) &&
            collision_aabb(player, coin))
        {
            coin->flags |= ENTITY_FLAG_DEAD;
            ++game_score;
            entity->state = DARKEN_DELETE;
        }
    }
}
