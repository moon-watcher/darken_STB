/* ============================================================================
 * main.c — "Darken Tactics"
 *
 * A tiny turn-based tactical RPG demo for Sega Genesis / Mega Drive (SGDK),
 * in the spirit of Shining Force 2: a grid battlefield, a party of heroes,
 * a squad of enemies, move + attack on your turn, then the enemy AI takes
 * its turn. It exists to show darken.h doing real work in a genre where
 * "entity" doesn't mean "autonomous actor" so much as "thing a turn
 * manager pokes from the outside" — which is exactly the case DARKEN_DIRECT
 * mode is built for.
 *
 * WHY DARKEN_DIRECT (and not the default STATE-MACHINE mode)
 * ------------------------------------------------------------
 * Units here don't really transition between self-contained states the
 * way a state-machine callback wants to (return the next state, return
 * DARKEN_DELETE, ...). Almost everything that happens to a unit is
 * commanded from outside: the turn manager decides when it moves, when it
 * attacks, when it's done for the round (paused) and when it dies
 * (deleted). That means nearly every call site needs the darken_entity
 * handle in hand — DARKEN_DIRECT hands it to update()/destroy() directly,
 * so we're not constantly reaching for DARKEN_ENTITY(data) to recover it.
 *
 * WHAT THIS EXAMPLE ACTUALLY EXERCISES FROM darken.h
 * ------------------------------------------------------------
 * - DARKEN_POOL_DECLARE / DARKEN_POOL_INIT — a single static pool holds
 *   every unit on the field, both teams together.
 * - DARKEN_SPAWN — creating heroes and monsters.
 * - update()/destroy() in DIRECT mode — see unit_update()/unit_destroy().
 * - darken_entity_pause()/darken_entity_resume() — repurposed as "has this
 *   unit already acted this round?" (see the big comment above begin_phase).
 * - darken_entity_delete() — unit death, and a real gotcha around it (see
 *   the comment above kill_unit()).
 * - DARKEN_FOREACH — driving the enemy AI, one unit at a time, in the same
 *   loop that's normally "just" the per-frame update.
 * - DARKEN_DATA / the raw `darken` struct fields — manually walking the
 *   paused zone, which DARKEN_FOREACH deliberately never visits.
 * - The library's pointer-stability guarantee — `active_unit` below is a
 *   darken_entity held across many frames while the player deliberates,
 *   which is only safe because darken.h promises an entity's address never
 *   moves while it's paused, active, or resumed.
 *
 * BUILDING THIS
 * ------------------------------------------------------------
 * This needs the real SGDK toolchain (m68k-elf-gcc + rescomp), which isn't
 * available in the environment this example was written in — so unlike
 * darken.h itself (which was extensively compiled and unit-tested on a
 * regular PC compiler throughout its development), this specific file has
 * only been carefully reviewed against SGDK's current API, not compiled.
 * See EXAMPLE_README.md for build steps and a full rundown of what's been
 * verified vs. what to double check.
 *
 * CONTROLS
 * ------------------------------------------------------------
 *   D-Pad   move the cursor (or the selected unit, mid-move)
 *   A       select a unit / confirm a move / confirm an attack
 *   B       cancel a move (while choosing where to move)
 *   START   end your phase early, even with units left to act
 * ============================================================================
 */

#include <genesis.h>

#define DARKEN_DIRECT
#define DARKEN_IMPLEMENTATION
#include "darken.h"

#include "res/resources.h" /* rescomp output: battlefield, hero_spr, enemy_spr, cursor_spr */

/* ----------------------------------------------------------------------
 * Battlefield grid
 * -------------------------------------------------------------------- */

#define GRID_W        10
#define GRID_H        7
#define TILE_SIZE     24
#define GRID_ORIGIN_X 16
#define GRID_ORIGIN_Y 16

#define grid_px_x(gx) (GRID_ORIGIN_X + (gx) * TILE_SIZE)
#define grid_px_y(gy) (GRID_ORIGIN_Y + (gy) * TILE_SIZE)

#define MAX_UNITS 8 /* 3 heroes + 3 monsters + a little headroom */

/* ----------------------------------------------------------------------
 * Unit payload — this is the `data[]` Darken hands back to us
 * -------------------------------------------------------------------- */

typedef enum
{
    TEAM_PLAYER,
    TEAM_ENEMY
} Team;

typedef enum
{
    CLASS_WARRIOR,
    CLASS_ARCHER,
    CLASS_MAGE,
    CLASS_GOBLIN,
    CLASS_SKELETON
} UnitClass;

