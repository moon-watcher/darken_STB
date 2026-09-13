#include <genesis.h>
#include "../darksys.h"

#define BENCH_ITERATIONS 10000
#define BENCH_FRAMES 60

static u32 bench_add(void)
{
    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 64, 1);

    void *value = (void *) 1;

    u32 total = 0;

    u16 frame;
    u16 i;

    for (frame = 0; frame < BENCH_FRAMES; frame++)
    {
        system.size = 0;

        for (i = 0; i < BENCH_ITERATIONS; i++)
        {
            if (!DARKSYS_ADD(&system, value))
                system.size = 0;
        }

        total += system.size;

        VDP_waitVSync();
    }

    MEM_free(system.pool);

    return total;
}

static u32 bench_foreach(void)
{
    darksys system = DARKSYS_POOL_ALLOC(MEM_alloc, 64, 2);

    void *a = (void *) 1;
    void *b = (void *) 2;

    u16 i;

    for (i = 0; i < 128; i++)
    {
        DARKSYS_ADD(&system, a);
        DARKSYS_ADD(&system, b);
    }

    u32 total = 0;

    u16 frame;

    for (frame = 0; frame < BENCH_FRAMES; frame++)
    {
        void *x;
        void *y;

        DARKSYS_FOREACH(&system, x, y,
        {
            if (x == a && y == b)
                total++;
        });

        VDP_waitVSync();
    }

    MEM_free(system.pool);

    return total;
}

void darksys_run_benchmarks(void)
{
    u32 add = bench_add();
    u32 foreach = bench_foreach();

    KLog_U1("DARKSYS ADD result: ", add);
    KLog_U1("DARKSYS FOREACH result: ", foreach);
}