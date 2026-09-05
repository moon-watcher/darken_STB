#ifndef LEVEL_H
#define LEVEL_H

#include <genesis.h>

#define LEVEL_WIDTH 200
#define LEVEL_HEIGHT 28

void level_init(void);
void level_reset(void);
uint16_t level_solid(uint16_t tx, uint16_t ty);
void level_draw(fix16 camera_x);

#endif