struct unit
{
    Team        team;
    UnitClass   cls;
    const char *name;
    s16         hp, hp_max;
    s16         atk, def;
    s16         move_range;
    s16         atk_range; /* 1 = melee, 2+ = ranged */
    u8          gx, gy;    /* grid position */
    Sprite     *spr;       /* this unit's hardware sprite */
};

/* One pool, both teams. Darken doesn't care what's *in* the payload or
   what it means — `team` here is just a field we defined, same as x/y or
   hp. Nothing about darken.h treats player units and enemy units any
   differently; every distinction below is us filtering by u->team. */
DARKEN_POOL_DECLARE(unit_storage, MAX_UNITS, sizeof(struct unit));
static darken units = DARKEN_POOL_INIT(unit_storage);

/* ----------------------------------------------------------------------
 * Turn / selection state
 * -------------------------------------------------------------------- */

typedef enum
{
    PHASE_PLAYER,
    PHASE_ENEMY
} Phase;

typedef enum
{
    PS_SELECT_UNIT,   /* free cursor, waiting to pick a unit to command */
    PS_SELECT_TILE,   /* moving the selected unit's cursor within range */
    PS_CONFIRM_ATTACK /* a target is in range, waiting for A/B          */
} PlayerState;

static Phase       phase  = PHASE_PLAYER;
static PlayerState pstate = PS_SELECT_UNIT;

static u8 cursor_x = 1, cursor_y = 3;
static Sprite *cursor_sprite;

/* The unit currently being commanded, and where it started this action
   (so a cancelled move can put it back). Held across many frames while
   the player deliberates — safe only because darken.h guarantees this
   handle's target never moves in memory while paused/active/resumed. */
static darken_entity active_unit    = 0;
static u8             active_start_x = 0, active_start_y = 0;
static darken_entity  attack_target = 0;

/* ----------------------------------------------------------------------
 * Forward declarations (the call graph isn't a straight line)
 * -------------------------------------------------------------------- */

static void begin_phase(Phase p);
static void run_enemy_phase(void);
static void resolve_attack(darken_entity attacker_e, darken_entity target_e);
static void check_victory(void);

/* ----------------------------------------------------------------------
 * Small helpers
 * -------------------------------------------------------------------- */

static s16 abs16(s16 v) { return (v < 0) ? -v : v; }

static u16 manhattan(u8 ax, u8 ay, u8 bx, u8 by)
{
    return (u16)(abs16((s16)ax - (s16)bx) + abs16((s16)ay - (s16)by));
}

/* darken_entity is `struct darken_entity *`; DARKEN_DATA gets a typed
   pointer to the payload. Wrapped here purely for readability below. */
static struct unit *unit_of(darken_entity e) { DARKEN_DATA(struct unit, u, e); return u; }

static void show_message(const char *msg)
{
    char buf[33];
    u16  i = 0;
    while (msg[i] && i < 32) { buf[i] = msg[i]; i++; }
    while (i < 32) buf[i++] = ' ';
    buf[32] = 0;
    VDP_drawText(buf, 3, 25);
}

static void show_status(const char *msg)
{
    char buf[33];
    u16  i = 0;
    while (msg[i] && i < 32) { buf[i] = msg[i]; i++; }
    while (i < 32) buf[i++] = ' ';
    buf[32] = 0;
    VDP_drawText(buf, 3, 24);
}

static void wait_frames(u16 n)
{
    while (n--)
    {
        SPR_update();
        SYS_doVBlankProcess();
    }
}

/* ----------------------------------------------------------------------
 * Seeing every living unit, not just the active ones
 * ----------------------------------------------------------------------
 * DARKEN_FOREACH only ever walks the ACTIVE zone [0, size) — that's the
 * whole point of the paused zone, it's meant to be invisible to it. But
 * we're repurposing "paused" to mean "already acted this round" (see
 * begin_phase below), so a paused unit is still very much alive and still
 * occupies a tile. Anything that needs to see EVERY living unit —
 * occupancy checks, targeting, victory conditions — has to walk both the
 * active zone [0, size) and the paused zone [paused, capacity) by hand,
 * using the `darken` struct's public fields directly. That's exactly what
 * DARKEN_FOREACH itself does internally for the active half.
 * -------------------------------------------------------------------- */

