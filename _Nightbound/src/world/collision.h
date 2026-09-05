#ifndef COLLISION_H
#define COLLISION_H

#include <genesis.h>
#include "../entities/entity.h"

uint16_t collision_aabb(const GameEntity *a, const GameEntity *b);
uint16_t collision_point_solid(fix16 x, fix16 y);
void collision_move_x(GameEntity *e);
void collision_move_y(GameEntity *e);

#endif
