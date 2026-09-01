#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include <genesis.h>

#define SCANLINES_PER_FRAME 262

extern volatile u32 g_benchFrameCounter;

void bench_init(void);
u32 bench_start(void);
u32 bench_frames_elapsed(u32 start);
void bench_report(const char *sys, const char *test, u32 frames, u32 reps, u16 entities);
void bench_report_raw(const char *sys, const char *test, u32 frames);

#endif