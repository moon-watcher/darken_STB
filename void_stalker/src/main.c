/* ============================================================================
 * DARKEN SECTOR — Shoot 'em up for SEGA GENESIS (SGDK)
 *
 * Compile with SGDK:
 *   %GDK_WIN%\bin\make -f %GDK_WIN%\makefile.gen
 *
 * Controls (Genesis pad):
 *   D-Pad      — Move ship
 *   A / B / C  — Shoot
 *   START      — Pause / Resume / Restart (Game Over)
 * ============================================================================ */

#define DARKEN_IMPLEMENTATION
#include "../../src/darken.h"
#include <genesis.h>

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define TILE_SIZE       8
#define SCREEN_W_T      40
#define SCREEN_H_T      28
#define GAME_H_T        25
#define MAX_ENTITIES    128
#define PAYLOAD_SIZE    32

#define TAG_PLAYER          1
#define TAG_PLAYER_BULLET   2
#define TAG_ENEMY           3
#define TAG_ENEMY_BULLET    4
#define TAG_POWERUP         5
#define TAG_PARTICLE        6
#define TAG_BOSS            7
#define TAG_BOSS_SHIELD     8

#define PWR_SPREAD      1
#define PWR_BOMB        2
#define PWR_SHIELD      3

#define TILE_EMPTY      0
#define TILE_SHIP       1
#define TILE_ENEMY0     2
#define TILE_ENEMY1     3
#define TILE_ENEMY2     4
#define TILE_BULLET_P   5
#define TILE_BULLET_E   6
#define TILE_POWERUP    7
#define TILE_BOSS       8
#define TILE_SHIELD     9
#define TILE_PARTICLE   10
#define TILE_STAR       11

/* ============================================================================
 * TILE GRAPHICS (8x8, 4bpp)
 * ============================================================================ */

static const u32 GFX_SHIP[8] = {
    0x00000000, 0x00010000, 0x00111000, 0x01101100,
    0x00111000, 0x00010000, 0x00111000, 0x00000000
};

static const u32 GFX_ENEMY0[8] = {
    0x00000000, 0x00000000, 0x00200200, 0x00222200,
    0x00222200, 0x00022000, 0x00022000, 0x00000000
};

static const u32 GFX_ENEMY1[8] = {
    0x00000000, 0x00030000, 0x00333000, 0x03333300,
    0x00333000, 0x00030000, 0x00000000, 0x00000000
};

static const u32 GFX_ENEMY2[8] = {
    0x00000000, 0x00444400, 0x04444440, 0x04444440,
    0x00444400, 0x00044000, 0x00044000, 0x00000000
};

static const u32 GFX_BULLET_P[8] = {
    0x00000000, 0x00000000, 0x00055000, 0x00055000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000
};

static const u32 GFX_BULLET_E[8] = {
    0x00000000, 0x00000000, 0x00066000, 0x00066000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000
};

static const u32 GFX_POWERUP[8] = {
    0x00000000, 0x00077000, 0x00777700, 0x00777700,
    0x00777700, 0x00077000, 0x00000000, 0x00000000
};

static const u32 GFX_BOSS[8] = {
    0x08888880, 0x88888888, 0x88888888, 0x88888888,
    0x88888888, 0x88888888, 0x88888888, 0x08888880
};

static const u32 GFX_SHIELD[8] = {
    0x00000000, 0x00099000, 0x00999900, 0x00999900,
    0x00999900, 0x00099000, 0x00000000, 0x00000000
};

static const u32 GFX_PARTICLE[8] = {
    0x00000000, 0x00000000, 0x00000000, 0x000AA000,
    0x000AA000, 0x00000000, 0x00000000, 0x00000000
};

static const u32 GFX_STAR[8] = {
    0x00000000, 0x00000000, 0x00000000, 0x000CC000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000
};

/* ============================================================================
 * PALETTE
 * ============================================================================ */

