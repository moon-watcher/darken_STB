/*
 * capture.c — Capture The Flag with Darken 2.0
 *
 * Compile:  gcc -std=gnu99 capture.c -o ctf -lm
 * Run:      ./ctf
 *
 * Controls: P1=WASD  P2=IJKL  Quit=Q
 */

#define DARKEN_IMPLEMENTATION
#include "darken.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <string.h>
#include <time.h>

/* ============================================================
 * CONFIG
 * ============================================================ */

#define MAP_W       42
#define MAP_H       22
#define FLAG_X      37.0f
#define FLAG_Y      11.0f
#define MAX_ENT     64

/* ============================================================
 * NON-BLOCKING INPUT (Linux)
 * ============================================================ */

static int kbhit(void)
{
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

static int getch_noblock(void)
{
    struct termios old, new;
    int ch;
    tcgetattr(STDIN_FILENO, &old);
    new = old;
    new.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    return ch;
}

/* ============================================================
 * GAME DATA
 * ============================================================ */

typedef struct {
    float x, y;
    float vx, vy;
    int   hp;
    int   type;          /* 0=player, 1=chaser, 2=patroller, 3=blocker */
    int   id;            /* 1 or 2 for players */
    int   dir;           /* patroller pattern */
    int   timer;         /* general purpose */
    int   invulnerable;  /* invincibility frames */
    char  symbol;
} EntityData;

/* Game globals */
static float g_p1_x = 3.0f, g_p1_y = 3.0f;
static float g_p2_x = 3.0f, g_p2_y = 18.0f;
static int   g_hp[2] = {3, 3};
static float g_spawn[2][2] = {{3.0f, 3.0f}, {3.0f, 18.0f}};
static int   g_game_over = 0;
static int   g_winner = 0;

/* ============================================================
 * ENEMY AI
 * ============================================================ */

static void ai_chaser(EntityData *e)
{
    /* Chase the closest player */
    float tx, ty;
    float d1 = (g_p1_x - e->x) * (g_p1_x - e->x) + (g_p1_y - e->y) * (g_p1_y - e->y);
    float d2 = (g_p2_x - e->x) * (g_p2_x - e->x) + (g_p2_y - e->y) * (g_p2_y - e->y);

    if (d1 < d2) { tx = g_p1_x; ty = g_p1_y; }
    else         { tx = g_p2_x; ty = g_p2_y; }

    float dx = tx - e->x;
    float dy = ty - e->y;
    float len = sqrtf(dx * dx + dy * dy);

    if (len > 0.5f) {
        e->vx = (dx / len) * 0.18f;
        e->vy = (dy / len) * 0.18f;
    }
}

static void ai_patroller(EntityData *e)
{
    /* Change direction every 50 frames (~1.5s) */
    e->timer++;
    if (e->timer > 50) {
        e->timer = 0;
        e->dir = rand() % 4;   /* 0=up 1=right 2=down 3=left */
    }
    switch (e->dir) {
        case 0: e->vx = 0.0f;   e->vy = -0.15f; break;
        case 1: e->vx = 0.15f;  e->vy = 0.0f;   break;
        case 2: e->vx = 0.0f;   e->vy = 0.15f;  break;
        case 3: e->vx = -0.15f; e->vy = 0.0f;   break;
    }
}

static void ai_blocker(EntityData *e)
{
    /* Stand between the closest player and the flag */
    float tx, ty;
    float d1 = (g_p1_x - e->x) * (g_p1_x - e->x) + (g_p1_y - e->y) * (g_p1_y - e->y);
    float d2 = (g_p2_x - e->x) * (g_p2_x - e->x) + (g_p2_y - e->y) * (g_p2_y - e->y);

    if (d1 < d2) { tx = g_p1_x; ty = g_p1_y; }
    else         { tx = g_p2_x; ty = g_p2_y; }

    float target_x = (tx + FLAG_X) * 0.5f;
    float target_y = (ty + FLAG_Y) * 0.5f;

    float dx = target_x - e->x;
    float dy = target_y - e->y;
    float len = sqrtf(dx * dx + dy * dy);

    if (len > 0.5f) {
        e->vx = (dx / len) * 0.10f;
        e->vy = (dy / len) * 0.10f;
    }
}

/* ============================================================
 * ENTITY STATE (callback run every frame by de_manager_update)
 * ============================================================ */

static void *game_state(EntityData *e)
{
    /* Movement */
    e->x += e->vx;
    e->y += e->vy;

    /* Friction for players (they slide a bit) */
    if (e->type == 0) {
        e->vx *= 0.85f;
        e->vy *= 0.85f;

        if (e->id == 1) { g_p1_x = e->x; g_p1_y = e->y; }
        else            { g_p2_x = e->x; g_p2_y = e->y; }
    }

    /* Map borders */
    if (e->x < 1.0f) e->x = 1.0f;
    if (e->x > MAP_W - 2) e->x = MAP_W - 2;
    if (e->y < 1.0f) e->y = 1.0f;
    if (e->y > MAP_H - 2) e->y = MAP_H - 2;

    /* Call the corresponding brain */
    if (e->type == 1) ai_chaser(e);
    else if (e->type == 2) ai_patroller(e);
    else if (e->type == 3) ai_blocker(e);

    if (e->invulnerable > 0) e->invulnerable--;

    return DE_STATE_LOOP;
}

/* ============================================================
 * ENTITY FACTORY
 * ============================================================ */

static de_entity create_entity(de_manager *m, de_system *ren,
                                int type, int id, float x, float y, char sym)
{
    de_entity e = de_manager_new(m);
    if (!e) {
        fprintf(stderr, "No free slots!\n");
        return NULL;
    }

    EntityData *d = (EntityData *)e->data;
    memset(d, 0, sizeof(EntityData));
    d->x = x;
    d->y = y;
    d->hp = (type == 0) ? 3 : 1;
    d->type = type;
    d->id = id;
    d->symbol = sym;

    e->state = (de_state)game_state;
    e->tag = (uint16_t)type;

    /* Register in render system: (x*, y*, sym*) */
    de_system_add(ren, &d->x, &d->y, &d->symbol);

    return e;
}

/* ============================================================
 * COLLISIONS
 * ============================================================ */

static void check_collisions(de_manager *m)
{
    /* Outer loop: players */
    DE_MANAGER_FOREACH(m, {
        EntityData *p = (EntityData *)ENTITY->data;
        if (p->type != 0) continue;   /* only players */

        /* --- Reached the flag? --- */
        float fdx = p->x - FLAG_X;
        float fdy = p->y - FLAG_Y;
        if (fdx * fdx + fdy * fdy < 2.5f) {
            g_game_over = 1;
            g_winner = p->id;
            return;
        }

        if (p->invulnerable > 0) continue;

        /* --- Hit by an enemy? --- */
        /* NOTE: nested DE_MANAGER_FOREACH is valid because each
         * macro creates its own ENTITY in a separate do-while block. */
        DE_MANAGER_FOREACH(m, {
            EntityData *enemy = (EntityData *)ENTITY->data;
            if (enemy->type == 0) continue;

            float edx = p->x - enemy->x;
            float edy = p->y - enemy->y;
            if (edx * edx + edy * edy < 1.8f) {
                p->hp--;
                p->invulnerable = 45;   /* ~0.75s invincibility */
                int idx = p->id - 1;
                g_hp[idx] = p->hp;

                /* Respawn */
                p->x = g_spawn[idx][0];
                p->y = g_spawn[idx][1];
                p->vx = 0.0f;
                p->vy = 0.0f;

                if (p->id == 1) { g_p1_x = p->x; g_p1_y = p->y; }
                else            { g_p2_x = p->x; g_p2_y = p->y; }

                if (p->hp <= 0) {
                    g_game_over = 1;
                    g_winner = (p->id == 1) ? 2 : 1;
                }
                return;  /* one hit per frame */
            }
        });
    });
}

/* ============================================================
 * RENDERING VIA DE_SYSTEM (flat array, cache-friendly)
 * ============================================================ */

static void draw_borders(void)
{
    int x, y;
    printf("\033[2J\033[H");   /* clear screen */

    for (x = 0; x < MAP_W; x++) printf("#");
    printf("\n");

    for (y = 1; y < MAP_H - 1; y++) {
        printf("#");
        for (x = 1; x < MAP_W - 1; x++) printf(" ");
        printf("#\n");
    }

    for (x = 0; x < MAP_W; x++) printf("#");
    printf("\n");
}

static void render_frame(de_system *ren)
{
    draw_borders();

    /* Flag */
    printf("\033[%d;%dH$", (int)FLAG_Y + 1, (int)FLAG_X + 1);

    /* Iterate the flat de_system: 3 pointers per group (x, y, symbol).
     * Using the public DE_SYSTEM_FOREACH macro. */
    DE_SYSTEM_FOREACH(ren, float *px, float *py, char *sym, {
        int ix = (int)(*px);
        int iy = (int)(*py);
        if (ix >= 1 && ix < MAP_W - 1 && iy >= 1 && iy < MAP_H - 1)
            printf("\033[%d;%dH%c", iy + 1, ix + 1, *sym);
    });

    /* HUD */
    printf("\033[%d;1H", MAP_H + 1);
    printf("=== CAPTURE THE FLAG ===\n");
    printf("P1 '%c': %d HP  |  P2 '%c': %d HP\n", '1', g_hp[0], '2', g_hp[1]);
    printf("Flag at (%.0f,%.0f)  |  Q to quit\n", FLAG_X, FLAG_Y);
}

/* ============================================================
 * MAIN
 * ============================================================ */

int main(void)
{
    srand((unsigned)time(NULL));

    /* --- Setup Darken --- */
    DE_MANAGER_STORAGE(world, MAX_ENT, sizeof(EntityData));
    DE_SYSTEM_STORAGE(render_sys, MAX_ENT, 3);

    struct de_manager manager;
    struct de_system  renderer;

    de_manager_init(&manager, DE_MANAGER_ARGS(world));
    de_system_init(&renderer, DE_SYSTEM_ARGS(render_sys));

    /* --- Create players --- */
    EntityData *p1_data = NULL;
    EntityData *p2_data = NULL;

    de_entity p1 = create_entity(&manager, &renderer, 0, 1, g_spawn[0][0], g_spawn[0][1], '1');
    de_entity p2 = create_entity(&manager, &renderer, 0, 2, g_spawn[1][0], g_spawn[1][1], '2');

    if (p1) p1_data = (EntityData *)p1->data;
    if (p2) p2_data = (EntityData *)p2->data;

    /* --- Create enemies --- */
    /* 2 Chasers */
    create_entity(&manager, &renderer, 1, 0, 20.0f, 5.0f,  'C');
    create_entity(&manager, &renderer, 1, 0, 25.0f, 15.0f, 'C');

    /* 2 Patrollers */
    create_entity(&manager, &renderer, 2, 0, 15.0f, 10.0f, 'P');
    create_entity(&manager, &renderer, 2, 0, 30.0f, 8.0f,  'P');

    /* 2 Blockers */
    create_entity(&manager, &renderer, 3, 0, 22.0f, 11.0f, 'B');
    create_entity(&manager, &renderer, 3, 0, 28.0f, 11.0f, 'B');

    /* --- Game loop --- */
    printf("Starting in 1 second... Grab a friend!\n");
    usleep(1000000);

    while (!g_game_over) {
        /* Input */
        if (kbhit()) {
            int c = getch_noblock();
            if (p1_data) {
                if (c == 'w' || c == 'W') p1_data->vy = -0.7f;
                if (c == 's' || c == 'S') p1_data->vy = 0.7f;
                if (c == 'a' || c == 'A') p1_data->vx = -0.7f;
                if (c == 'd' || c == 'D') p1_data->vx = 0.7f;
            }
            if (p2_data) {
                if (c == 'i' || c == 'I') p2_data->vy = -0.7f;
                if (c == 'k' || c == 'K') p2_data->vy = 0.7f;
                if (c == 'j' || c == 'J') p2_data->vx = -0.7f;
                if (c == 'l' || c == 'L') p2_data->vx = 0.7f;
            }
            if (c == 'q' || c == 'Q') g_game_over = 1;
        }

        /* One tick of the engine: runs ALL states */
        de_manager_update(&manager);

        /* Collisions */
        check_collisions(&manager);

        /* Render via de_system */
        render_frame(&renderer);

        usleep(33333);  /* ~30 FPS */
    }

    /* --- Game Over --- */
    printf("\033[%d;1H", MAP_H + 4);
    if (g_winner == 1 || g_winner == 2)
        printf(">>> PLAYER %d CAPTURED THE FLAG! <<<\n", g_winner);
    else
        printf(">>> GAME OVER <<<\n");

    de_manager_reset(&manager);
    return 0;
}
