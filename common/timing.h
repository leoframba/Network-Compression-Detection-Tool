#ifndef NCD_TIMING_H
#define NCD_TIMING_H

#include <stdint.h>
#include <sys/time.h>
#include <time.h>

/** Difference between two timespec values (end - start). */
struct timespec timespec_delta(struct timespec start, struct timespec end);

/** Convert a timespec duration to milliseconds. */
long timespec_to_ms(struct timespec delta);

/** Difference between two timeval values in milliseconds. */
uint64_t timeval_delta_ms(struct timeval start, struct timeval end);

/**
 * Compression verdict from the paper:
 *   compression detected iff (high_ms - low_ms) > threshold_ms
 */
int compression_detected(long low_ms, long high_ms, int threshold_ms);

/** high_ms - low_ms */
long timing_delta_ms(long low_ms, long high_ms);

/**
 * Print a human-readable or JSON result line.
 * json_mode: if non-zero, emit a single JSON object.
 */
void print_detection_result(long low_ms, long high_ms, int threshold_ms,
                            int sufficient_data, int json_mode);

#endif /* NCD_TIMING_H */
