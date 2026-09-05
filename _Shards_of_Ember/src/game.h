/**
 * game.h — shared types and globals for "Shards of Ember", built on darken.h
 * and running natively on SGDK (Sega Genesis / Mega Drive), GCC 13.2 / m68k.
 *
 * SGDK-ONLY RULE: this project never includes stdio.h, stdlib.h, or a host
 * string.h/time.h. There is no libc under a bare-metal m68k-elf target —
 * that's exactly why <stdio.h> failed to be found. Every file here only
 * does `#include "genesis.h"`, which already pulls in SGDK's own string.h
 * (sprintf/vsprintf/strcpy/...), memory.h (memcpy/memset), tools.h
 * (random/setRandomSeed), timer.h, joy.h and vdp*.h. darken.h itself still
 * uses <stdint.h>, which is fine: stdint.h/stddef.h are freestanding
 * headers the compiler provides itself, not part of libc.
 *
 * ENGINE-USAGE OVERVIEW
 * ----------------------------------------
 * One darken manager (g_world) holds every live object: player, monsters,
 * ground items, the shopkeeper NPC. They share one payload size
 * (sizeof(EntityData), a union of every concrete payload); `entity->tag`
 * says which union member is actually live, `entity->usr` says which map
 * the entity belongs to.
 *
 * Enemy AI is two darken_state functions (enemy_state_patrol/_chase) that
 * hand control to each other by *returning* the other one's pointer. When a
 * chasing enemy reaches the player it calls battle_start() and returns
 * DARKEN_PAUSE, so the engine parks it in the paused zone on its next tick.
 *
 * Changing maps (world.c: switch_map) pauses every entity that isn't
 * tagged for the destination map and resumes every one that is — no
 * spawning/destroying, relying on darken's guarantee that a paused
 * entity's data never moves until it's resumed or deleted.
 *
 * On game over / victory we call darken_reset() and respawn everything for
 * a clean "press START to play again" loop.
 */

#ifndef GAME_H
#define GAME_H

// #include <stddef.h> /* offsetof — freestanding header, provided by GCC itself */
#include <stdint.h> /* included here, before genesis.h's types.h, on purpose:
                      * darken.h also includes <stdint.h>. If SGDK's types.h
                      * ran first it would #define uint8_t/int8_t/... as
                      * macro aliases for its own u8/s8/... names, and the
                      * *later* processing of the real <stdint.h> would then
                      * get those macro names substituted into its own
                      * typedefs, redeclaring s8/u8/... with a conflicting
                      * type. Including <stdint.h> for real first avoids that
                      * — its typedefs are already registered by the time
                      * types.h's guarded #define block runs. */

#include "genesis.h"

#include "darken.h"

/* ---------------------------------------------------------------------- */
/* Tunables                                                                */
/* ---------------------------------------------------------------------- */

#define MAX_ENTITIES 40
#define MAP_W 20
#define MAP_H 10
#define MAP_X 2  /* tile column where the map is drawn on screen */
#define MAP_Y 2  /* tile row where the map is drawn on screen    */
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
    GS_TITLE,
    GS_EXPLORE,
    GS_STATUS,
    GS_BATTLE,
    GS_SHOP,
    GS_GAMEOVER,
    GS_WIN
} GameState;

/* ---------------------------------------------------------------------- */
/* Entity payloads                                                         */
/*                                                                          */
/* Every payload starts with `s16 x; s16 y;` on purpose: that shared        */
/* prefix is a "common initial sequence" inside EntityData, so any variant  */
/* may be used to read/write another variant's position — see               */
/* entity_pos()/entity_set_pos() in world.c.                                */
/* ---------------------------------------------------------------------- */

typedef struct
{
    s16 x, y;
    s16 hp, hp_max;
    s16 mp, mp_max;
    s16 atk, def;
    s16 level;
    s16 exp;
    s16 gold;
    s16 potions;
} PlayerData;

typedef struct
{
    s16 x, y;
    s16 hp, hp_max;
    s16 atk, def;
    s16 exp_reward, gold_reward;
    EnemyType type;
    s16 spawn_x, spawn_y; /* patrol leash origin */
} EnemyData;

typedef struct
{
    s16 x, y;
    ItemType type;
    s16 value;
} ItemData;

typedef struct
{
    s16 x, y;
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
extern char g_msg[40];
extern char g_msg2[40];

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
    return (darken_entity)((uint8_t *)data - (uint32_t)&((darken_entity)0)->data);
}

/* ui: small sprintf+VDP_drawText helper shared by every screen (world.c) */
void ui_printf(u16 x, u16 y, const char *fmt, ...);

/* world.c ---------------------------------------------------------------- */
extern char world_overworld[MAP_H][MAP_W + 1];
extern char world_dungeon[MAP_H][MAP_W + 1];

void build_maps(void);
const char (*current_map(void))[MAP_W + 1];
u16 tile_blocked(s16 x, s16 y);
void entity_pos(darken_entity e, s16 *x, s16 *y);
void entity_set_pos(darken_entity e, s16 x, s16 y);
darken_entity entity_at(s16 x, s16 y);
void switch_map(MapId new_map);
void render_explore(void);
void try_move_player(s16 dx, s16 dy);
void try_interact(void);

/* entities.c --------------------------------------------------------------*/
darken_entity spawn_player(s16 x, s16 y);
darken_entity spawn_enemy(EnemyType type, s16 x, s16 y, MapId map);
darken_entity spawn_item(ItemType type, s16 x, s16 y, s16 value, MapId map);
darken_entity spawn_npc(const char *name, s16 x, s16 y, MapId map);
void spawn_world(void);
void player_gain_exp(s16 amount);
char enemy_glyph(EnemyType t);
const char *enemy_name(EnemyType t);

void *passive_state(void *data);
void *enemy_state_patrol(void *data);
void *enemy_state_chase(void *data);

/* battle.c ------------------------------------------------------------- */
void battle_start(darken_entity enemy);
void render_battle(void);
void battle_input(u16 pressed);

/* shop.c (in main.c) ----------------------------------------------------- */
void render_shop(void);
void shop_input(u16 pressed);
void render_status(void);

#endif /* GAME_H */