static const u16 PALETTE[16] = {
    0x0000, /* 0  transparent */
    0x0EEE, /* 1  white    */
    0x00E0, /* 2  green    */
    0x0E00, /* 3  red      */
    0x000E, /* 4  blue     */
    0x0EE0, /* 5  yellow   */
    0x0E08, /* 6  orange   */
    0x00EE, /* 7  cyan     */
    0x0888, /* 8  gray     */
    0x0E0E, /* 9  magenta  */
    0x0CCC, /* 10 light gray */
    0x0444, /* 11 dark gray  */
    0x0E44, /* 12 unused */
    0x0400, /* 13 unused */
    0x0040, /* 14 unused */
    0x0004  /* 15 unused */
};

/* ============================================================================
 * PAYLOAD STRUCTURES
 * ============================================================================ */

typedef struct {
    s16 x, y;
    s16 vx, vy;
    s16 hp;
    u8  power;
    u8  bombs;
    u8  inv_timer;
    u8  shield_timer;
} ship_data;

typedef struct {
    s16 x, y;
    s16 vx, vy;
} bullet_data;

typedef struct {
    s16 x, y;
    s16 origin_x;
    s16 t;
    s16 hp;
    u8  type;
    u8  shoot_timer;
} enemy_data;

typedef struct {
    s16 x, y;
    s16 vy;
    u8  kind;
    u8  blink;
} powerup_data;

typedef struct {
    s16 x, y;
    s16 vx, vy;
    u8  life;
    u8  color;
} particle_data;

typedef struct {
    s16 x, y;
    s16 hp;
    s16 max_hp;
    s16 t;
    u8  timer;
    darken_entity shield_left;
    darken_entity shield_right;
} boss_data;

/* ============================================================================
 * GAME STATE
 * ============================================================================ */

typedef struct {
    u16 score;
    u8  wave;
    u8  wave_timer;
    u8  boss_spawned;
    u8  game_over;
    u8  paused;
    u8  shake;
    u8  scroll_y;
} game_state;

static game_state G = {0};

static darken       world;
static DARKEN_STORAGE(world_storage, MAX_ENTITIES, PAYLOAD_SIZE);

static darken       fx_world;
static DARKEN_STORAGE(fx_storage, MAX_ENTITIES, PAYLOAD_SIZE);

static darken_entity player_entity = 0;

static s16 pending_x[8];
static s16 pending_y[8];
static u8  pending_count = 0;

static const s16 SIN_TABLE[32] = {
    0, 25, 49, 71, 90, 106, 117, 125, 128, 125, 117, 106, 90, 71, 49, 25,
    0, -25, -49, -71, -90, -106, -117, -125, -128, -125, -117, -106, -90, -71, -49, -25
};

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================ */

static void *state_player_alive(ship_data *s);
static void *state_player_invulnerable(ship_data *s);
static void *state_player_dead(ship_data *s);
static void *state_bullet_fly(bullet_data *b);
static void *state_enemy_sine(enemy_data *e);
static void *state_enemy_straight(enemy_data *e);
static void *state_enemy_shooter(enemy_data *e);
static void *state_boss_shield(enemy_data *e);
static void *state_boss_enter(boss_data *b);
static void *state_boss_attack(boss_data *b);
static void *state_boss_defeated(boss_data *b);
static void *state_powerup_fall(powerup_data *p);
static void *state_particle_fade(particle_data *p);

static void spawn_player_bullet(s16 x, s16 y, s16 vx, s16 vy);
static void spawn_enemy_bullet(s16 x, s16 y, s16 vx, s16 vy);
static void spawn_enemy(u8 etype, s16 x, s16 y);
static void spawn_boss(void);
static void spawn_particle(s16 x, s16 y, u8 color);

/* ============================================================================
 * VRAM HELPERS
 * ============================================================================ */

