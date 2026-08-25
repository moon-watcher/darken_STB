#include <genesis.h>
#include "collision.h"
#include "level.h"

static int16_t floor_div_8(fix16 v)
{
    int16_t px = F16_toInt(v);
    if (px < 0) return -1;
    return px >> 3;
}

static uint16_t rect_hits_solid(fix16 x, fix16 y, fix16 w, fix16 h)
{
    int16_t left = floor_div_8(x);
    int16_t right = floor_div_8(x + w - FIX16(0.01));
    int16_t top = floor_div_8(y);
    int16_t bottom = floor_div_8(y + h - FIX16(0.01));

    int16_t ty;
    for (ty = top; ty <= bottom; ++ty)
    {
        int16_t tx;
        for (tx = left; tx <= right; ++tx)
        {
            if (level_solid((uint16_t)tx, (uint16_t)ty))
                return 1;
        }
    }

    return 0;
}

void collision_move_x(GameEntity *e)
{
    if (e->vx > 0)
    {
        if (rect_hits_solid(e->x, e->y, e->width, e->height))
        {
            e->x = ((F16_toInt(e->x + e->width) >> 3) << 3) - F16_toInt(e->width);
            e->vx = 0;
        }
    }
    else if (e->vx < 0)
    {
        if (rect_hits_solid(e->x, e->y, e->width, e->height))
        {
            e->x = ((F16_toInt(e->x) >> 3) + 1) << 3;
            e->vx = 0;
        }
    }
}

void collision_move_y(GameEntity *e)
{
    if (rect_hits_solid(e->x, e->y, e->width, e->height))
    {
        if (e->vy > 0)
        {
            e->y = ((F16_toInt(e->y + e->height) >> 3) << 3) - F16_toInt(e->height);
            e->flags |= ENTITY_FLAG_GROUNDED;
        }
        else
        {
            e->y = ((F16_toInt(e->y) >> 3) + 1) << 3;
        }

        e->vy = 0;
    }
}

uint16_t collision_point_solid(fix16 x, fix16 y)
{
    int16_t tx = floor_div_8(x);
    int16_t ty = floor_div_8(y);
    return level_solid((uint16_t)tx, (uint16_t)ty);
}

uint16_t collision_aabb(const GameEntity *a, const GameEntity *b)
{
    if (a->x + a->width <= b->x) return 0;
    if (b->x + b->width <= a->x) return 0;
    if (a->y + a->height <= b->y) return 0;
    if (b->y + b->height <= a->y) return 0;
    return 1;
}