static darken_entity unit_at(u8 gx, u8 gy)
{
    u16 i;
    for (i = 0; i < units.size; i++)
    {
        darken_entity e = units.pool[i];
        if (unit_of(e)->gx == gx && unit_of(e)->gy == gy) return e;
    }
    for (i = units.paused; i < units.capacity; i++)
    {
        darken_entity e = units.pool[i];
        if (unit_of(e)->gx == gx && unit_of(e)->gy == gy) return e;
    }
    return 0;
}

static u16 count_team_alive(Team team)
{
    u16 i, n = 0;
    for (i = 0; i < units.size; i++)
        if (unit_of(units.pool[i])->team == team) n++;
    for (i = units.paused; i < units.capacity; i++)
        if (unit_of(units.pool[i])->team == team) n++;
    return n;
}

/* Nearest unit that does NOT belong to `team` — i.e. "nearest enemy of
   `team`". Used both by the player (find a target near the unit they just
   moved) and by the AI (find the nearest hero to walk towards). */
static darken_entity nearest_enemy_of(Team team, u8 gx, u8 gy)
{
    darken_entity best      = 0;
    u16           best_dist = 0xFFFF;
    u16           i;

    for (i = 0; i < units.size; i++)
    {
        darken_entity e = units.pool[i];
        struct unit  *u = unit_of(e);
        if (u->team == team) continue;
        u16 d = manhattan(gx, gy, u->gx, u->gy);
        if (d < best_dist) { best_dist = d; best = e; }
    }
    for (i = units.paused; i < units.capacity; i++)
    {
        darken_entity e = units.pool[i];
        struct unit  *u = unit_of(e);
        if (u->team == team) continue;
        u16 d = manhattan(gx, gy, u->gx, u->gy);
        if (d < best_dist) { best_dist = d; best = e; }
    }
    return best;
}

/* Resume every paused unit belonging to `team`, leaving the other team's
   paused units untouched.
   ----------------------------------------------------------------------
   This looks like it should be a single forward scan over the paused
   zone, resuming matches as we go — it isn't, and it's worth knowing why,
   because it's the one place in this file doing something darken.h
   itself doesn't hand you a macro for.

   darken_entity_resume() swaps the entity out of the paused zone via the
   CURRENT `paused` boundary, then into the active zone via the CURRENT
   `size` boundary. Whichever entity used to sit at that paused boundary
   gets moved into the slot we just vacated — and that replacement isn't
   guaranteed to still be a paused entity if our target wasn't already
   sitting at the boundary itself. A naive "check index i, resume or i++"
   loop can end up re-examining a slot that just became a *free* slot,
   reading whichever stale entity happens to be sitting there. This was
   verified with a real (if silly) fuzz test against darken.h during
   development: a naive version of this loop failed (hit an iteration
   guard meant to catch infinite loops) in just over half of 1000 random
   team/order combinations.

   The fix is to never trust index bookkeeping across a resume: rescan
   from the current `paused` boundary after every single resume. */
static void resume_team(Team team)
{
    bool found;
    do
    {
        found = FALSE;
        u16 i;
        for (i = units.paused; i < units.capacity; i++)
        {
            if (unit_of(units.pool[i])->team == team)
            {
                darken_entity_resume(units.pool[i]);
                found = TRUE;
                break; /* the paused zone just changed shape, start over */
            }
        }
    } while (found);
}

/* ----------------------------------------------------------------------
 * Darken callbacks — DIRECT mode: void callback(darken_entity, void *)
 * -------------------------------------------------------------------- */

/* Runs every frame for every ACTIVE unit. In a turn-based game the actual
   gameplay logic is driven by the turn manager, not by a per-frame
   callback — so update() has one small, honest job: keep the hardware
   sprite in sync with the logical grid position, for whichever unit(s)
   just moved this frame. */
static void unit_update(darken_entity entity, struct unit *u)
{
    (void)entity;
    SPR_setPosition(u->spr, grid_px_x(u->gx), grid_px_y(u->gy));
}

/* Called once, right before a unit is actually removed from the pool.
   Its only job is to give back the hardware sprite slot — darken.h
   doesn't know SGDK sprites exist, that link only lives in *our* payload. */
static void unit_destroy(darken_entity entity, struct unit *u)
{
    (void)entity;
    SPR_releaseSprite(u->spr);
}

/* darken_entity_delete() only calls destroy() for entities that are
   ACTIVE at the moment of the call (see the comment on darken_entity_
   delete() in darken.h). We use "paused" to mean "already acted this
   round" — so a unit killed while paused (very normal: the enemy team
   acts entirely while every hero is paused from having already gone this
   round) would otherwise skip its destroy() callback and leak its sprite.
   Resuming right before deleting guarantees destroy() always runs. */