static inline s16 clamp_s16(s16 v, s16 lo, s16 hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static void load_tiles(void)
{
    VDP_loadTileData(GFX_SHIP,     TILE_SHIP,     1, 0);
    VDP_loadTileData(GFX_ENEMY0,   TILE_ENEMY0,   1, 0);
    VDP_loadTileData(GFX_ENEMY1,   TILE_ENEMY1,   1, 0);
    VDP_loadTileData(GFX_ENEMY2,   TILE_ENEMY2,   1, 0);
    VDP_loadTileData(GFX_BULLET_P, TILE_BULLET_P, 1, 0);
    VDP_loadTileData(GFX_BULLET_E, TILE_BULLET_E, 1, 0);
    VDP_loadTileData(GFX_POWERUP,  TILE_POWERUP,  1, 0);
    VDP_loadTileData(GFX_BOSS,     TILE_BOSS,     1, 0);
    VDP_loadTileData(GFX_SHIELD,   TILE_SHIELD,   1, 0);
    VDP_loadTileData(GFX_PARTICLE, TILE_PARTICLE, 1, 0);
    VDP_loadTileData(GFX_STAR,     TILE_STAR,     1, 0);
}

static void setup_background(void)
{
    u16 y, x;
    for (y = 0; y < 32; ++y) {
        for (x = 0; x < 64; ++x) {
            if ((random() % 16) == 0) {
                VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, TILE_STAR), x, y);
            } else {
                VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, TILE_EMPTY), x, y);
            }
        }
    }
}

static void clear_game_area(void)
{
    u16 y, x;
    for (y = 2; y < SCREEN_H_T; ++y) {
        for (x = 0; x < SCREEN_W_T; ++x) {
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, 0, 0, TILE_EMPTY), x, y);
        }
    }
}

static void draw_tile(s16 px, s16 py, u16 tile_idx)
{
    s16 tx = px >> 3;
    s16 ty = (py >> 3) + 2;
    if (tx >= 0 && tx < SCREEN_W_T && ty >= 2 && ty < SCREEN_H_T) {
        VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, 0, 0, tile_idx), (u16)tx, (u16)ty);
    }
}

/* ============================================================================
 * DESTRUCTORS
 * ============================================================================ */

static void destructor_enemy(enemy_data *ed)
{
    if (pending_count < 8) {
        pending_x[pending_count] = ed->x;
        pending_y[pending_count] = ed->y;
        pending_count++;
    }
    G.score += 10;
}

static void destructor_boss(boss_data *b)
{
    (void)b;
    G.score += 100;
    G.shake = 20;
    G.boss_spawned = 0;
    G.wave_timer = 120;
}

/* ============================================================================
 * ENTITY STATES
 * ============================================================================ */

static void *state_player_alive(ship_data *s)
{
    u16 joy = JOY_readJoypad(JOY_1);

    s->vx = 0; s->vy = 0;
    if (joy & BUTTON_LEFT)  s->vx = -2;
    if (joy & BUTTON_RIGHT) s->vx = 2;
    if (joy & BUTTON_UP)    s->vy = -2;
    if (joy & BUTTON_DOWN)  s->vy = 2;

    s->x = clamp_s16(s->x + s->vx, 8, 312);
    s->y = clamp_s16(s->y + s->vy, 16, 216);

    if ((joy & (BUTTON_A | BUTTON_B | BUTTON_C)) && s->inv_timer < 6) {
        if (s->power == 1) {
            spawn_player_bullet(s->x, s->y - 8, 0, -4);
        } else if (s->power == 2) {
            spawn_player_bullet(s->x - 6, s->y - 4, 0, -4);
            spawn_player_bullet(s->x + 6, s->y - 4, 0, -4);
        } else {
            spawn_player_bullet(s->x, s->y - 8, 0, -4);
            spawn_player_bullet(s->x - 8, s->y - 4, -1, -3);
            spawn_player_bullet(s->x + 8, s->y - 4, 1, -3);
        }
        s->inv_timer = 8;
    }
    if (s->inv_timer > 0) s->inv_timer--;
    if (s->shield_timer > 0) s->shield_timer--;

    return DARKEN_LOOP;
}

