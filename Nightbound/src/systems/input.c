#include <genesis.h>
#include "input.h"

static uint16_t current;
static uint16_t previous;

void input_init(void)
{
    current = 0;
    previous = 0;
}

void input_update(void)
{
    previous = current;
    current = JOY_readJoypad(JOY_1);
}

uint16_t input_left(void) { return current & BUTTON_LEFT; }
uint16_t input_right(void) { return current & BUTTON_RIGHT; }
uint16_t input_jump_pressed(void)
{
    return (current & (BUTTON_A | BUTTON_B | BUTTON_C)) &&
           !(previous & (BUTTON_A | BUTTON_B | BUTTON_C));
}
uint16_t input_jump_held(void)
{
    return current & (BUTTON_A | BUTTON_B | BUTTON_C);
}
uint16_t input_pause_pressed(void)
{
    return (current & BUTTON_START) && !(previous & BUTTON_START);
}
