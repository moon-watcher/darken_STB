#include <genesis.h>

#define DARKEN_IMPLEMENTATION
#define BBB_IMPLEMENTATION
#define CCC_IMPLEMENTATION

// #include "darken-1.1.0_dev.h"
// #include "_bbb.h"
// #include "_ccc.h"

#define DARKSYS_IMPLEMENTATION
#include "darksys.h"

#define DSMAP_IMPLEMENTATION
#include "dsmap.h"

#define VSMAP_IMPLEMENTATION
#include "vsmap.h"

// #include "tests/darken/benchmarks.h"
// #include "tests/darken/tests.h"
// #include "tests/darken/examples.h"
// #include "tests/darksys/ds.h"
// #include "tests/darksys/tests.h"
#include "tests/darksys/tests2.h"
// #include "tests_kimi/bench.h"
// #include "tests_kimi/compare.h"
// #include "tests_kimi/examples.h"

int main(void)
{
    // vsmap_run_tests();
    // dsmap_run_tests();

    // bench_darksys();
    // test_darksys();
    // BLASTEM_PROFIL_START
    // darksys_run_benchmarks();
    // BLASTEM_PROFIL_END

    BLASTEM_PROFIL_START
    darksys_run_tests();
    BLASTEM_PROFIL_END

    // kimi_compare();
    // kimi_benchmarks();

    //

    BLASTEM_PROFIL_START
    // darken_run_usage_example();
    // kimi_run_usage_example();
    BLASTEM_PROFIL_END

    //

    BLASTEM_PROFIL_START
    // darken_run_usage_example();
    // darken_run_all_tests();
    // darken_run_benchmarks();

    // run_ds_test();
    // ds_run_all_tests();
    BLASTEM_PROFIL_END

    return 0;
}
