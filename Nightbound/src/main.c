#include <genesis.h>
#include "game/game.h"

int main(void)
{
    VDP_setScreenWidth320();
    VDP_setScreenHeight224();
    VDP_setPlaneSize(64, 32, TRUE);

    JOY_init();
    game_init();

    while (TRUE)
    {
        JOY_update();
        game_update();
        SYS_doVBlankProcess();
    }

    return 0;
}
