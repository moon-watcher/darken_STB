/**
 * world.c — maps, VDP text rendering, and movement/collision handling.
 *
 * Maps are built procedurally (border + a handful of pillar coordinates)
 * rather than typed as ASCII art, so there's no risk of a mis-counted row
 * width leaving a wall with a hole in it.
 */

#include "game.h"

char world_overworld[MAP_H][MAP_W + 1];
char world_dungeon[MAP_H][MAP_W + 1];

/* Shared little helper: format text into a scratch buffer with SGDK's own
 * vsprintf, then draw it at a tile position. Every screen in the game
 * (explore HUD, battle, shop, title, end screens) goes through this. */
void ui_printf(u16 x, u16 y, const char *fmt, ...)
{
    char buf[40];
    va_list args;

    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    va_end(args);

    VDP_drawText(buf, x, y);
}

static void fill_room(char m[MAP_H][MAP_W + 1])
{
    u16 x, y;

    for (y = 0; y < MAP_H; y++)
    {
        for (x = 0; x < MAP_W; x++)
            m[y][x] = (x == 0 || x == MAP_W - 1 || y == 0 || y == MAP_H - 1) ? '#' : '.';
        m[y][MAP_W] = '\0';
    }
}

static void put(char m[MAP_H][MAP_W + 1], u16 x, u16 y, char c)
{
    m[y][x] = c;
}

void build_maps(void)
{
    u16 i;

    static const u8 ow_pillars[][2] = {
        {5, 2}, {5, 3}, {5, 4}, {9, 2}, {10, 2}, {11, 2}, {9, 6}, {10, 6}, {11, 6}, {14, 3}, {14, 4}, {14, 5}, {3, 6}, {4, 6}};
    static const u8 dg_pillars[][2] = {
        {4, 2}, {4, 3}, {4, 4}, {4, 5}, {8, 5}, {9, 5}, {10, 5}, {11, 5}, {14, 2}, {14, 3}, {14, 4}, {6, 7}, {7, 7}};

    fill_room(world_overworld);
    for (i = 0; i < sizeof(ow_pillars) / sizeof(ow_pillars[0]); i++)
        put(world_overworld, ow_pillars[i][0], ow_pillars[i][1], '#');
    put(world_overworld, 17, 7, '>');

    fill_room(world_dungeon);
    for (i = 0; i < sizeof(dg_pillars) / sizeof(dg_pillars[0]); i++)
        put(world_dungeon, dg_pillars[i][0], dg_pillars[i][1], '#');
    put(world_dungeon, 1, 1, '<');
}

const char (*current_map(void))[MAP_W + 1]
{
    return g_map == MAP_OVERWORLD ? world_overworld : world_dungeon;
}

u16 tile_blocked(s16 x, s16 y)
{
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H)
        return TRUE;
    return current_map()[y][x] == '#';
}

/* Every EntityData variant shares `s16 x; s16 y;` as its first two members,
 * so reading/writing through .player is valid regardless of which variant
 * is actually live (C's "common initial sequence" rule for unions). */
void entity_pos(darken_entity e, s16 *x, s16 *y)
{
    EntityData *d = (EntityData *)e->data;
    *x = d->player.x;
    *y = d->player.y;
}

void entity_set_pos(darken_entity e, s16 x, s16 y)
{
    EntityData *d = (EntityData *)e->data;
    d->player.x = x;
    d->player.y = y;
}

