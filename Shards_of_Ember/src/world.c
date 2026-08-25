/**
 * world.c — maps, rendering, and movement/collision handling.
 *
 * Maps are built procedurally (border + a handful of pillar coordinates)
 * rather than typed as ASCII art, so there's no risk of a mis-counted row
 * width leaving a wall with a hole in it.
 */

#include <stdio.h>
#include <string.h>

#include "game.h"

char world_overworld[MAP_H][MAP_W + 1];
char world_dungeon[MAP_H][MAP_W + 1];

static void fill_room(char m[MAP_H][MAP_W + 1])
{
    for (int y = 0; y < MAP_H; y++)
    {
        for (int x = 0; x < MAP_W; x++)
            m[y][x] = (x == 0 || x == MAP_W - 1 || y == 0 || y == MAP_H - 1) ? '#' : '.';
        m[y][MAP_W] = '\0';
    }
}

static void put(char m[MAP_H][MAP_W + 1], int x, int y, char c)
{
    m[y][x] = c;
}

void build_maps(void)
{
    fill_room(world_overworld);
    int ow_pillars[][2] = {
        {5, 2}, {5, 3}, {5, 4}, {9, 2}, {10, 2}, {11, 2}, {9, 6}, {10, 6}, {11, 6}, {14, 3}, {14, 4}, {14, 5}, {3, 6}, {4, 6}};
    for (size_t i = 0; i < sizeof(ow_pillars) / sizeof(ow_pillars[0]); i++)
        put(world_overworld, ow_pillars[i][0], ow_pillars[i][1], '#');
    put(world_overworld, 17, 7, '>');

    fill_room(world_dungeon);
    int dg_pillars[][2] = {
        {4, 2}, {4, 3}, {4, 4}, {4, 5}, {8, 5}, {9, 5}, {10, 5}, {11, 5}, {14, 2}, {14, 3}, {14, 4}, {6, 7}, {7, 7}};
    for (size_t i = 0; i < sizeof(dg_pillars) / sizeof(dg_pillars[0]); i++)
        put(world_dungeon, dg_pillars[i][0], dg_pillars[i][1], '#');
    put(world_dungeon, 1, 1, '<');
}

const char (*current_map(void))[MAP_W + 1]
{
    return g_map == MAP_OVERWORLD ? world_overworld : world_dungeon;
}

int tile_blocked(int x, int y)
{
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H)
        return 1;
    return current_map()[y][x] == '#';
}

/* Every EntityData variant shares `int x; int y;` as its first two members,
 * so reading/writing through .player is valid regardless of which variant
 * is actually live (C's "common initial sequence" rule for unions). */
void entity_pos(darken_entity e, int *x, int *y)
{
    EntityData *d = (EntityData *)e->data;
    *x = d->player.x;
    *y = d->player.y;
}

void entity_set_pos(darken_entity e, int x, int y)
{
    EntityData *d = (EntityData *)e->data;
    d->player.x = x;
    d->player.y = y;
}

darken_entity entity_at(int x, int y)
{
    darken_entity found = NULL;
    DARKEN_FOREACH(&g_world, {
        if (ENTITY->tag == KIND_PLAYER)
            continue;
        int ex;
        int ey;
        entity_pos(ENTITY, &ex, &ey);
        if (ex == x && ey == y)
            found = ENTITY;
    });
    return found;
}

/* Pause every active entity that doesn't belong to `new_map` and resume
 * every paused entity that does. Snapshotting pointers first means the
 * shuffling that pause()/resume() do to the pool array can't make us skip
 * or double-visit an entity: entity addresses never move, only which slot
 * they occupy does. */
void switch_map(MapId new_map)
{
    darken_entity snap[MAX_ENTITIES];
    int n;

    n = 0;
    for (int i = g_world.paused; i < g_world.capacity; i++)
        snap[n++] = g_world.pool[i];
    for (int i = 0; i < n; i++)
        if (snap[i]->tag != KIND_PLAYER && snap[i]->usr == (uint16_t)new_map)
            darken_entity_resume(snap[i]);

    n = 0;
    for (int i = 0; i < g_world.size; i++)
        snap[n++] = g_world.pool[i];
    for (int i = 0; i < n; i++)
        if (snap[i]->tag != KIND_PLAYER && snap[i]->usr != (uint16_t)new_map)
            darken_entity_pause(snap[i]);

    g_map = new_map;
}

