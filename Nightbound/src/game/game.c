#include <genesis.h>
#include "game.h"
#include "../entities/entity.h"
#include "../entities/player.h"
#include "../entities/enemy.h"
#include "../entities/coin.h"
#include "../entities/bullet.h"
#include "../world/level.h"
#include "../systems/input.h"
#include "../systems/camera.h"

uint32_t game_frame;
uint16_t game_score;
uint16_t game_paused;

darken g_entity_manager;
DARKEN_STORAGE(g_entities, 48, sizeof(GameEntity));

darken_entity g_player_entity;

static void game_render_entities(void)
{
    /* BG_B is our dynamic/entity layer. */
    VDP_clearPlane(BG_B, TRUE);

    DARKEN_FOREACH(&g_entity_manager,
        DARKEN_DATA(GameEntity, e, ENTITY);
        entity_render(e);
    );
}

static void game_spawn_level_entities(void)
{
    g_player_entity = player_spawn(FIX16(48), FIX16(128));

    enemy_spawn(FIX16(320), FIX16(144), FIX16(260), FIX16(390));
    enemy_spawn(FIX16(720), FIX16(96), FIX16(650), FIX16(820));
    enemy_spawn(FIX16(1120), FIX16(144), FIX16(1040), FIX16(1220));

    coin_spawn(FIX16(240), FIX16(128));
    coin_spawn(FIX16(352), FIX16(96));
    coin_spawn(FIX16(520), FIX16(64));
    coin_spawn(FIX16(760), FIX16(128));
    coin_spawn(FIX16(1040), FIX16(96));
    coin_spawn(FIX16(1260), FIX16(64));
}

void game_init(void)
{
    game_frame = 0;
    game_score = 0;
    game_paused = 0;

    darken_init(&g_entity_manager, DARKEN_ARGS(g_entities));

    level_init();
    input_init();
    camera_init();
    game_spawn_level_entities();

    VDP_setTextPalette(PAL0);
}

void game_restart(void)
{
    darken_reset(&g_entity_manager);

    game_score = 0;
    game_paused = 0;
    camera_reset();
    level_reset();

    game_spawn_level_entities();
}

static void game_pause_toggle(void)
{
    if (input_pause_pressed())
    {
        game_paused ^= 1;

        if (game_paused)
        {
            DARKEN_FOREACH(&g_entity_manager,
                if (ENTITY != g_player_entity)
                    darken_entity_pause(ENTITY);
            );
        }
        else
        {
            /* Resume parked entities until the paused zone is empty. */
            while (g_entity_manager.paused < g_entity_manager.capacity)
            {
                darken_entity_resume(g_entity_manager.pool[g_entity_manager.paused]);
            }
        }
    }
}

void game_update(void)
{
    ++game_frame;

    input_update();
    game_pause_toggle();

    if (!game_paused)
    {
        darken_update(&g_entity_manager);
        entity_systems_update();
        camera_update();
    }

    game_render_entities();

    VDP_setTextPlane(BG_B);
    VDP_drawText("DARKEN PLATFORMER", 1, 1);
    VDP_drawText("A/B/C: JUMP  START: PAUSE", 1, 2);
    VDP_drawText("SCORE", 28, 1);
    VDP_drawText( "0000", 34, 1);
}
