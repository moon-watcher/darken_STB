#include <genesis.h>
#include "camera.h"
#include "../game/game.h"
#include "../entities/entity.h"
#include "../world/level.h"

static fix16 g_camera_x;

void camera_init(void)
{
    g_camera_x = 0;
}

void camera_reset(void)
{
    g_camera_x = 0;
}

void camera_update(void)
{
    if (!g_player_entity)
        return;

    DARKEN_DATA(GameEntity, p, g_player_entity);

    fix16 target = p->x - FIX16(120);
    if (target < 0) target = 0;
    if (target > FIX16(1280)) target = FIX16(1280);

    g_camera_x += (target - g_camera_x) / 4;

    level_draw(g_camera_x);
}

int16_t camera_world_to_screen_x(fix16 world_x)
{
    return F16_toInt(world_x - g_camera_x);
}

fix16 camera_x(void)
{
    return g_camera_x;
}
