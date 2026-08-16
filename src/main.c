#include <genesis.h>

#define DARKEN_IMPLEMENTATION
#include "darken.h"

#include "tests/examples.h"
#include "tests/tests.h"
#include "tests/benchmarks.h"


int main(void)
{
    run_usage_example();
    run_all_tests();
    run_benchmarks();

    return 0;
}