darken_entity entity_at(s16 x, s16 y)
{
    darken_entity found = NULL;

    DARKEN_FOREACH(&g_world, {
        s16 ex;
        s16 ey;
        if (ENTITY->tag == KIND_PLAYER)
            continue;
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
    u16 n, i;

    n = 0;
    for (i = g_world.paused; i < g_world.capacity; i++)
        snap[n++] = g_world.pool[i];
    for (i = 0; i < n; i++)
        if (snap[i]->tag != KIND_PLAYER && snap[i]->usr == (u16)new_map)
            darken_entity_resume(snap[i]);

    n = 0;
    for (i = 0; i < g_world.size; i++)
        snap[n++] = g_world.pool[i];
    for (i = 0; i < n; i++)
        if (snap[i]->tag != KIND_PLAYER && snap[i]->usr != (u16)new_map)
            darken_entity_pause(snap[i]);

    g_map = new_map;
}

void render_explore(void)
{
    char grid[MAP_H][MAP_W + 1];
    PlayerData *p = (PlayerData *)g_player->data;
    u16 y;

    memcpy(grid, current_map(), sizeof(grid));

    DARKEN_FOREACH(&g_world, {
        s16 ex;
        s16 ey;
        char glyph = '?';
        entity_pos(ENTITY, &ex, &ey);
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

    VDP_clearTextArea(0, 0, 40, 28);

    for (y = 0; y < MAP_H; y++)
        VDP_drawText(grid[y], MAP_X, MAP_Y + y);

    ui_printf(MAP_X, MAP_Y + MAP_H + 1, "%s", g_map == MAP_OVERWORLD ? "OVERWORLD" : "DUNGEON");
    ui_printf(MAP_X, MAP_Y + MAP_H + 2, "LV%d  HP %d/%d  MP %d/%d",
              p->level, p->hp, p->hp_max, p->mp, p->mp_max);
    ui_printf(MAP_X, MAP_Y + MAP_H + 3, "GOLD %d  POTIONS %d", p->gold, p->potions);

    if (g_msg[0])
        VDP_drawText(g_msg, MAP_X, MAP_Y + MAP_H + 5);

    VDP_drawText("D-PAD MOVE   A TALK   B STATUS", MAP_X, 26);
}

static void pickup_item(darken_entity item)
{
    ItemData *d = &((EntityData *)item->data)->item;
    PlayerData *p = (PlayerData *)g_player->data;

    if (d->type == ITEM_POTION)
    {
        p->potions++;
        sprintf(g_msg, "Found a potion. (%d in bag)", p->potions);
    }
    else
    {
        p->gold += d->value;
        sprintf(g_msg, "Found %d gold.", d->value);
    }
    darken_entity_delete(item); /* item is always in the active zone -> destructor fires */
}

void try_move_player(s16 dx, s16 dy)
{
    PlayerData *p = (PlayerData *)g_player->data;
    s16 nx = p->x + dx, ny = p->y + dy;
    u16 world_turn = TRUE;

    strclr(g_msg);

    if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H)
    {
        sprintf(g_msg, "You can't go that way.");
        world_turn = FALSE;
    }
    else if (current_map()[ny][nx] == '#')
    {
        sprintf(g_msg, "Something blocks the way.");
        world_turn = FALSE;
    }
    else
    {
        darken_entity other = entity_at(nx, ny);
        if (other && other->tag == KIND_ENEMY)
        {
            battle_start(other);
            world_turn = FALSE; /* the world pauses while a battle plays out */
        }
        else if (other && other->tag == KIND_NPC)
        {
            NpcData *npc = &((EntityData *)other->data)->npc;
            sprintf(g_msg, "%s is here. Press A to talk.", npc->name);
            world_turn = FALSE;
        }
        else
        {
            char tile;

            if (other && other->tag == KIND_ITEM)
                pickup_item(other);

            p->x = nx;
            p->y = ny;

            tile = current_map()[ny][nx];
            if (tile == '>' && g_map == MAP_OVERWORLD)
            {
                switch_map(MAP_DUNGEON);
                p->x = 2;
                p->y = 2;
                sprintf(g_msg, "You descend into the dungeon.");
                world_turn = FALSE;
            }
            else if (tile == '<' && g_map == MAP_DUNGEON)
            {
                switch_map(MAP_OVERWORLD);
                p->x = 16;
                p->y = 6;
                sprintf(g_msg, "You climb back to the surface.");
                world_turn = FALSE;
            }
        }
    }

    if (world_turn && g_state == GS_EXPLORE)
        darken_update(&g_world);

    render_explore();
}

void try_interact(void)
{
    PlayerData *p = (PlayerData *)g_player->data;
    static const s8 off[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    u16 i;

    strclr(g_msg);

    for (i = 0; i < 4; i++)
    {
        darken_entity other = entity_at(p->x + off[i][0], p->y + off[i][1]);
        if (other && other->tag == KIND_NPC)
        {
            g_state = GS_SHOP;
            render_shop();
            return;
        }
    }
    sprintf(g_msg, "There's nobody to talk to here.");
    render_explore();
}
