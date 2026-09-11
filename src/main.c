#include <genesis.h>


void bench_dsmap_compare(void);


int main(void)
{
    // bench_dsmap_compare();
    dsmap_bench_deepseek_main();

    while (1)
    {
        SYS_doVBlankProcess();
    }

    return 0;
}