#include "entropy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    unsigned char low[64];
    unsigned char high[64];

    fill_low_entropy(low, sizeof(low));
    int all_zero = 1;
    for (size_t i = 0; i < sizeof(low); i++) {
        if (low[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    expect_true(all_zero, "low-entropy payload is all zeros");

    fill_high_entropy(high, sizeof(high));
    expect_true(high[0] == 0 && high[1] == 0, "high-entropy reserves packet id bytes");

    int differs = memcmp(low, high, sizeof(low)) != 0;
    expect_true(differs, "high-entropy differs from low-entropy");

    /* Rough check: not all bytes identical (unlikely for urandom). */
    int varied = 0;
    for (size_t i = 2; i < sizeof(high); i++) {
        if (high[i] != high[2]) {
            varied = 1;
            break;
        }
    }
    expect_true(varied, "high-entropy payload has varied bytes");

    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
