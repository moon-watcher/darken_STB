#include <genesis.h>


void bench_dsmap0_compare(void);


int main(void)
{
    bench_dsmap0_compare();
    // dsmap0_bench_deepseek_main();

    while (1)
    {
        SYS_doVBlankProcess();
    }

    return 0;
}