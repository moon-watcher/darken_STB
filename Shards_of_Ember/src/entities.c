/**
 * entities.c — spawn helpers and the enemy AI state machine.
 *
 * Enemies alternate between two darken_state functions, enemy_state_patrol
 * and enemy_state_chase. Each returns DARKEN_LOOP to keep going, or the
 * *other* function's pointer to switch behavior — the mechanism
 * darken_state exists for. When a chasing enemy reaches the player it calls
 * battle_start() and returns DARKEN_PAUSE, so darken itself moves the enemy
 * into the paused zone on its next tick.
 */

#include "game.h"
#include "darken.h"

static const struct
{
    char glyph;
    const char *name;
    s16 hp, atk, def, exp_reward, gold_reward;
} ENEMY_TABLE[] = {
    [ENEMY_SLIME] = {'s', "Slime", 10, 3, 1, 5, 3},
    [ENEMY_BAT] = {'b', "Bat", 8, 4, 0, 6, 2},
    [ENEMY_GOBLIN] = {'g', "Goblin", 16, 5, 2, 10, 6},
    [ENEMY_ORC] = {'o', "Orc", 28, 8, 4, 20, 15},
    [ENEMY_DRAGON] = {'D', "Ember Dragon", 60, 14, 6, 100, 100},
};

char enemy_glyph(EnemyType t) { return ENEMY_TABLE[t].glyph; }
const char *enemy_name(EnemyType t) { return ENEMY_TABLE[t].name; }

void *passive_state(void *data)
{
    (void)data;
    return DARKEN_LOOP;
}

static u16 in_range(s16 ax, s16 ay, s16 bx, s16 by, s16 radius)
{
    s16 dx = ax - bx, dy = ay - by;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return (dx > dy ? dx : dy) <= radius;
}

static void step_toward(EnemyData *e, s16 tx, s16 ty)
{
    s16 nx = e->x, ny = e->y;
    darken_entity blocker;

    if (e->x != tx)
        nx += (tx > e->x) ? 1 : -1;
    else if (e->y != ty)
        ny += (ty > e->y) ? 1 : -1;

    if (tile_blocked(nx, ny))
        return;
    blocker = entity_at(nx, ny);
    if (blocker && (blocker->tag == KIND_NPC || blocker->tag == KIND_ENEMY || blocker->tag == KIND_ITEM))
        return;

    e->x = nx;
    e->y = ny;
}