static void kill_unit(darken_entity e)
{
    darken_entity_resume(e);
    darken_entity_delete(e);
}

/* ----------------------------------------------------------------------
 * Spawning
 * -------------------------------------------------------------------- */

static darken_entity spawn_unit(Team team, UnitClass cls, u8 gx, u8 gy)
{
    darken_entity e = DARKEN_SPAWN(&units);
    if (!e) return 0; /* pool full */

    struct unit *u = unit_of(e);
    memset(u, 0, sizeof(*u));

    u->team = team;
    u->cls  = cls;
    u->gx   = gx;
    u->gy   = gy;

    switch (cls)
    {
        case CLASS_WARRIOR:
            u->name = "Warrior"; u->hp = u->hp_max = 24;
            u->atk = 8; u->def = 4; u->move_range = 3; u->atk_range = 1;
            break;
        case CLASS_ARCHER:
            u->name = "Archer"; u->hp = u->hp_max = 16;
            u->atk = 6; u->def = 1; u->move_range = 3; u->atk_range = 3;
            break;
        case CLASS_MAGE:
            u->name = "Mage"; u->hp = u->hp_max = 12;
            u->atk = 9; u->def = 0; u->move_range = 2; u->atk_range = 2;
            break;
        case CLASS_GOBLIN:
            u->name = "Goblin"; u->hp = u->hp_max = 14;
            u->atk = 5; u->def = 1; u->move_range = 3; u->atk_range = 1;
            break;
        case CLASS_SKELETON:
            u->name = "Skeleton"; u->hp = u->hp_max = 18;
            u->atk = 6; u->def = 2; u->move_range = 2; u->atk_range = 1;
            break;
    }

    const SpriteDefinition *def = (team == TEAM_PLAYER) ? &hero_spr : &enemy_spr;
    u16                     pal = (team == TEAM_PLAYER) ? PAL1 : PAL2;
    u->spr = SPR_addSprite(def, grid_px_x(gx), grid_px_y(gy), TILE_ATTR(pal, 1, FALSE, FALSE));

    /* DIRECT mode: both of these get called as (entity, entity->data). */
    e->update  = (darken_state)unit_update;
    e->destroy = (darken_state)unit_destroy;

    return e;
}

/* ----------------------------------------------------------------------
 * Combat
 * -------------------------------------------------------------------- */

static void resolve_attack(darken_entity attacker_e, darken_entity target_e)
{
    struct unit *a = unit_of(attacker_e);
    struct unit *t = unit_of(target_e);

    s16 dmg = a->atk - t->def;
    if (dmg < 1) dmg = 1;
    t->hp -= dmg;

    if (t->hp <= 0)
        kill_unit(target_e);

    check_victory(); /* may not return, if that was the last unit standing */
}

/* ----------------------------------------------------------------------
 * Enemy AI — one simple pass per enemy unit, per enemy phase
 * -------------------------------------------------------------------- */

static void ai_take_turn(darken_entity self_e, struct unit *self)
{
    darken_entity target_e = nearest_enemy_of(TEAM_ENEMY, self->gx, self->gy);
    if (!target_e) return; /* no heroes left; shouldn't normally happen mid-phase */

    struct unit *target = unit_of(target_e);
    s16          steps   = self->move_range;

    /* Greedy walk towards the target, one tile at a time, one axis at a
       time. No pathfinding and no obstacle avoidance beyond "don't step
       onto an occupied tile" — kept deliberately simple for this example. */
    while (steps > 0 && manhattan(self->gx, self->gy, target->gx, target->gy) > 0)
    {
        s16 dx = (s16)target->gx - (s16)self->gx;
        s16 dy = (s16)target->gy - (s16)self->gy;
        u8  nx = self->gx, ny = self->gy;

        if (abs16(dx) >= abs16(dy) && dx != 0)
            nx = (u8)(self->gx + ((dx > 0) ? 1 : -1));
        else if (dy != 0)
            ny = (u8)(self->gy + ((dy > 0) ? 1 : -1));
        else
            break;

        if (unit_at(nx, ny)) break; /* something's in the way, stop here */

        self->gx = nx;
        self->gy = ny;
        steps--;

        if (manhattan(self->gx, self->gy, target->gx, target->gy) <= (u16)self->atk_range)
            break; /* close enough, no need to keep closing the distance */
    }

    if (manhattan(self->gx, self->gy, target->gx, target->gy) <= (u16)self->atk_range)
        resolve_attack(self_e, target_e);
}

