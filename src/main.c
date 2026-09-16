#include <genesis.h>

#define DARKEN_DIRECT

int main(void)
{
    // bench_dsmap0_compare();
    // dsmap0_bench_deepseek_main();
    // bench_darksys_compare();
    // bench_darksys89_compare();
    // test_darksys_sgdk_main();
    // bench_darksys_sgdk_main();
    // dsmap_test_main();
    // darken8_test_main();
    // test_vsmap_main();
    // test_darksys_main();
    // darken_run_benchmarks();
    // example_movers();
    // test_darksys();


    darken_run_tests();

    uint32_t r;
    r = darken_bench_init(1000);         kprintf("darken_bench_init: %d", r);
    r = darken_bench_spawn(1000);        kprintf("darken_bench_spawn: %d", r);
    r = darken_bench_update(1000);       kprintf("darken_bench_update: %d", r);
    r = darken_bench_foreach(1000);      kprintf("darken_bench_foreach: %d", r);
    r = darken_bench_pause_resume(1000); kprintf("darken_bench_pause_resume: %d", r);
    r = darken_bench_delete(1000);       kprintf("darken_bench_delete: %d", r);


    return 0;
}