#include <genesis.h>
#include "level.h"

/* A compact hand-authored collision map. 1 = solid. */
static uint8_t solid[LEVEL_WIDTH][LEVEL_HEIGHT];

static const uint16_t floor_segments[][2] =
{
    {0, 199},
};

static void add_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t yy;
    for (yy = y; yy < y + h && yy < LEVEL_HEIGHT; ++yy)
    {
        uint16_t xx;
        for (xx = x; xx < x + w && xx < LEVEL_WIDTH; ++xx)
            solid[xx][yy] = 1;
    }
}

void level_init(void)
{
    uint16_t x;
    uint16_t y;

    for (x = 0; x < LEVEL_WIDTH; ++x)
        for (y = 0; y < LEVEL_HEIGHT; ++y)
            solid[x][y] = 0;

    for (x = floor_segments[0][0]; x <= floor_segments[0][1]; ++x)
        solid[x][26] = 1;

    add_rect(12, 22, 8, 4);
    add_rect(28, 19, 8, 7);
    add_rect(43, 23, 7, 3);
    add_rect(55, 20, 8, 6);
    add_rect(72, 16, 8, 10);
    add_rect(91, 21, 7, 5);
    add_rect(106, 18, 9, 8);
    add_rect(125, 22, 8, 4);
    add_rect(142, 18, 10, 8);
    add_rect(165, 21, 8, 5);
    add_rect(180, 17, 10, 9);

    level_draw(0);
}

void level_reset(void)
{
    level_init();
}

uint16_t level_solid(uint16_t tx, uint16_t ty)
{
    if (tx >= LEVEL_WIDTH || ty >= LEVEL_HEIGHT)
        return 1;

    return solid[tx][ty];
}

void level_draw(fix16 camera_x)
{
    /* Generate simple tile art: tile 0 empty, tile 1 solid, tile 2 coin/entity. */
    static const u32 tiles[3 * 8] =
    {
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x11111111, 0x11111111, 0x11111111, 0x11111111,
        0x00011000, 0x00111100, 0x01111110, 0x01111110,
        0x00111100, 0x00011000, 0x00000000, 0x00000000
    };

    VDP_loadTileData(tiles, 1, 3, CPU);
    VDP_clearPlane(BG_A, TRUE);

    uint16_t start_x = (uint16_t)(F16_toInt(camera_x) >> 3);
    uint16_t x;
    uint16_t y;

    for (y = 0; y < 28; ++y)
    {
        for (x = 0; x < 40; ++x)
        {
            uint16_t world_x = start_x + x;
            if (world_x < LEVEL_WIDTH && level_solid(world_x, y))
                VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, 1), x, y);
        }
    }

    VDP_setHorizontalScroll(BG_A, 0);
}
