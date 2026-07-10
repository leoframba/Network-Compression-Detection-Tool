#ifndef NCD_ENTROPY_H
#define NCD_ENTROPY_H

#include <stddef.h>

/**
 * Fill buffer with low-entropy payload (zeros), matching the paper.
 * First two bytes are reserved for packet sequence id and left as 0.
 */
void fill_low_entropy(unsigned char *buffer, size_t size);

/**
 * Fill buffer with high-entropy payload from /dev/urandom (paper: /dev/random).
 * Falls back to a simple LCG if /dev/urandom is unavailable.
 * First two bytes are reserved for packet sequence id.
 */
int fill_high_entropy(unsigned char *buffer, size_t size);

#endif /* NCD_ENTROPY_H */
