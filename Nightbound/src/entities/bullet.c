#include <genesis.h>
#include "bullet.h"
#include "../world/collision.h"

#define BULLET_SPEED FIX16(3.0)

void *bullet_state(void *data)
{
    GameEntity *b = (GameEntity *)data;
    b->x += b->vx;
    return DARKEN_LOOP;
}

static void *bullet_destroy(void *data)
{
    GameEntity *b = (GameEntity *)data;
    b->flags |= ENTITY_FLAG_HIDDEN;
    return DARKEN_LOOP;
}

darken_entity bullet_spawn(fix16 x, fix16 y, int16_t direction)
{
    darken_entity entity = darken_spawn(&g_entity_manager);
    if (!entity)
        return NULL;

    DARKEN_DATA(GameEntity, b, entity);

    b->type = ENTITY_BULLET;
    b->x = x;
    b->y = y;
    b->vx = direction < 0 ? -BULLET_SPEED : BULLET_SPEED;
    b->vy = 0;
    b->width = FIX16(4);
    b->height = FIX16(4);
    b->direction = direction;
    b->flags = 0;
    b->timer = 0;
    b->color = PAL1;

    entity->tag = ENTITY_BULLET;
    entity->state = bullet_state;
    entity->destructor = (darken_state)bullet_destroy;

    return entity;
}

void bullet_post_update(darken_entity entity, GameEntity *bullet)
{
    if (bullet->x < 0 || bullet->x > FIX16(1600) ||
        collision_point_solid(bullet->x, bullet->y))
    {
        entity->state = DARKEN_DELETE;
    }
}
