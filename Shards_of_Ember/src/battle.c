/**
 * battle.c — turn-based combat, mapped straight onto the pad:
 * A = Attack, B = Fireball, C = Potion, START = Flee.
 *
 * battle_start() is called either directly (player walks into an enemy) or
 * from inside enemy_state_chase() (enemy walks into the player). Either
 * way the enemy is still sitting in the ACTIVE zone when the fight
 * resolves — if it came from enemy_state_chase, the DARKEN_PAUSE it
 * returned only takes effect on darken's *next* tick, and we never call
 * darken_update() again while g_state == GS_BATTLE. That means a normal
 * darken_entity_delete() on defeat always hits the "still active" branch,
 * so the enemy's destructor reliably fires. On a successful flee we just
 * hand the enemy back its patrol state directly (a plain field write —
 * `state` is public) and reset it to its leash origin.
 */

#include "game.h"
#include "darken.h"


void battle_start(darken_entity enemy)
{
    EnemyData *e = &((EntityData *)enemy->data)->enemy;

    g_state = GS_BATTLE;
    g_battle_enemy = enemy;
    sprintf(g_msg, "A wild %s appears!", enemy_name(e->type));
    strclr(g_msg2);
    render_battle();
}

void render_battle(void)
{
    PlayerData *p = (PlayerData *)g_player->data;
    EnemyData *e = &((EntityData *)g_battle_enemy->data)->enemy;

    VDP_clearTextArea(0, 0, 40, 28);
    VDP_drawText("=== BATTLE ===", 2, 2);
    ui_printf(2, 4, "%s   HP %d/%d", enemy_name(e->type), e->hp, e->hp_max);
    ui_printf(2, 6, "You   HP %d/%d  MP %d/%d", p->hp, p->hp_max, p->mp, p->mp_max);

    if (g_msg[0])
        VDP_drawText(g_msg, 2, 9);
    if (g_msg2[0])
        VDP_drawText(g_msg2, 2, 10);

    VDP_drawText("A ATTACK        B FIREBALL(3MP)", 2, 13);
    VDP_drawText("C POTION        START FLEE", 2, 14);
}

static s16 roll_damage(s16 atk, s16 def)
{
    s16 dmg = atk - def + ((s16)(random() % 5) - 2);
    return dmg < 1 ? 1 : dmg;
}

static void enemy_attacks(void)
{
    PlayerData *p = (PlayerData *)g_player->data;
    EnemyData *e = &((EntityData *)g_battle_enemy->data)->enemy;
    s16 dmg = roll_damage(e->atk, p->def);

    p->hp -= dmg;
    sprintf(g_msg2, "%s hits you for %d damage.", enemy_name(e->type), dmg);

    if (p->hp <= 0)
    {
        p->hp = 0;
        g_state = GS_GAMEOVER;
    }
}

static void resolve_victory(void)
{
    EnemyData *e = &((EntityData *)g_battle_enemy->data)->enemy;
    s16 exp = e->exp_reward, gold = e->gold_reward;
    u16 was_dragon = (e->type == ENEMY_DRAGON);
    PlayerData *p = (PlayerData *)g_player->data;

    p->gold += gold;
    sprintf(g_msg, "Defeated the %s! +%d exp +%d gold.", enemy_name(e->type), exp, gold);
    strclr(g_msg2);

    /* Still in the active zone (see file header comment): the destructor
     * runs, which is what gives it a chance to drop bonus loot. */
    darken_entity_delete(g_battle_enemy);
    g_battle_enemy = NULL;

    player_gain_exp(exp); /* may overwrite g_msg2 with a level-up line */

    g_state = was_dragon ? GS_WIN : GS_EXPLORE;
}

void battle_input(u16 pressed)
{
    PlayerData *p = (PlayerData *)g_player->data;
    EnemyData *e = &((EntityData *)g_battle_enemy->data)->enemy;

    strclr(g_msg);
    strclr(g_msg2);

    if (pressed & BUTTON_A)
    {
        s16 dmg = roll_damage(p->atk, e->def);
        e->hp -= dmg;
        sprintf(g_msg, "You hit the %s for %d damage.", enemy_name(e->type), dmg);
    }
    else if (pressed & BUTTON_B)
    {
        if (p->mp < 3)
        {
            sprintf(g_msg, "Not enough MP.");
            render_battle();
            return; /* free action, doesn't cost a turn */
        }
        p->mp -= 3;
        {
            s16 dmg = roll_damage(p->atk + 6, e->def);
            e->hp -= dmg;
            sprintf(g_msg, "Fireball scorches the %s for %d.", enemy_name(e->type), dmg);
        }
    }
    else if (pressed & BUTTON_C)
    {
        if (p->potions <= 0)
        {
            sprintf(g_msg, "You have no potions.");
            render_battle();
            return; /* free action */
        }
        p->potions--;
        p->hp += 15;
        if (p->hp > p->hp_max)
            p->hp = p->hp_max;
        sprintf(g_msg, "You drink a potion. HP restored.");
    }
    else if (pressed & BUTTON_START)
    {
        if ((random() % 100) < 70)
        {
            entity_set_pos(g_battle_enemy, e->spawn_x, e->spawn_y);
            g_battle_enemy->state = (darken_state)enemy_state_patrol;
            g_battle_enemy = NULL;
            g_state = GS_EXPLORE;
            sprintf(g_msg, "You got away safely.");
            render_explore();
            return;
        }
        sprintf(g_msg, "You couldn't escape!");
    }
    else
    {
        return; /* not one of our buttons, ignore */
    }

    if (e->hp <= 0)
    {
        resolve_victory();
        if (g_state == GS_EXPLORE)
            render_explore();
        return;
    }

    enemy_attacks();
    if (g_state == GS_BATTLE)
        render_battle();
}
