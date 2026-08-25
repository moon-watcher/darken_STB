/**
 * battle.c — turn-based combat.
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

#include <genesis.h>
#include "game.h"

void battle_start(darken_entity enemy)
{
    g_state = GS_BATTLE;
    g_battle_enemy = enemy;
    EnemyData *e = &((EntityData *)enemy->data)->enemy;
    snprintf(g_msg, sizeof(g_msg), "A wild %s appears!", enemy_name(e->type));
}

void render_battle(void)
{
    PlayerData *p = (PlayerData *)g_player->data;
    EnemyData *e = &((EntityData *)g_battle_enemy->data)->enemy;

    printf("\n  === BATTLE ===\n");
    printf("  %s   HP %d/%d\n", enemy_name(e->type), e->hp, e->hp_max);
    printf("  You         HP %d/%d  MP %d/%d\n", p->hp, p->hp_max, p->mp, p->mp_max);
    if (g_msg[0])
        printf("  > %s\n", g_msg);
    g_msg[0] = '\0';
    printf("  1) Attack  2) Fireball (3 MP)  3) Use potion  4) Flee\n");
}

static int roll_damage(int atk, int def)
{
    int dmg = atk - def + (rand() % 5 - 2);
    return dmg < 1 ? 1 : dmg;
}

static void enemy_attacks(void)
{
    PlayerData *p = (PlayerData *)g_player->data;
    EnemyData *e = &((EntityData *)g_battle_enemy->data)->enemy;
    int dmg = roll_damage(e->atk, p->def);
    p->hp -= dmg;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s hits you for %d damage.", enemy_name(e->type), dmg);
    strncat(g_msg, "  ", sizeof(g_msg) - strlen(g_msg) - 1);
    strncat(g_msg, buf, sizeof(g_msg) - strlen(g_msg) - 1);

    if (p->hp <= 0)
    {
        p->hp = 0;
        g_state = GS_GAMEOVER;
    }
}

static void resolve_victory(void)
{
    EnemyData *e = &((EntityData *)g_battle_enemy->data)->enemy;
    int exp = e->exp_reward, gold = e->gold_reward;
    int was_dragon = (e->type == ENEMY_DRAGON);
    PlayerData *p = (PlayerData *)g_player->data;

    p->gold += gold;
    snprintf(g_msg, sizeof(g_msg), "You defeated the %s! +%d exp, +%d gold.",
             enemy_name(e->type), exp, gold);

    /* Still in the active zone (see file header comment): the destructor
     * runs, which is what gives it a chance to drop bonus loot. */
    darken_entity_delete(g_battle_enemy);
    g_battle_enemy = NULL;

    player_gain_exp(exp);

    g_state = was_dragon ? GS_WIN : GS_EXPLORE;
}

void battle_turn(int choice)
{
    PlayerData *p = (PlayerData *)g_player->data;
    EnemyData *e = &((EntityData *)g_battle_enemy->data)->enemy;
    g_msg[0] = '\0';

    switch (choice)
    {
    case 1: /* Attack */
    {
        int dmg = roll_damage(p->atk, e->def);
        e->hp -= dmg;
        snprintf(g_msg, sizeof(g_msg), "You hit the %s for %d damage.", enemy_name(e->type), dmg);
        break;
    }
    case 2: /* Fireball */
        if (p->mp < 3)
        {
            snprintf(g_msg, sizeof(g_msg), "Not enough MP.");
            return; /* free action, doesn't cost a turn */
        }
        p->mp -= 3;
        {
            int dmg = roll_damage(p->atk + 6, e->def);
            e->hp -= dmg;
            snprintf(g_msg, sizeof(g_msg), "Fireball scorches the %s for %d damage.", enemy_name(e->type), dmg);
        }
        break;
    case 3: /* Potion */
        if (p->potions <= 0)
        {
            snprintf(g_msg, sizeof(g_msg), "You have no potions.");
            return; /* free action */
        }
        p->potions--;
        p->hp += 15;
        if (p->hp > p->hp_max)
            p->hp = p->hp_max;
        snprintf(g_msg, sizeof(g_msg), "You drink a potion. HP restored.");
        break;
    case 4: /* Flee */
        if (rand() % 100 < 70)
        {
            entity_set_pos(g_battle_enemy, e->spawn_x, e->spawn_y);
            g_battle_enemy->state = (darken_state)enemy_state_patrol;
            g_battle_enemy = NULL;
            g_state = GS_EXPLORE;
            snprintf(g_msg, sizeof(g_msg), "You got away safely.");
            return;
        }
        snprintf(g_msg, sizeof(g_msg), "You couldn't escape!");
        break;
    default:
        snprintf(g_msg, sizeof(g_msg), "Choose 1-4.");
        return; /* invalid input, no turn spent */
    }

    if (e->hp <= 0)
    {
        resolve_victory();
        return;
    }
    enemy_attacks();
}