static void run_enemy_phase(void)
{
    /* DARKEN_FOREACH visits every ACTIVE unit in reverse order, which is
       exactly what makes it safe to pause (or delete) the current entity
       mid-loop — see darken.h's own note on DARKEN_FOREACH. We filter to
       TEAM_ENEMY ourselves; darken.h has no notion of teams. */
    DARKEN_FOREACH(&units, {
        struct unit *u = unit_of(_entity);
        if (u->team == TEAM_ENEMY)
        {
            ai_take_turn(_entity, u);
            darken_entity_pause(_entity); /* "done acting this round" */
            wait_frames(20);              /* so a human can actually see it happen */
        }
    });
}

/* ----------------------------------------------------------------------
 * Phase management
 * ----------------------------------------------------------------------
 * We reuse darken's paused zone as "has acted this round": once a unit
 * moves (and optionally attacks) we pause it, which removes it from
 * DARKEN_FOREACH / darken_update()'s reach for the rest of the round
 * without deleting it. At the start of a team's phase we resume every one
 * of that team's units so they're eligible to act again.
 * -------------------------------------------------------------------- */

static void begin_phase(Phase p)
{
    resume_team((p == PHASE_PLAYER) ? TEAM_PLAYER : TEAM_ENEMY);
    phase = p;

    if (p == PHASE_PLAYER)
    {
        pstate        = PS_SELECT_UNIT;
        active_unit   = 0;
        attack_target = 0;
        show_message("Your turn - pick a unit");
        return;
    }

    show_message("Enemy turn...");
    run_enemy_phase();

    if (count_team_alive(TEAM_PLAYER) == 0)
    {
        show_message("DEFEAT... reset to try again");
        while (TRUE) { SPR_update(); SYS_doVBlankProcess(); }
    }

    begin_phase(PHASE_PLAYER); /* loop back around for the next round */
}

static void check_victory(void)
{
    if (count_team_alive(TEAM_ENEMY) == 0)
    {
        show_message("VICTORY! Reset to play again");
        while (TRUE) { SPR_update(); SYS_doVBlankProcess(); }
    }
    if (count_team_alive(TEAM_PLAYER) == 0)
    {
        show_message("DEFEAT... reset to try again");
        while (TRUE) { SPR_update(); SYS_doVBlankProcess(); }
    }
}

/* ----------------------------------------------------------------------
 * Player input
 * -------------------------------------------------------------------- */

static u16 read_new_presses(void)
{
    static u16 prev = 0;
    u16        now  = JOY_readJoypad(JOY_1);
    u16        newly_pressed = (u16)(now & ~prev);
    prev = now;
    return newly_pressed;
}

static void move_cursor(u16 pressed)
{
    if ((pressed & BUTTON_UP)    && cursor_y > 0)          cursor_y--;
    if ((pressed & BUTTON_DOWN)  && cursor_y < GRID_H - 1)  cursor_y++;
    if ((pressed & BUTTON_LEFT)  && cursor_x > 0)          cursor_x--;
    if ((pressed & BUTTON_RIGHT) && cursor_x < GRID_W - 1)  cursor_x++;
}

