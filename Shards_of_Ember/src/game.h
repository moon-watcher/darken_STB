/**
 * game.h — shared types and globals for "Shards of Ember", a small terminal
 * RPG built entirely on top of darken.h.
 *
 * ENGINE-USAGE OVERVIEW (read this first)
 * ----------------------------------------
 * We use ONE darken manager (g_world) for every live object in the game:
 * the player, wandering monsters, ground items, and the shopkeeper NPC.
 * They all share the same payload size (sizeof(EntityData), a union of
 * every concrete payload), and `entity->tag` says which union member is
 * actually live. That's what `tag` ("available to the user") is for.
 *
 * `entity->usr` is repurposed as a MapId: every non-player entity remembers
 * which map it belongs to. Switching maps (see world.c: switch_map) simply
 * pauses every active entity that isn't tagged for the destination map and
 * resumes every paused entity that is. Because darken guarantees a paused
 * entity's slot (and therefore its data) stays untouched until it's resumed
 * or deleted, the entire dungeon can sit "frozen" in the paused zone while
 * you explore the overworld, and pop back exactly as you left it.
 *
 * Enemy AI is two darken_state functions (enemy_state_patrol / _chase) that
 * hand control to each other by *returning* the other one's pointer — the
 * plain FSM-transition mechanism darken_state is built for. When a chasing
 * enemy steps onto the player it calls battle_start() and returns
 * DARKEN_PAUSE, so the engine itself parks the enemy in the paused zone on
 * its next tick.
 */

#ifndef GAME_H
#define GAME_H

#include <genesis.h>

#include "darken.h"

/* ---------------------------------------------------------------------- */
/* Tunables                                                                */
/* ---------------------------------------------------------------------- */

#define MAX_ENTITIES 40
#define MAP_W 20
#define MAP_H 10
#define AGGRO_RADIUS 4
#define DEAGGRO_RADIUS 7

/* ---------------------------------------------------------------------- */
/* Entity kinds (stored in entity->tag)                                    */
/* ---------------------------------------------------------------------- */

typedef enum
{
    KIND_PLAYER = 1,
    KIND_ENEMY,
    KIND_ITEM,
    KIND_NPC
} EntityKind;

typedef enum
{
    ENEMY_SLIME,
    ENEMY_BAT,
    ENEMY_GOBLIN,
    ENEMY_ORC,
    ENEMY_DRAGON /* final boss */
} EnemyType;

typedef enum
{
    ITEM_POTION,
    ITEM_GOLD
} ItemType;

typedef enum
{
    MAP_OVERWORLD = 0,
    MAP_DUNGEON = 1
} MapId;

typedef enum
{
    GS_EXPLORE,
    GS_BATTLE,
    GS_SHOP,
    GS_GAMEOVER,
    GS_WIN
} GameState;

/* ---------------------------------------------------------------------- */
/* Entity payloads                                                         */
/*                                                                          */
/* Every payload below starts with `int x; int y;` on purpose: that shared  */
/* prefix is a "common initial sequence" inside EntityData, so any of these */
/* structs may be used to read/write another variant's position — see      */
/* entity_pos()/entity_set_pos() in world.c.                                */
/* ---------------------------------------------------------------------- */

typedef struct
{
    int x, y;
    int hp, hp_max;
    int mp, mp_max;
    int atk, def;
    int level;
    int exp;
    int gold;
    int potions;
} PlayerData;

typedef struct
{
    int x, y;
    int hp, hp_max;
    int atk, def;
    int exp_reward, gold_reward;
    EnemyType type;
    int spawn_x, spawn_y; /* patrol leash origin */
} EnemyData;

typedef struct
{
    int x, y;
    ItemType type;
    int value;
} ItemData;

typedef struct
{
    int x, y;
    char name[16];
} NpcData;

typedef union
{
    PlayerData player;
    EnemyData enemy;
    ItemData item;
    NpcData npc;
} EntityData;

/* ---------------------------------------------------------------------- */
/* Globals (defined in main.c)                                             */
/* ---------------------------------------------------------------------- */

extern darken g_world;
extern darken_entity g_player;
extern GameState g_state;
extern MapId g_map;
extern darken_entity g_battle_enemy;
extern char g_msg[256];

/* ---------------------------------------------------------------------- */
/* Helper: recover the owning entity from a data pointer.                  */
/*                                                                          */
/* darken_state callbacks only receive entity->data. Since struct           */
/* darken_entity is a complete (non-opaque) type, we can walk back from the */
/* flexible array member to the containing entity with offsetof — the same */
/* stability guarantee the header documents ("an entity's own memory        */
/* address never moves") is what makes this safe to do from inside a state  */
/* callback.                                                                */
/* ---------------------------------------------------------------------- */
static inline darken_entity darken_entity_from_data(void *data)
{
    #include <stddef.h>
    
    return (darken_entity)((uint8_t *)data - offsetof(struct darken_entity, data));
}

/* world.c ---------------------------------------------------------------- */
extern char world_overworld[MAP_H][MAP_W + 1];
extern char world_dungeon[MAP_H][MAP_W + 1];

const char (*current_map(void))[MAP_W + 1];
int tile_blocked(int x, int y);
void entity_pos(darken_entity e, int *x, int *y);
void entity_set_pos(darken_entity e, int x, int y);
darken_entity entity_at(int x, int y);
void switch_map(MapId new_map);
void render_explore(void);
void try_move_player(int dx, int dy);
void try_interact(void);

/* entities.c --------------------------------------------------------------*/
darken_entity spawn_player(int x, int y);
darken_entity spawn_enemy(EnemyType type, int x, int y, MapId map);
darken_entity spawn_item(ItemType type, int x, int y, int value, MapId map);
darken_entity spawn_npc(const char *name, int x, int y, MapId map);
void spawn_world(void);
void player_gain_exp(int amount);
char enemy_glyph(EnemyType t);
const char *enemy_name(EnemyType t);

void *passive_state(void *data);
void *enemy_state_patrol(void *data);
void *enemy_state_chase(void *data);

/* battle.c ------------------------------------------------------------- */
void battle_start(darken_entity enemy);
void render_battle(void);
void battle_turn(int choice);

#endif /* GAME_H */
