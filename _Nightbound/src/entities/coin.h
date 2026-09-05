#ifndef COIN_H
#define COIN_H

#include "entity.h"

darken_entity coin_spawn(fix16 x, fix16 y);
void coin_post_update(darken_entity entity, GameEntity *coin);
void *coin_state(void *data);

#endif