static void handle_player_input(u16 pressed)
{
    if (pressed & BUTTON_START)
    {
        begin_phase(PHASE_ENEMY); /* end the phase early, acted or not */
        return;
    }

    switch (pstate)
    {
        case PS_SELECT_UNIT:
        {
            move_cursor(pressed);

            if (pressed & BUTTON_A)
            {
                darken_entity e = unit_at(cursor_x, cursor_y);
                /* DARKEN_ENTITY_IN_ACTIVE(e) is exactly "hasn't acted yet
                   this round": only the current phase's team ever has
                   members in the active zone (see the big comment above). */
                if (e && unit_of(e)->team == TEAM_PLAYER && DARKEN_ENTITY_IN_ACTIVE(e))
                {
                    active_unit    = e;
                    active_start_x = cursor_x;
                    active_start_y = cursor_y;
                    pstate         = PS_SELECT_TILE;
                    show_message("Move, A to confirm, B to cancel");
                }
            }
            break;
        }

        case PS_SELECT_TILE:
        {
            move_cursor(pressed);

            if (pressed & BUTTON_B)
            {
                cursor_x    = active_start_x;
                cursor_y    = active_start_y;
                active_unit = 0;
                pstate      = PS_SELECT_UNIT;
                show_message("Your turn - pick a unit");
            }
            else if (pressed & BUTTON_A)
            {
                struct unit  *u        = unit_of(active_unit);
                u16           dist     = manhattan(active_start_x, active_start_y, cursor_x, cursor_y);
                darken_entity occupant = unit_at(cursor_x, cursor_y);

                if (dist > (u16)u->move_range || (occupant && occupant != active_unit))
                {
                    show_message("Can't move there");
                    break;
                }

                u->gx = cursor_x;
                u->gy = cursor_y;

                attack_target = nearest_enemy_of(TEAM_PLAYER, u->gx, u->gy);
                if (attack_target &&
                    manhattan(u->gx, u->gy, unit_of(attack_target)->gx, unit_of(attack_target)->gy) <= (u16)u->atk_range)
                {
                    pstate = PS_CONFIRM_ATTACK;
                    show_message("Attack? A = yes, B = no");
                }
                else
                {
                    darken_entity_pause(active_unit); /* done for this round */
                    active_unit = 0;
                    pstate      = PS_SELECT_UNIT;
                    show_message("Your turn - pick a unit");
                }
            }
            break;
        }

        case PS_CONFIRM_ATTACK:
        {
            if (pressed & BUTTON_A)
            {
                resolve_attack(active_unit, attack_target); /* may freeze on victory/defeat */
                show_message("Hit!");
            }
            if (pressed & (BUTTON_A | BUTTON_B))
            {
                darken_entity_pause(active_unit);
                active_unit   = 0;
                attack_target = 0;
                pstate        = PS_SELECT_UNIT;
            }
            break;
        }
    }

    /* Everyone on this side has acted (the active zone can only ever hold
       the current phase's team, see the big comment above begin_phase) —
       move on automatically instead of waiting for START. */
    if (pstate == PS_SELECT_UNIT && DARKEN_COUNT_ACTIVE(&units) == 0)
        begin_phase(PHASE_ENEMY);
}

/* ----------------------------------------------------------------------
 * HUD
 * -------------------------------------------------------------------- */

static void update_hud(void)
{
    darken_entity e = (pstate == PS_SELECT_UNIT) ? unit_at(cursor_x, cursor_y) : active_unit;

    if (e)
    {
        struct unit *u = unit_of(e);
        char         buf[24];
        sprintf(buf, "%s  HP %d/%d", u->name, u->hp, u->hp_max);
        show_status(buf);
    }
    else
    {
        show_status("");
    }
}

/* ----------------------------------------------------------------------
 * Entry point
 * -------------------------------------------------------------------- */

int main(bool hardReset)
{
    (void)hardReset;

    JOY_init();
    SPR_init();

    /* Static battlefield background on BG_B, drawn once. */
    PAL_setPalette(PAL0, battlefield.palette->data, 1);
    VDP_drawImageEx(BG_B, &battlefield, TILE_ATTR_FULL(PAL0, 0, 0, 0, 1), 0, 0, 0, CPU);

    /* One palette per sprite sheet. */
    PAL_setPalette(PAL1, hero_spr.palette->data, DMA);
    PAL_setPalette(PAL2, enemy_spr.palette->data, DMA);
    PAL_setPalette(PAL3, cursor_spr.palette->data, DMA);

    darken_init(&units);

    /* The party. */
    spawn_unit(TEAM_PLAYER, CLASS_WARRIOR, 1, 3);
    spawn_unit(TEAM_PLAYER, CLASS_ARCHER, 1, 2);
    spawn_unit(TEAM_PLAYER, CLASS_MAGE, 1, 4);

    /* The opposition. */
    spawn_unit(TEAM_ENEMY, CLASS_GOBLIN, 8, 2);
    spawn_unit(TEAM_ENEMY, CLASS_GOBLIN, 8, 4);
    spawn_unit(TEAM_ENEMY, CLASS_SKELETON, 8, 3);

    cursor_sprite = SPR_addSprite(&cursor_spr, grid_px_x(cursor_x), grid_px_y(cursor_y),
                                   TILE_ATTR(PAL3, 2, FALSE, FALSE));

    begin_phase(PHASE_PLAYER);

    while (TRUE)
    {
        u16 pressed = read_new_presses();

        if (phase == PHASE_PLAYER)
            handle_player_input(pressed);

        SPR_setPosition(cursor_sprite, grid_px_x(cursor_x), grid_px_y(cursor_y));
        update_hud();

        darken_update(&units); /* syncs every active unit's sprite position */
        SPR_update();
        SYS_doVBlankProcess();
    }

    return 0;
}
