#ifndef GAME_ENTITY_H
#define GAME_ENTITY_H

#include <genesis.h>
#include "darken.h"

typedef enum
{
    ENTITY_NONE = 0,
    ENTITY_PLAYER,
    ENTITY_ENEMY,
    ENTITY_COIN,
    ENTITY_BULLET
} EntityType;

typedef struct
{
    EntityType type;
    fix16 x;
    fix16 y;
    fix16 vx;
    fix16 vy;
    fix16 width;
    fix16 height;
    int16_t direction;
    uint16_t flags;
    uint16_t timer;
    uint16_t color;
} GameEntity;

#define ENTITY_FLAG_GROUNDED 0x0001
#define ENTITY_FLAG_DEAD     0x0002
#define ENTITY_FLAG_HIDDEN   0x0004

extern darken g_entity_manager;

void entity_render(GameEntity *entity);
void entity_systems_update(void);
void entity_destroy_sprite(GameEntity *entity);

#endif