static void *state_player_invulnerable(ship_data *s)
{
    s->x = clamp_s16(s->x + s->vx, 8, 312);
    s->y = clamp_s16(s->y + s->vy, 16, 216);
    if (--s->inv_timer == 0)
        return state_player_alive;
    return DARKEN_LOOP;
}

static void *state_player_dead(ship_data *s)
{
    (void)s;
    G.game_over = 1;
    return DARKEN_LOOP;
}

static void *state_bullet_fly(bullet_data *b)
{
    b->x += b->vx;
    b->y += b->vy;
    if (b->x < 0 || b->x >= 320 || b->y < 0 || b->y >= 224)
        return DARKEN_DELETE;
    return DARKEN_LOOP;
}

static void *state_enemy_sine(enemy_data *e)
{
    e->t = (e->t + 1) & 31;
    e->x = e->origin_x + (SIN_TABLE[e->t] >> 3);
    e->y += 1;
    if (e->y > 224)
        return DARKEN_DELETE;
    return DARKEN_LOOP;
}

static void *state_enemy_straight(enemy_data *e)
{
    e->y += 2;
    if (e->y > 224)
        return DARKEN_DELETE;
    return DARKEN_LOOP;
}

static void *state_enemy_shooter(enemy_data *e)
{
    e->y += 1;
    if (++e->shoot_timer > 50) {
        e->shoot_timer = 0;
        if (player_entity && DARKEN_ENTITY_IN_ACTIVE(player_entity)) {
            ship_data *ps = (ship_data *)player_entity->data;
            s16 dx = ps->x - e->x;
            s16 dy = ps->y - e->y;
            s16 dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (dist > 0 && dist < 200) {
                s16 vx = (dx * 2) / dist;
                s16 vy = (dy * 2) / dist;
                if (vy < 1) vy = 1;
                spawn_enemy_bullet(e->x, e->y, vx, vy);
            }
        }
    }
    if (e->y > 224)
        return DARKEN_DELETE;
    return DARKEN_LOOP;
}

static void *state_boss_shield(enemy_data *e)
{
    (void)e;
    return DARKEN_LOOP;
}

static void *state_boss_enter(boss_data *b)
{
    b->y += 1;
    if (b->y > 40)
        return state_boss_attack;
    return DARKEN_LOOP;
}

static void *state_boss_attack(boss_data *b)
{
    b->t = (b->t + 1) & 31;
    b->x = 160 + (SIN_TABLE[b->t] >> 1);

    if (++b->timer > 45) {
        b->timer = 0;
        spawn_enemy_bullet(b->x, b->y + 8, 0, 2);
        spawn_enemy_bullet(b->x - 8, b->y + 4, -1, 2);
        spawn_enemy_bullet(b->x + 8, b->y + 4, 1, 2);
    }

    if (b->shield_left && DARKEN_STATE_IS_ACTIVE(b->shield_left->state)) {
        enemy_data *sl = (enemy_data *)b->shield_left->data;
        sl->x = b->x - 24;
        sl->y = b->y;
    }
    if (b->shield_right && DARKEN_STATE_IS_ACTIVE(b->shield_right->state)) {
        enemy_data *sr = (enemy_data *)b->shield_right->data;
        sr->x = b->x + 24;
        sr->y = b->y;
    }

    if (b->hp <= 0)
        return state_boss_defeated;
    return DARKEN_LOOP;
}

static void *state_boss_defeated(boss_data *b)
{
    b->y += 2;
    if ((random() % 4) == 0) {
        spawn_particle(b->x + (random() % 20) - 10, b->y + (random() % 12) - 6, 1);
    }
    if (b->y > 250)
        return DARKEN_DELETE;
    return DARKEN_LOOP;
}