void render_explore(void)
{
    char grid[MAP_H][MAP_W + 1];
    memcpy(grid, current_map(), sizeof(grid));

    DARKEN_FOREACH(&g_world, {
        int ex;
        int ey;
        entity_pos(ENTITY, &ex, &ey);
        char glyph = '?';
        if (ENTITY->tag == KIND_PLAYER)
            glyph = '@';
        else if (ENTITY->tag == KIND_ENEMY)
            glyph = enemy_glyph(((EntityData *)ENTITY->data)->enemy.type);
        else if (ENTITY->tag == KIND_ITEM)
            glyph = ((EntityData *)ENTITY->data)->item.type == ITEM_POTION ? '!' : '$';
        else if (ENTITY->tag == KIND_NPC)
            glyph = 'N';
        grid[ey][ex] = glyph;
    });

    PlayerData *p = (PlayerData *)g_player->data;

    putchar('\n');
    for (int y = 0; y < MAP_H; y++)
        printf("  %s\n", grid[y]);
    printf("\n  %s | Lv%d HP %d/%d MP %d/%d  Gold %d  Potions %d\n",
           g_map == MAP_OVERWORLD ? "Overworld" : "Dungeon",
           p->level, p->hp, p->hp_max, p->mp, p->mp_max, p->gold, p->potions);
    if (g_msg[0])
        printf("  > %s\n", g_msg);
    g_msg[0] = '\0';
}

static void pickup_item(darken_entity item)
{
    ItemData *d = &((EntityData *)item->data)->item;
    PlayerData *p = (PlayerData *)g_player->data;

    if (d->type == ITEM_POTION)
    {
        p->potions++;
        snprintf(g_msg, sizeof(g_msg), "You found a potion. (%d in bag)", p->potions);
    }
    else
    {
        p->gold += d->value;
        snprintf(g_msg, sizeof(g_msg), "You found %d gold.", d->value);
    }
    darken_entity_delete(item); /* item is always in the active zone -> destructor fires */
}

void try_move_player(int dx, int dy)
{
    PlayerData *p = (PlayerData *)g_player->data;
    int nx = p->x + dx, ny = p->y + dy;
    int world_turn = 1;

    if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H)
    {
        snprintf(g_msg, sizeof(g_msg), "You can't go that way.");
        world_turn = 0;
    }
    else if (current_map()[ny][nx] == '#')
    {
        snprintf(g_msg, sizeof(g_msg), "There's something in the way.");
        world_turn = 0;
    }
    else
    {
        darken_entity other = entity_at(nx, ny);
        if (other && other->tag == KIND_ENEMY)
        {
            battle_start(other);
            world_turn = 0; /* the world pauses while a battle plays out */
        }
        else if (other && other->tag == KIND_NPC)
        {
            NpcData *npc = &((EntityData *)other->data)->npc;
            snprintf(g_msg, sizeof(g_msg), "%s is here. Press 'e' to talk.", npc->name);
            world_turn = 0;
        }
        else
        {
            if (other && other->tag == KIND_ITEM)
                pickup_item(other);

            p->x = nx;
            p->y = ny;

            char tile = current_map()[ny][nx];
            if (tile == '>' && g_map == MAP_OVERWORLD)
            {
                switch_map(MAP_DUNGEON);
                p->x = 2;
                p->y = 2;
                snprintf(g_msg, sizeof(g_msg), "You descend into the dungeon.");
                world_turn = 0;
            }
            else if (tile == '<' && g_map == MAP_DUNGEON)
            {
                switch_map(MAP_OVERWORLD);
                p->x = 16;
                p->y = 6;
                snprintf(g_msg, sizeof(g_msg), "You climb back to the surface.");
                world_turn = 0;
            }
        }
    }

    if (world_turn && g_state == GS_EXPLORE)
        darken_update(&g_world);
}

void try_interact(void)
{
    PlayerData *p = (PlayerData *)g_player->data;
    static const int off[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};

    for (int i = 0; i < 4; i++)
    {
        darken_entity other = entity_at(p->x + off[i][0], p->y + off[i][1]);
        if (other && other->tag == KIND_NPC)
        {
            g_state = GS_SHOP;
            return;
        }
    }
    snprintf(g_msg, sizeof(g_msg), "There's nobody to talk to here.");
}
