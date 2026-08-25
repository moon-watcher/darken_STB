/**
 * main.c — "Shards of Ember"
 *
 * A tiny turn-based terminal RPG built on darken.h.
 *
 * NOTE on including darken.h twice: darken.h's declarations are guarded by
 * #ifndef DARKEN_H, but its implementation block (guarded only by #ifdef
 * DARKEN_IMPLEMENTATION, with no include-guard of its own) is not. game.h
 * includes darken.h too, so we #undef DARKEN_IMPLEMENTATION right after our
 * own include to make sure the implementation is compiled exactly once in
 * this translation unit.
 */

#define DARKEN_IMPLEMENTATION
#include "darken.h"
#undef DARKEN_IMPLEMENTATION

#include "game.h"

// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <time.h>
#include <genesis.h>

/* ---------------------------------------------------------------------- */
/* Globals                                                                 */
/* ---------------------------------------------------------------------- */

darken g_world;
darken_entity g_player;
GameState g_state = GS_EXPLORE;
MapId g_map = MAP_OVERWORLD;
darken_entity g_battle_enemy = NULL;
char g_msg[256] = "";

static DARKEN_STORAGE(world_storage, MAX_ENTITIES, sizeof(EntityData));

extern void build_maps(void); /* world.c */

static int read_choice(void)
{
    char line[64];
    if (!fgets(line, sizeof(line), stdin))
        return -1;
    while (line[0] == ' ' || line[0] == '\t')
        memmove(line, line + 1, strlen(line));
    return line[0];
}

static int run_shop(void)
{
    PlayerData *p = (PlayerData *)g_player->data;

    for (;;)
    {
        printf("\n  === Mira's Shop ===\n");
        printf("  Gold: %d   Potions: %d\n", p->gold, p->potions);
        printf("  1) Buy a potion (10 gold)   2) Leave\n  > ");
        int c = read_choice();
        if (c == -1)
            return 0;

        if (c == '1')
        {
            if (p->gold >= 10)
            {
                p->gold -= 10;
                p->potions++;
                printf("  Bought a potion.\n");
            }
            else
            {
                printf("  Not enough gold.\n");
            }
        }
        else
        {
            snprintf(g_msg, sizeof(g_msg), "Safe travels.");
            g_state = GS_EXPLORE;
            return 1;
        }
    }
}

static void print_intro(void)
{
    printf("======================================================\n");
    printf("               S H A R D S   O F   E M B E R\n");
    printf("======================================================\n");
    printf("A small RPG demo built on the Darken entity system.\n\n");
    printf("Controls:\n");
    printf("  w/a/s/d  move\n");
    printf("  e        talk / interact\n");
    printf("  i        check your status\n");
    printf("  q        quit\n\n");
    printf("Fight your way through the overworld, find the stairs\n");
    printf("('>'), and defeat what waits at the bottom of the\n");
    printf("dungeon. Good luck.\n");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); /* flush every write: safe for piped/interactive play alike */
    srand((unsigned)time(NULL));

    darken_init(&g_world, DARKEN_ARGS(world_storage));
    build_maps();
    spawn_world();

    print_intro();

    while (g_state == GS_EXPLORE || g_state == GS_BATTLE || g_state == GS_SHOP)
    {
        if (g_state == GS_EXPLORE)
        {
            render_explore();
            printf("  > ");
            int c = read_choice();
            switch (c)
            {
            case 'w': try_move_player(0, -1); break;
            case 's': try_move_player(0, 1); break;
            case 'a': try_move_player(-1, 0); break;
            case 'd': try_move_player(1, 0); break;
            case 'e': try_interact(); break;
            case 'i':
            {
                PlayerData *p = (PlayerData *)g_player->data;
                printf("\n  Lv%d  HP %d/%d  MP %d/%d  ATK %d  DEF %d  EXP %d  Gold %d  Potions %d\n",
                       p->level, p->hp, p->hp_max, p->mp, p->mp_max, p->atk, p->def,
                       p->exp, p->gold, p->potions);
                break;
            }
            case 'q':
                printf("\n  Farewell, traveler.\n");
                return 0;
            case -1:
                return 0;
            default:
                snprintf(g_msg, sizeof(g_msg), "Unknown command.");
                break;
            }
        }
        else if (g_state == GS_BATTLE)
        {
            render_battle();
            printf("  > ");
            int c = read_choice();
            if (c == -1)
                return 0;
            battle_turn(c - '0');
        }
        else /* GS_SHOP */
        {
            if (!run_shop())
                return 0;
        }
    }

    if (g_state == GS_GAMEOVER)
    {
        printf("\n  You have fallen. GAME OVER.\n");
        return 0;
    }
    if (g_state == GS_WIN)
    {
        printf("\n  The Ember Dragon falls. Its light returns to the land.\n");
        printf("  *** YOU WIN ***\n");
        return 0;
    }
    return 0;
}
