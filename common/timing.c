#include "timing.h"

#include <stdio.h>
#include <sys/time.h>

struct timespec timespec_delta(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec - start.tv_nsec) < 0) {
        temp.tv_sec = end.tv_sec - start.tv_sec - 1;
        temp.tv_nsec = 1000000000L + end.tv_nsec - start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec - start.tv_sec;
        temp.tv_nsec = end.tv_nsec - start.tv_nsec;
    }
    return temp;
}

long timespec_to_ms(struct timespec delta)
{
    return (long)(delta.tv_sec * 1000L + delta.tv_nsec / 1000000L);
}

uint64_t timeval_delta_ms(struct timeval start, struct timeval end)
{
    uint64_t m_start = ((uint64_t)start.tv_sec * 1000ULL) + (uint64_t)(start.tv_usec / 1000);
    uint64_t m_end = ((uint64_t)end.tv_sec * 1000ULL) + (uint64_t)(end.tv_usec / 1000);
    return m_end - m_start;
}

long timing_delta_ms(long low_ms, long high_ms)
{
    return high_ms - low_ms;
}

int compression_detected(long low_ms, long high_ms, int threshold_ms)
{
    return timing_delta_ms(low_ms, high_ms) > threshold_ms;
}

void print_detection_result(long low_ms, long high_ms, int threshold_ms,
                            int sufficient_data, int json_mode)
{
    long delta = timing_delta_ms(low_ms, high_ms);
    int detected = sufficient_data && compression_detected(low_ms, high_ms, threshold_ms);

    if (json_mode) {
        printf("{\"low_ms\":%ld,\"high_ms\":%ld,\"delta_ms\":%ld,"
               "\"threshold_ms\":%d,\"sufficient_data\":%s,\"compression\":%s}\n",
               low_ms, high_ms, delta, threshold_ms,
               sufficient_data ? "true" : "false",
               detected ? "true" : "false");
        return;
    }

    printf("Low-entropy train:  %ld ms\n", low_ms);
    printf("High-entropy train: %ld ms\n", high_ms);
    printf("Delta (high - low): %ld ms\n", delta);
    printf("Threshold:          %d ms\n", threshold_ms);

    if (!sufficient_data) {
        printf("Result: Insufficient data — cannot determine compression.\n");
        return;
    }

    if (detected) {
        printf("Result: Compression detected.\n");
    } else {
        printf("Result: No compression detected.\n");
    }
}
