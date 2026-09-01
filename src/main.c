#include <genesis.h>

#define DARKEN_IMPLEMENTATION
#define BBB_IMPLEMENTATION
#define DARKSYS_IMPLEMENTATION
#include "darken.h"
#include "darken2.h"
#include "darksys.h"

#include "tests/darken/benchmarks.h"
#include "tests/darken/tests.h"
#include "tests/darken/examples.h"
#include "tests/darksys/ds.h"
#include "tests/darksys/tests.h"
#include "tests_kimi/bench.h"
#include "tests_kimi/compare.h"
#include "tests_kimi/examples.h"

int main(void)
{
    // kimi_compare();
    // kimi_benchmarks();

    //

    BLASTEM_PROFIL_START
    darken_run_usage_example();
    kimi_run_usage_example();
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
