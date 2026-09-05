#include <genesis.h>
#include "player.h"
#include "../game/game.h"
#include "../systems/input.h"
#include "../world/collision.h"
#include "../world/level.h"

#define PLAYER_SPEED       FIX16(1.75)
#define PLAYER_ACCEL       FIX16(0.35)
#define PLAYER_FRICTION     FIX16(0.25)
#define PLAYER_JUMP        FIX16(4.8)
#define PLAYER_GRAVITY     FIX16(0.22)
#define PLAYER_MAX_FALL    FIX16(4.5)

static void player_apply_horizontal(GameEntity *p)
{
    if (input_left())
    {
        p->vx -= PLAYER_ACCEL;
        if (p->vx < -PLAYER_SPEED) p->vx = -PLAYER_SPEED;
        p->direction = -1;
    }
    else if (input_right())
    {
        p->vx += PLAYER_ACCEL;
        if (p->vx > PLAYER_SPEED) p->vx = PLAYER_SPEED;
        p->direction = 1;
    }
    else
    {
        if (p->vx > 0)
        {
            p->vx -= PLAYER_FRICTION;
            if (p->vx < 0) p->vx = 0;
        }
        else if (p->vx < 0)
        {
            p->vx += PLAYER_FRICTION;
            if (p->vx > 0) p->vx = 0;
        }
    }
}

static void player_move(GameEntity *p)
{
    p->x += p->vx;
    collision_move_x(p);

    p->vy += PLAYER_GRAVITY;
    if (p->vy > PLAYER_MAX_FALL)
        p->vy = PLAYER_MAX_FALL;

    p->y += p->vy;
    p->flags &= ~ENTITY_FLAG_GROUNDED;
    collision_move_y(p);
}

void *player_state_idle(void *data)
{
    GameEntity *p = (GameEntity *)data;
    player_apply_horizontal(p);
    player_move(p);

    if (p->flags & ENTITY_FLAG_DEAD)
        return player_state_dead;
    if (!(p->flags & ENTITY_FLAG_GROUNDED))
        return player_state_fall;
    if (input_jump_pressed())
    {
        p->vy = -PLAYER_JUMP;
        return player_state_jump;
    }
    if (p->vx != 0)
        return player_state_run;

    return DARKEN_LOOP;
}

void *player_state_run(void *data)
{
    GameEntity *p = (GameEntity *)data;
    player_apply_horizontal(p);
    player_move(p);

    if (p->flags & ENTITY_FLAG_DEAD)
        return player_state_dead;
    if (!(p->flags & ENTITY_FLAG_GROUNDED))
        return player_state_fall;
    if (input_jump_pressed())
    {
        p->vy = -PLAYER_JUMP;
        return player_state_jump;
    }
    if (p->vx == 0)
        return player_state_idle;

    return DARKEN_LOOP;
}

void *player_state_jump(void *data)
{
    GameEntity *p = (GameEntity *)data;
    player_apply_horizontal(p);

    /* Holding the jump button slightly extends the jump. */
    if (input_jump_held() && p->vy < FIX16(-1.5))
        p->vy += FIX16(0.05);

    player_move(p);

    if (p->flags & ENTITY_FLAG_DEAD)
        return player_state_dead;
    if (p->vy >= 0)
        return player_state_fall;

    return DARKEN_LOOP;
}

void *player_state_fall(void *data)
{
    GameEntity *p = (GameEntity *)data;
    player_apply_horizontal(p);
    player_move(p);

    if (p->flags & ENTITY_FLAG_DEAD)
        return player_state_dead;
    if (p->flags & ENTITY_FLAG_GROUNDED)
    {
        if (p->vx != 0)
            return player_state_run;
        return player_state_idle;
    }

    return DARKEN_LOOP;
}

void *player_state_dead(void *data)
{
    GameEntity *p = (GameEntity *)data;

    p->vx = 0;
    p->vy = 0;
    p->flags |= ENTITY_FLAG_HIDDEN;

    if (input_jump_pressed())
    {
        p->flags &= ~(ENTITY_FLAG_DEAD | ENTITY_FLAG_HIDDEN);
        p->x = FIX16(48);
        p->y = FIX16(128);
        p->vy = 0;
        return player_state_fall;
    }

    return DARKEN_LOOP;
}

static void *player_destroy(GameEntity *p)
{
    p->flags |= ENTITY_FLAG_HIDDEN;
    return DARKEN_LOOP;
}

darken_entity player_spawn(fix16 x, fix16 y)
{
    darken_entity entity = darken_spawn(&g_entity_manager);
    if (!entity)
        return NULL;

    DARKEN_DATA(GameEntity, p, entity);

    p->type = ENTITY_PLAYER;
    p->x = x;
    p->y = y;
    p->vx = 0;
    p->vy = 0;
    p->width = FIX16(8);
    p->height = FIX16(14);
    p->direction = 1;
    p->flags = 0;
    p->timer = 0;
    p->color = PAL1;

    entity->tag = ENTITY_PLAYER;
    entity->usr = 0;
    entity->state = player_state_fall;
    entity->destructor = (darken_state)player_destroy;

    return entity;
}

void player_post_update(darken_entity entity, GameEntity *p)
{
    if (p->y > FIX16(224))
        p->flags |= ENTITY_FLAG_DEAD;

    DARKEN_FOREACH(&g_entity_manager,
        DARKEN_DATA(GameEntity, other, ENTITY);

        if (other->type == ENTITY_ENEMY && collision_aabb(p, other))
            p->flags |= ENTITY_FLAG_DEAD;
    );

    (void)entity;
}
