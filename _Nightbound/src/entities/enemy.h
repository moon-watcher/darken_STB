#ifndef ENEMY_H
#define ENEMY_H

#include "entity.h"

darken_entity enemy_spawn(fix16 x, fix16 y, fix16 left, fix16 right);
void enemy_post_update(darken_entity entity, GameEntity *enemy);
void *enemy_state_patrol(void *data);

#endif
