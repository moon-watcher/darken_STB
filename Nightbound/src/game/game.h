#ifndef GAME_H
#define GAME_H

#include <genesis.h>
#include "darken.h"

extern uint32_t game_frame;
extern uint16_t game_score;
extern uint16_t game_paused;
extern darken g_entity_manager;
extern darken_entity g_player_entity;

void game_init(void);
void game_update(void);
void game_restart(void);

#endif
