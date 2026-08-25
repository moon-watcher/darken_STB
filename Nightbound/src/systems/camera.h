#ifndef CAMERA_H
#define CAMERA_H

#include <genesis.h>

void camera_init(void);
void camera_reset(void);
void camera_update(void);
int16_t camera_world_to_screen_x(fix16 world_x);
fix16 camera_x(void);

#endif