static void *state_powerup_fall(powerup_data *p)
{
    p->y += p->vy;
    p->blink++;
    if (p->y > 224)
        return DARKEN_DELETE;
    return DARKEN_LOOP;
}

static void *state_particle_fade(particle_data *p)
{
    p->x += p->vx;
    p->y += p->vy;
    if (--p->life == 0)
        return DARKEN_DELETE;
    return DARKEN_LOOP;
}

/* ============================================================================
 * SPAWNER FUNCTIONS
 * ============================================================================ */

static void spawn_player_bullet(s16 x, s16 y, s16 vx, s16 vy)
{
    darken_entity b = darken_spawn(&world);
    if (!b) return;
    bullet_data *bul = (bullet_data *)b->data;
    bul->x = x; bul->y = y;
    bul->vx = vx; bul->vy = vy;
    b->state = (darken_state)state_bullet_fly;
    b->tag = TAG_PLAYER_BULLET;
}

static void spawn_enemy_bullet(s16 x, s16 y, s16 vx, s16 vy)
{
    darken_entity b = darken_spawn(&world);
    if (!b) return;
    bullet_data *bul = (bullet_data *)b->data;
    bul->x = x; bul->y = y;
    bul->vx = vx; bul->vy = vy;
    b->state = (darken_state)state_bullet_fly;
    b->tag = TAG_ENEMY_BULLET;
}

static void spawn_enemy(u8 etype, s16 x, s16 y)
{
    darken_entity e = darken_spawn(&world);
    if (!e) return;
    enemy_data *ed = (enemy_data *)e->data;
    ed->x = x; ed->y = y;
    ed->origin_x = x;
    ed->t = 0;
    ed->hp = (etype == 2) ? 3 : 1;
    ed->type = etype;
    ed->shoot_timer = 0;
    e->destructor = (darken_state)destructor_enemy;
    e->tag = TAG_ENEMY;

    switch (etype) {
        case 0: e->state = (darken_state)state_enemy_sine;     break;
        case 1: e->state = (darken_state)state_enemy_straight; break;
        case 2: e->state = (darken_state)state_enemy_shooter;  break;
    }
}

static void spawn_powerup(s16 x, s16 y)
{
    darken_entity e = darken_spawn(&world);
    if (!e) return;
    powerup_data *pwr = (powerup_data *)e->data;
    pwr->x = x; pwr->y = y;
    pwr->vy = 1;
    pwr->kind = 1 + (random() % 3);
    pwr->blink = 0;
    e->state = (darken_state)state_powerup_fall;
    e->tag = TAG_POWERUP;
}

static void spawn_particle(s16 x, s16 y, u8 color)
{
    darken_entity p = darken_spawn(&fx_world);
    if (!p) return;
    particle_data *pt = (particle_data *)p->data;
    pt->x = x; pt->y = y;
    pt->vx = (random() % 5) - 2;
    pt->vy = (random() % 5) - 2;
    pt->life = 10 + (random() % 10);
    pt->color = color;
    p->state = (darken_state)state_particle_fade;
    p->tag = TAG_PARTICLE;
}

static void spawn_boss(void)
{
    darken_entity boss = darken_spawn(&world);
    if (!boss) return;
    boss_data *b = (boss_data *)boss->data;
    b->x = 160; b->y = -16;
    b->hp = 60; b->max_hp = 60;
    b->t = 0; b->timer = 0;
    b->shield_left = 0;
    b->shield_right = 0;
    boss->state = (darken_state)state_boss_enter;
    boss->destructor = (darken_state)destructor_boss;
    boss->tag = TAG_BOSS;

    darken_entity sl = darken_spawn(&world);
    if (sl) {
        enemy_data *sed = (enemy_data *)sl->data;
        sed->x = b->x - 24; sed->y = b->y;
        sed->hp = 15;
        sl->state = (darken_state)state_boss_shield;
        sl->tag = TAG_BOSS_SHIELD;
        b->shield_left = sl;
    }
    darken_entity sr = darken_spawn(&world);
    if (sr) {
        enemy_data *sed = (enemy_data *)sr->data;
        sed->x = b->x + 24; sed->y = b->y;
        sed->hp = 15;
        sr->state = (darken_state)state_boss_shield;
        sr->tag = TAG_BOSS_SHIELD;
        b->shield_right = sr;
    }

    G.boss_spawned = 1;
}

