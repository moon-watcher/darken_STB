#ifndef INPUT_H
#define INPUT_H

#include <genesis.h>

void input_init(void);
void input_update(void);
uint16_t input_left(void);
uint16_t input_right(void);
uint16_t input_jump_pressed(void);
uint16_t input_jump_held(void);
uint16_t input_pause_pressed(void);

#endif
