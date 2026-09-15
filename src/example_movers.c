#include <stdint.h>
#include "darksys-1.0.0_dev.h"

/* --------------------------------------------------------------------------
 * Minimal fix16_t stand-in, matching SGDK's own type 1:1 (a plain int32_t,
 * 16.16 fixed point). In real SGDK code just `#include <genesis.h>` and use
 * its fix16_t/FIX16/F16toInt directly -- this stub exists only so the
 * example builds and runs standalone here, off-console.
 * ------------------------------------------------------------------------ */
typedef int32_t fix16_t;
#define FIX16(x) ((fix16_t)((x) * 65536))
#define FIX16_TO_INT(x) ((int)((x) >> 16))

/* --------------------------------------------------------------------------
 * A pool of moving entities: x, y, vx, vy per record (4 params).
 * Each fix16_t is exactly 32 bits, same width as a void* on m68k, so it is
 * stored directly as the pointer's bit pattern -- no boxing, no extra
 * allocation, just a cast in and a cast out.
 * ------------------------------------------------------------------------ */
#define MAX_MOVERS 8
#define MOVER_PARAMS 4 /* x, y, vx, vy */

DARKSYS_POOL_DECLARE(moversStorage, MAX_MOVERS, MOVER_PARAMS);
static darksys movers;

static darksys_handle mover_spawn(fix16_t x, fix16_t y, fix16_t vx, fix16_t vy)
{
    return DARKSYS_ADD(&movers, x, y, vx, vy);
}

#define assert(x) \
    if (!(x))     \
        return;

/* System: integrates position += velocity for every active mover.
 *
 * DARKSYS_FOREACH binds one local per field; each local receives the raw
 * void* bit pattern that was stored, so it must be cast back to the real
 * type before use -- exactly like reading any other value out of darksys.
 * To write the new position back in place, assign to `_pool[0]`/`_pool[1]`
 * (the same two slots xp/yp were just read from); see the comment on
 * _DARKSYS_FOREACH_RUN in darksys.h. */
static void movers_update(void)
{
    DARKSYS_FOREACH(&movers, fix16_t * x, fix16_t * y, fix16_t * vx, fix16_t * vy, {
        *x += *vx;
        *y += *vy;
    });
}

static void mover_print(darksys_handle h)
{
    void **row = DARKSYS_DATA(&movers, h);
    fix16_t x = (fix16_t)row[0];
    fix16_t y = (fix16_t)row[1];
    kprintf("  h=%u -> x=%d y=%d", h, FIX16_TO_INT(x), FIX16_TO_INT(y));
}

int example_movers(void)
{
    movers = DARKSYS_POOL_BIND(moversStorage);

    darksys_handle a = mover_spawn(FIX16(0), FIX16(0), FIX16(1), FIX16(0));    /* moves right  */
    darksys_handle b = mover_spawn(FIX16(10), FIX16(10), FIX16(0), FIX16(-1)); /* moves up   */
    darksys_handle c = mover_spawn(FIX16(5), FIX16(5), FIX16(2), FIX16(2));    /* moves diagonally */

    assert(a != DARKSYS_INVALID_HANDLE && b != DARKSYS_INVALID_HANDLE && c != DARKSYS_INVALID_HANDLE);

    kprintf("frame 0:");
    mover_print(a);
    mover_print(b);
    mover_print(c);

    for (int frame = 1; frame <= 3; ++frame)
    {
        movers_update();
        kprintf("frame %d:", frame);
        mover_print(a);
        mover_print(b);
        mover_print(c);
    }

    /* a moves +1 on x every frame, 3 frames in a row */
    void **ra = DARKSYS_DATA(&movers, a);
    assert(FIX16_TO_INT((fix16_t)ra[0]) == 3);
    assert(FIX16_TO_INT((fix16_t)ra[1]) == 0);

    /* b moves -1 on y every frame */
    void **rb = DARKSYS_DATA(&movers, b);
    assert(FIX16_TO_INT((fix16_t)rb[0]) == 10);
    assert(FIX16_TO_INT((fix16_t)rb[1]) == 7);

    /* removing b mid-pool must not disturb a or c, and their DATA pointers
     * must still resolve correctly afterwards (this is exactly what the
     * offsets[] cache in darksys_remove needs to keep in sync) */
    darksys_remove(&movers, b);
    assert(!darksys_valid(&movers, b));
    assert(darksys_valid(&movers, a) && darksys_valid(&movers, c));

    void **ra2 = DARKSYS_DATA(&movers, a);
    void **rc2 = DARKSYS_DATA(&movers, c);
    assert(FIX16_TO_INT((fix16_t)ra2[0]) == 3);
    assert(FIX16_TO_INT((fix16_t)rc2[0]) == 11); /* 5 + 2*3 */

    movers_update(); /* one more frame, now with only a and c active */
    ra2 = DARKSYS_DATA(&movers, a);
    rc2 = DARKSYS_DATA(&movers, c);
    assert(FIX16_TO_INT((fix16_t)ra2[0]) == 4);
    assert(FIX16_TO_INT((fix16_t)rc2[0]) == 13);

    /* removing an already-removed handle, or one that was never issued,
     * must be a no-op (this is the bug fixed in darksys_remove) */
    uint16_t count_before = movers.count;
    darksys_remove(&movers, b);
    darksys_remove(&movers, (darksys_handle)999);
    assert(movers.count == count_before);

    kprintf("ALL OK");
    return 0;
}