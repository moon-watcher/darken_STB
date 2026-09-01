#include "bench_common.h"

volatile u32 g_benchFrameCounter = 0;

static void bench_vblank_cb(void) { g_benchFrameCounter++; }

void bench_init(void) { SYS_setVIntCallback(bench_vblank_cb); }

u32 bench_start(void) { return g_benchFrameCounter; }

u32 bench_frames_elapsed(u32 start) { return g_benchFrameCounter - start; }

void bench_report(const char *sys, const char *test, u32 frames, u32 reps, u16 entities)
{
    u32 totalScanlines = frames * SCANLINES_PER_FRAME;
    u32 sl_per_op      = totalScanlines / reps;
    u32 sl_per_ent     = totalScanlines / (reps * entities);
    kprintf("[%s] %s", sys, test);
    kprintf("  frames=%ld  scanlines/op=%ld  scanlines/ent=%ld",
            frames, sl_per_op, sl_per_ent);
}

void bench_report_raw(const char *sys, const char *test, u32 frames)
{
    kprintf("[%s] %s: %ld frames (%ld scanlines)",
            sys, test, frames, frames * SCANLINES_PER_FRAME);
}