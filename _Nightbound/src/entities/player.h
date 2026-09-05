#ifndef PLAYER_H
#define PLAYER_H

#include "entity.h"

darken_entity player_spawn(fix16 x, fix16 y);
void player_post_update(darken_entity entity, GameEntity *player);
void *player_state_idle(void *data);
void *player_state_run(void *data);
void *player_state_jump(void *data);
void *player_state_fall(void *data);
void *player_state_dead(void *data);

#endif
