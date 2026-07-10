#include "timing.h"

#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

static void expect_true(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        failures++;
    } else {
        printf("PASS: %s\n", msg);
    }
}

int main(void)
{
    expect_true(compression_detected(40, 160, 100) == 1,
                "delta 120 > 100 detects compression");
    expect_true(compression_detected(40, 90, 100) == 0,
                "delta 50 <= 100 does not detect");
    expect_true(compression_detected(100, 200, 100) == 0,
                "delta exactly 100 is not greater than threshold");
    expect_true(timing_delta_ms(10, 55) == 45, "timing_delta_ms basic");

    struct timespec a = {.tv_sec = 1, .tv_nsec = 500000000L};
    struct timespec b = {.tv_sec = 2, .tv_nsec = 700000000L};
    struct timespec d = timespec_delta(a, b);
    expect_true(d.tv_sec == 1 && d.tv_nsec == 200000000L, "timespec_delta");
    expect_true(timespec_to_ms(d) == 1200, "timespec_to_ms includes seconds");

    struct timeval t0 = {.tv_sec = 10, .tv_usec = 0};
    struct timeval t1 = {.tv_sec = 10, .tv_usec = 500000};
    expect_true(timeval_delta_ms(t0, t1) == 500, "timeval_delta_ms");

    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