void *enemy_state_patrol(void *data)
{
    EnemyData *e = (EnemyData *)data;
    PlayerData *p = (PlayerData *)g_player->data;
    darken_entity self = darken_entity_from_data(data);

    if (g_map == (MapId)self->usr && in_range(e->x, e->y, p->x, p->y, AGGRO_RADIUS))
        return (darken_state)enemy_state_chase;

    if ((random() % 3) == 0)
    {
        static const s8 off[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        u16 dir = random() % 4;
        s16 nx = e->x + off[dir][0], ny = e->y + off[dir][1];

        if (in_range(nx, ny, e->spawn_x, e->spawn_y, 3) && !tile_blocked(nx, ny))
        {
            darken_entity blocker = entity_at(nx, ny);
            if (!blocker || blocker->tag == KIND_PLAYER)
            {
                e->x = nx;
                e->y = ny;
            }
        }
    }
    return DARKEN_LOOP;
}

void *enemy_state_chase(void *data)
{
    darken_entity self = darken_entity_from_data(data);
    EnemyData *e = (EnemyData *)data;
    PlayerData *p = (PlayerData *)g_player->data;

    if (g_map != (MapId)self->usr || !in_range(e->x, e->y, p->x, p->y, DEAGGRO_RADIUS))
        return (darken_state)enemy_state_patrol;

    step_toward(e, p->x, p->y);

    if (e->x == p->x && e->y == p->y)
    {
        battle_start(self);
        return DARKEN_PAUSE;
    }
    return DARKEN_LOOP;
}

static void *enemy_destructor(void *data)
{
    EnemyData *e = (EnemyData *)data;
    darken_entity self = darken_entity_from_data(data);

    /* A chance of bonus loot when a monster falls. Note: darken only calls
     * a destructor for entities removed from the ACTIVE zone (see
     * darken_entity_delete) — battle.c always resolves kills while the
     * enemy is still active (see the comment at the top of battle.c), so
     * this reliably fires. */
    if ((random() % 100) < 30)
        spawn_item(ITEM_GOLD, e->x, e->y, 5 + (random() % 10), (MapId)self->usr);

    return DARKEN_DELETE;
}

static void *item_destructor(void *data)
{
    (void)data;
    return DARKEN_DELETE;
}

darken_entity spawn_player(s16 x, s16 y)
{
    darken_entity e = darken_spawn(&g_world);
    PlayerData *p = (PlayerData *)e->data;

    e->tag = KIND_PLAYER;
    e->usr = 0;
    e->state = (darken_state)passive_state;
    e->destructor = NULL;

    memset(p, 0, sizeof(*p));
    p->x = x;
    p->y = y;
    p->hp = p->hp_max = 24;
    p->mp = p->mp_max = 10;
    p->atk = 6;
    p->def = 2;
    p->level = 1;
    p->exp = 0;
    p->gold = 5;
    p->potions = 2;
    return e;
}

darken_entity spawn_enemy(EnemyType type, s16 x, s16 y, MapId map)
{
    darken_entity e = darken_spawn(&g_world);
    EnemyData *d = (EnemyData *)e->data;

    e->tag = KIND_ENEMY;
    e->usr = (u16)map;
    e->state = (darken_state)enemy_state_patrol;
    e->destructor = (darken_state)enemy_destructor;

    d->x = x;
    d->y = y;
    d->spawn_x = x;
    d->spawn_y = y;
    d->type = type;
    d->hp = d->hp_max = ENEMY_TABLE[type].hp;
    d->atk = ENEMY_TABLE[type].atk;
    d->def = ENEMY_TABLE[type].def;
    d->exp_reward = ENEMY_TABLE[type].exp_reward;
    d->gold_reward = ENEMY_TABLE[type].gold_reward;
    return e;
}

darken_entity spawn_item(ItemType type, s16 x, s16 y, s16 value, MapId map)
{
    darken_entity e = darken_spawn(&g_world);
    ItemData *d = (ItemData *)e->data;

    e->tag = KIND_ITEM;
    e->usr = (u16)map;
    e->state = (darken_state)passive_state;
    e->destructor = (darken_state)item_destructor;

    d->x = x;
    d->y = y;
    d->type = type;
    d->value = value;
    return e;
}

darken_entity spawn_npc(const char *name, s16 x, s16 y, MapId map)
{
    darken_entity e = darken_spawn(&g_world);
    NpcData *d = (NpcData *)e->data;

    e->tag = KIND_NPC;
    e->usr = (u16)map;
    e->state = (darken_state)passive_state;
    e->destructor = NULL;

    d->x = x;
    d->y = y;
    strncpy(d->name, name, sizeof(d->name) - 1);
    d->name[sizeof(d->name) - 1] = '\0';
    return e;
}

void spawn_world(void)
{
    g_player = spawn_player(2, 2);

    /* Overworld */
    spawn_npc("Mira", 2, 7, MAP_OVERWORLD);
    spawn_item(ITEM_POTION, 7, 5, 0, MAP_OVERWORLD);
    spawn_item(ITEM_POTION, 16, 2, 0, MAP_OVERWORLD);
    spawn_item(ITEM_GOLD, 12, 7, 8, MAP_OVERWORLD);
    spawn_enemy(ENEMY_SLIME, 8, 3, MAP_OVERWORLD);
    spawn_enemy(ENEMY_BAT, 16, 4, MAP_OVERWORLD);
    spawn_enemy(ENEMY_GOBLIN, 4, 5, MAP_OVERWORLD);
    spawn_enemy(ENEMY_GOBLIN, 14, 7, MAP_OVERWORLD);

    /* Dungeon */
    spawn_enemy(ENEMY_ORC, 9, 2, MAP_DUNGEON);
    spawn_enemy(ENEMY_GOBLIN, 13, 6, MAP_DUNGEON);
    spawn_enemy(ENEMY_GOBLIN, 2, 5, MAP_DUNGEON);
    spawn_enemy(ENEMY_DRAGON, 17, 7, MAP_DUNGEON);
    spawn_item(ITEM_POTION, 12, 2, 0, MAP_DUNGEON);
    spawn_item(ITEM_GOLD, 16, 3, 20, MAP_DUNGEON);

    /* Everything above was spawned active; this pauses every dungeon-tagged
     * entity and leaves overworld ones running, establishing the invariant
     * that only the current map's entities are ever active. */
    switch_map(MAP_OVERWORLD);
}

void player_gain_exp(s16 amount)
{
    PlayerData *p = (PlayerData *)g_player->data;
    p->exp += amount;

    while (p->exp >= p->level * 25)
    {
        p->exp -= p->level * 25;
        p->level++;
        p->hp_max += 8;
        p->mp_max += 4;
        p->atk += 2;
        p->def += 1;
        p->hp = p->hp_max;
        p->mp = p->mp_max;
        sprintf(g_msg2, "Level up! You are Lv%d now.", p->level);
    }
}
