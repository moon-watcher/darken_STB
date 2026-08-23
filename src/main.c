#include <genesis.h>

#define DARKEN_IMPLEMENTATION
#define DARKSYS_IMPLEMENTATION
#include "darken.h"
#include "darksys.h"

#include "tests/darken/benchmarks.h"
#include "tests/darken/tests.h"
#include "tests/darken/examples.h"
#include "tests/darksys/ds.h"
#include "tests/darksys/tests.h"

int main(void)
{
    BLASTEM_PROFIL_START
    darken_run_usage_example();
    darken_run_all_tests();
    darken_run_benchmarks();

    run_ds_test();
    ds_run_all_tests();
    BLASTEM_PROFIL_END

    return 0;
}
