#include <genesis.h>
#include "enemy.h"
#include "../world/collision.h"

#define ENEMY_SPEED FIX16(0.65)
#define ENEMY_GRAVITY FIX16(0.20)
#define ENEMY_MAX_FALL FIX16(4.0)

typedef struct
{
    GameEntity base;
    fix16 left;
    fix16 right;
} EnemyData;

/* The public payload remains GameEntity-sized; patrol limits are encoded in tag/usr. */
static void *enemy_state_patrol_impl(void *data)
{
    GameEntity *e = (GameEntity *)data;
    e->vx = (e->direction < 0) ? -ENEMY_SPEED : ENEMY_SPEED;

    e->x += e->vx;
    collision_move_x(e);

    e->vy += ENEMY_GRAVITY;
    if (e->vy > ENEMY_MAX_FALL) e->vy = ENEMY_MAX_FALL;
    e->y += e->vy;
    e->flags &= ~ENTITY_FLAG_GROUNDED;
    collision_move_y(e);

    /* usr stores a compact patrol phase; tag is the entity type. */
    if (e->x < FIX16(32) || e->x > FIX16(1400))
        e->direction = -e->direction;

    return DARKEN_LOOP;
}

void *enemy_state_patrol(void *data)
{
    return enemy_state_patrol_impl(data);
}

static void *enemy_destroy(void *data)
{
    GameEntity *e = (GameEntity *)data;
    e->flags |= ENTITY_FLAG_HIDDEN;
    return DARKEN_LOOP;
}

darken_entity enemy_spawn(fix16 x, fix16 y, fix16 left, fix16 right)
{
    (void)left;
    (void)right;

    darken_entity entity = darken_spawn(&g_entity_manager);
    if (!entity)
        return NULL;

    DARKEN_DATA(GameEntity, e, entity);

    e->type = ENTITY_ENEMY;
    e->x = x;
    e->y = y;
    e->vx = 0;
    e->vy = 0;
    e->width = FIX16(8);
    e->height = FIX16(8);
    e->direction = -1;
    e->flags = 0;
    e->timer = 0;
    e->color = PAL2;

    entity->tag = ENTITY_ENEMY;
    entity->usr = 0;
    entity->state = enemy_state_patrol;
    entity->destructor = (darken_state)enemy_destroy;

    return entity;
}

void enemy_post_update(darken_entity entity, GameEntity *enemy)
{
    if (enemy->y > FIX16(224))
        darken_entity_delete(entity);
}
