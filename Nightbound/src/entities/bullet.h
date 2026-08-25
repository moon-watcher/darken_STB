#ifndef BULLET_H
#define BULLET_H

#include "entity.h"

darken_entity bullet_spawn(fix16 x, fix16 y, int16_t direction);
void bullet_post_update(darken_entity entity, GameEntity *bullet);
void *bullet_state(void *data);

#endif