/* ============================================================================
 * COLLISION SYSTEM
 * ============================================================================ */

static int rect_hit(s16 x1, s16 y1, s16 w1, s16 h1, s16 x2, s16 y2, s16 w2, s16 h2)
{
    return (x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2);
}

static void check_collisions(void)
{
    s16 i, j;

    /* Player bullets vs enemies/boss/shields */
    for (i = world.size - 1; i >= 0; --i) {
        darken_entity bul = world.pool[i];
        if (bul->tag != TAG_PLAYER_BULLET) continue;
        bullet_data *bd = (bullet_data *)bul->data;

        for (j = world.size - 1; j >= 0; --j) {
            darken_entity target = world.pool[j];
            if (target->tag != TAG_ENEMY && target->tag != TAG_BOSS && target->tag != TAG_BOSS_SHIELD)
                continue;

            int hit = 0;
            if (target->tag == TAG_ENEMY) {
                enemy_data *ed = (enemy_data *)target->data;
                if (rect_hit(bd->x - 2, bd->y - 2, 4, 4, ed->x - 4, ed->y - 4, 8, 8)) {
                    ed->hp--;
                    if (ed->hp <= 0) darken_entity_delete(target);
                    hit = 1;
                }
            } else if (target->tag == TAG_BOSS) {
                boss_data *bd2 = (boss_data *)target->data;
                if (rect_hit(bd->x - 2, bd->y - 2, 4, 4, bd2->x - 12, bd2->y - 8, 24, 16)) {
                    bd2->hp--;
                    hit = 1;
                }
            } else if (target->tag == TAG_BOSS_SHIELD) {
                enemy_data *sed = (enemy_data *)target->data;
                if (rect_hit(bd->x - 2, bd->y - 2, 4, 4, sed->x - 4, sed->y - 4, 8, 8)) {
                    sed->hp--;
                    if (sed->hp <= 0) darken_entity_delete(target);
                    hit = 1;
                }
            }

            if (hit) {
                darken_entity_delete(bul);
                break;
            }
        }
    }

    /* Enemy bullets / enemies / boss vs player */
    if (!player_entity || !DARKEN_ENTITY_IN_ACTIVE(player_entity))
        return;

    ship_data *ps = (ship_data *)player_entity->data;
    if (ps->inv_timer > 0 || ps->shield_timer > 0)
        return;

    s16 px = ps->x - 4, py = ps->y - 4;

    for (i = world.size - 1; i >= 0; --i) {
        darken_entity e = world.pool[i];
        int hit = 0;

        if (e->tag == TAG_ENEMY_BULLET) {
            bullet_data *bd = (bullet_data *)e->data;
            if (rect_hit(px, py, 8, 8, bd->x - 2, bd->y - 2, 4, 4)) hit = 1;
        } else if (e->tag == TAG_ENEMY) {
            enemy_data *ed = (enemy_data *)e->data;
            if (rect_hit(px, py, 8, 8, ed->x - 4, ed->y - 4, 8, 8)) hit = 1;
        } else if (e->tag == TAG_BOSS) {
            boss_data *bd = (boss_data *)e->data;
            if (rect_hit(px, py, 8, 8, bd->x - 12, bd->y - 8, 24, 16)) hit = 1;
        } else if (e->tag == TAG_POWERUP) {
            powerup_data *pwr = (powerup_data *)e->data;
            if (rect_hit(px, py, 8, 8, pwr->x - 4, pwr->y - 4, 8, 8)) {
                if (pwr->kind == PWR_SPREAD && ps->power < 3) ps->power++;
                else if (pwr->kind == PWR_BOMB && ps->bombs < 5) ps->bombs++;
                else if (pwr->kind == PWR_SHIELD) ps->shield_timer = 120;
                G.score += 5;
                darken_entity_delete(e);
                continue;
            }
        }

        if (hit) {
            ps->hp--;
            ps->inv_timer = 90;
            G.shake = 10;
            for (int k = 0; k < 6; ++k) spawn_particle(ps->x, ps->y, 0);

            if (e->tag == TAG_ENEMY_BULLET) darken_entity_delete(e);

            if (ps->hp <= 0) {
                player_entity->state = (darken_state)state_player_dead;
            } else {
                player_entity->state = (darken_state)state_player_invulnerable;
            }
        }
    }
}

