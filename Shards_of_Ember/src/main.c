/**
 * main.c — "Shards of Ember" for SGDK (Sega Genesis / Mega Drive).
 *
 * Controls
 *   Explore : D-Pad move, A talk/interact, B status toggle
 *   Battle  : A attack, B fireball (3 MP), C potion, START flee
 *   Shop    : A buy potion (10g), B leave
 *   Title / end screens: START to begin / play again
 *
 * There is no keyboard, no stdio, no blocking read: every screen is driven
 * by polling JOY_readJoypad(JOY_1) once per frame and edge-detecting which
 * buttons just went down (state & ~prevState), so holding a direction
 * doesn't repeat every single frame.
 */

#include "darken.h"
#define DARKEN_IMPLEMENTATION

#include "game.h"

/* ---------------------------------------------------------------------- */
/* Globals                                                                 */
/* ---------------------------------------------------------------------- */

darken g_world;
darken_entity g_player;
GameState g_state = GS_TITLE;
MapId g_map = MAP_OVERWORLD;
darken_entity g_battle_enemy = NULL;
char g_msg[40] = "";
char g_msg2[40] = "";

static DARKEN_STORAGE(world_storage, MAX_ENTITIES, sizeof(EntityData));

/* -------------------------------------------------------------- Shop -- */

void render_shop(void)
{
    PlayerData *p = (PlayerData *)g_player->data;

    VDP_clearTextArea(0, 0, 40, 28);
    VDP_drawText("=== MIRA'S SHOP ===", 2, 2);
    ui_printf(2, 4, "Gold: %d   Potions: %d", p->gold, p->potions);
    VDP_drawText("A  Buy a potion (10 gold)", 2, 7);
    VDP_drawText("B  Leave", 2, 8);
    if (g_msg[0])
        VDP_drawText(g_msg, 2, 10);
}

void shop_input(u16 pressed)
{
    PlayerData *p = (PlayerData *)g_player->data;

    strclr(g_msg);

    if (pressed & BUTTON_A)
    {
        if (p->gold >= 10)
        {
            p->gold -= 10;
            p->potions++;
            sprintf(g_msg, "Bought a potion.");
        }
        else
        {
            sprintf(g_msg, "Not enough gold.");
        }
        render_shop();
    }
    else if (pressed & BUTTON_B)
    {
        sprintf(g_msg, "Safe travels.");
        g_state = GS_EXPLORE;
        render_explore();
    }
}

/* ------------------------------------------------------------ Status -- */

void render_status(void)
{
    PlayerData *p = (PlayerData *)g_player->data;

    VDP_clearTextArea(0, 0, 40, 28);
    VDP_drawText("=== STATUS ===", 2, 2);
    ui_printf(2, 4, "Level %d   Exp %d", p->level, p->exp);
    ui_printf(2, 5, "HP %d/%d   MP %d/%d", p->hp, p->hp_max, p->mp, p->mp_max);
    ui_printf(2, 6, "ATK %d   DEF %d", p->atk, p->def);
    ui_printf(2, 7, "Gold %d   Potions %d", p->gold, p->potions);
    VDP_drawText("Press B to return", 2, 9);
}

/* ------------------------------------------------------- Title / end -- */

static void render_title(void)
{
    VDP_clearTextArea(0, 0, 40, 28);
    VDP_drawText("SHARDS OF EMBER", 12, 6);
    VDP_drawText("an RPG demo built on the", 8, 9);
    VDP_drawText("Darken entity system", 9, 10);
    VDP_drawText("D-PAD move   A talk   B status", 6, 15);
    VDP_drawText("Fight your way through the", 6, 17);
    VDP_drawText("overworld, find the stairs (>),", 6, 18);
    VDP_drawText("and defeat what waits below.", 6, 19);
    VDP_drawText("PRESS START", 14, 23);
}

static void render_end(void)
{
    VDP_clearTextArea(0, 0, 40, 28);
    if (g_state == GS_WIN)
    {
        VDP_drawText("THE EMBER DRAGON FALLS.", 8, 10);
        VDP_drawText("*** YOU WIN ***", 12, 12);
    }
    else
    {
        VDP_drawText("You have fallen.", 12, 10);
        VDP_drawText("*** GAME OVER ***", 11, 12);
    }
    VDP_drawText("PRESS START to play again", 7, 16);
}

/* Wait for a fresh START press. Also used on the title screen to gather a
 * player-timing-dependent random seed, since there's no clock to seed from
 * otherwise. */
static void wait_for_start(u16 seed_random)
{
    u16 prev = JOY_readJoypad(JOY_1);
    u32 ticks = 0;

    for (;;)
    {
        u16 state = JOY_readJoypad(JOY_1);
        u16 pressed = state & ~prev;
        prev = state;

        if (pressed & BUTTON_START)
            break;

        ticks++;
        SYS_doVBlankProcess();
    }

    if (seed_random)
        setRandomSeed((u16)ticks);

    /* Drain the press so the next screen doesn't see a stale START edge. */
    while (JOY_readJoypad(JOY_1) & BUTTON_START)
        SYS_doVBlankProcess();
}

/* ------------------------------------------------------------- Main --- */

int main(bool hardReset)
{
    u16 prevState;

    JOY_init();
    darken_init(&g_world, DARKEN_ARGS(world_storage));
    build_maps();

    for (;;)
    {
        g_state = GS_TITLE;
        render_title();
        wait_for_start(TRUE);

        darken_reset(&g_world);
        spawn_world();
        g_map = MAP_OVERWORLD;
        g_state = GS_EXPLORE;
        strclr(g_msg);
        render_explore();

        prevState = JOY_readJoypad(JOY_1);

        while (g_state != GS_GAMEOVER && g_state != GS_WIN)
        {
            u16 state = JOY_readJoypad(JOY_1);
            u16 pressed = state & ~prevState;
            prevState = state;

            if (pressed)
            {
                switch (g_state)
                {
                case GS_EXPLORE:
                    if (pressed & BUTTON_UP)
                        try_move_player(0, -1);
                    else if (pressed & BUTTON_DOWN)
                        try_move_player(0, 1);
                    else if (pressed & BUTTON_LEFT)
                        try_move_player(-1, 0);
                    else if (pressed & BUTTON_RIGHT)
                        try_move_player(1, 0);
                    else if (pressed & BUTTON_A)
                        try_interact();
                    else if (pressed & BUTTON_B)
                    {
                        g_state = GS_STATUS;
                        render_status();
                    }
                    break;
                case GS_STATUS:
                    if (pressed & BUTTON_B)
                    {
                        g_state = GS_EXPLORE;
                        render_explore();
                    }
                    break;
                case GS_BATTLE:
                    battle_input(pressed);
                    break;
                case GS_SHOP:
                    shop_input(pressed);
                    break;
                default:
                    break;
                }
            }

            SYS_doVBlankProcess();
        }

        render_end();
        wait_for_start(FALSE);
    }

    return 0;
}