/* ============================================================================
 * WAVE SYSTEM
 * ============================================================================ */

static void update_wave(void)
{
    static u16 spawn_timer = 0;
    static u16 spawned = 0;
    static u16 to_spawn = 15;

    if (G.game_over || G.paused || G.boss_spawned) return;
    if (G.wave_timer > 0) { G.wave_timer--; return; }

    spawn_timer++;
    if (spawn_timer < 25) return;
    spawn_timer = 0;

    if (spawned < to_spawn) {
        u8 et = ((G.wave % 3 == 2) && (spawned % 5 == 0)) ? 2 : (spawned & 1);
        s16 x = 16 + (random() % 288);
        spawn_enemy(et, x, -8);
        spawned++;
    } else {
        u8 alive = 0;
        DARKEN_FOREACH(&world, {
            if (ENTITY->tag == TAG_ENEMY) alive++;
        });
        if (alive == 0) {
            spawn_boss();
            spawned = 0;
            to_spawn = 15 + G.wave * 3;
            G.wave++;
        }
    }
}

/* ============================================================================
 * RENDERING
 * ============================================================================ */

static void render_hud(void)
{
    char buf[32];

    sprintf(buf, "SCORE:%05u", G.score);
    VDP_drawText(buf, 1, 0);

    sprintf(buf, "WAVE:%02u", G.wave);
    VDP_drawText(buf, 30, 0);

    if (player_entity && DARKEN_ENTITY_IN_USED(player_entity)) {
        ship_data *ps = (ship_data *)player_entity->data;
        sprintf(buf, "HP:%d B:%d", ps->hp, ps->bombs);
        VDP_drawText(buf, 1, 1);
    }

    if (G.paused) {
        VDP_drawText("PAUSED", 17, 13);
    }
    if (G.game_over) {
        VDP_drawText("GAME OVER", 15, 12);
        VDP_drawText("PRESS START", 14, 14);
    }
}

static void render_entities(void)
{
    s16 ox = 0, oy = 0;

    clear_game_area();

    if (G.shake > 0) {
        G.shake--;
        ox = (random() % 5) - 2;
        oy = (random() % 5) - 2;
    }

    DARKEN_FOREACH(&world, {
        switch (ENTITY->tag) {
            case TAG_PLAYER: {
                ship_data *s = (ship_data *)ENTITY->data;
                if (s->inv_timer > 0 && ((s->inv_timer >> 2) & 1)) break;
                draw_tile(s->x + ox, s->y + oy, TILE_SHIP);
                break;
            }
            case TAG_PLAYER_BULLET: {
                bullet_data *b = (bullet_data *)ENTITY->data;
                draw_tile(b->x + ox, b->y + oy, TILE_BULLET_P);
                break;
            }
            case TAG_ENEMY: {
                enemy_data *ed = (enemy_data *)ENTITY->data;
                u16 tile = (ed->type == 0) ? TILE_ENEMY0 : (ed->type == 1) ? TILE_ENEMY1 : TILE_ENEMY2;
                draw_tile(ed->x + ox, ed->y + oy, tile);
                break;
            }
            case TAG_ENEMY_BULLET: {
                bullet_data *b = (bullet_data *)ENTITY->data;
                draw_tile(b->x + ox, b->y + oy, TILE_BULLET_E);
                break;
            }
            case TAG_POWERUP: {
                powerup_data *pwr = (powerup_data *)ENTITY->data;
                if ((pwr->blink >> 2) & 1)
                    draw_tile(pwr->x + ox, pwr->y + oy, TILE_POWERUP);
                break;
            }
            case TAG_BOSS: {
                boss_data *b = (boss_data *)ENTITY->data;
                draw_tile(b->x + ox, b->y + oy, TILE_BOSS);
                break;
            }
            case TAG_BOSS_SHIELD: {
                enemy_data *sed = (enemy_data *)ENTITY->data;
                draw_tile(sed->x + ox, sed->y + oy, TILE_SHIELD);
                break;
            }
        }
    });

    DARKEN_FOREACH(&fx_world, {
        particle_data *p = (particle_data *)ENTITY->data;
        draw_tile(p->x + ox, p->y + oy, TILE_PARTICLE);
    });
}

/* ============================================================================
 * GAME LOOP
 * ============================================================================ */

static void reset_game(void)
{
    darken_reset(&world);
    darken_reset(&fx_world);

    G.score = 0;
    G.wave = 1;
    G.wave_timer = 60;
    G.boss_spawned = 0;
    G.game_over = 0;
    G.paused = 0;
    G.shake = 0;
    pending_count = 0;

    player_entity = darken_spawn(&world);
    ship_data *s = (ship_data *)player_entity->data;
    s->x = 160; s->y = 200;
    s->hp = 3;
    s->power = 1;
    s->bombs = 2;
    s->inv_timer = 0;
    s->shield_timer = 0;
    player_entity->state = (darken_state)state_player_alive;
    player_entity->tag = TAG_PLAYER;
}

static void handle_input(void)
{
    static u8 start_prev = 0;
    u16 joy = JOY_readJoypad(JOY_1);

    if (joy & BUTTON_START) {
        if (!start_prev) {
            if (G.game_over) {
                reset_game();
            } else {
                G.paused = !G.paused;
                if (G.paused) {
                    DARKEN_FOREACH(&world, {
                        if (ENTITY != player_entity)
                            darken_entity_pause(ENTITY);
                    });
                    DARKEN_FOREACH(&fx_world, {
                        darken_entity_pause(ENTITY);
                    });
                } else {
                    while (world.paused < world.capacity)
                        darken_entity_resume(world.pool[world.paused]);
                    while (fx_world.paused < fx_world.capacity)
                        darken_entity_resume(fx_world.pool[fx_world.paused]);
                }
            }
        }
        start_prev = 1;
    } else {
        start_prev = 0;
    }
}

int main(bool hardReset)
{
    (void)hardReset;

    VDP_setScreenWidth320();
    VDP_setPlaneSize(64, 32, TRUE);
    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);

    PAL_setColors(0, PALETTE, 16, CPU);
    load_tiles();
    setup_background();
    VDP_clearPlane(BG_A, TRUE);

    darken_init(&world, DARKEN_ARGS(world_storage));
    darken_init(&fx_world, DARKEN_ARGS(fx_storage));

    reset_game();

    while (TRUE) {
        handle_input();

        if (!G.paused && !G.game_over) {
            darken_update(&world);
            darken_update(&fx_world);

            {
                u8 i, k;
                for (i = 0; i < pending_count; ++i) {
                    for (k = 0; k < 4; ++k)
                        spawn_particle(pending_x[i], pending_y[i], 1);
                }
                pending_count = 0;
            }

            check_collisions();
            update_wave();

            G.scroll_y = (G.scroll_y + 1) & 0xFF;
            VDP_setVerticalScrollTile(BG_B, 0, &G.scroll_y, 1, CPU);
        }

        render_entities();
        render_hud();

        SYS_doVBlankProcess();
    }

    return 0;
}
