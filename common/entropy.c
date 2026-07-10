#include "entropy.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void fill_low_entropy(unsigned char *buffer, size_t size)
{
    memset(buffer, 0, size);
}

int fill_high_entropy(unsigned char *buffer, size_t size)
{
    if (size == 0) {
        return 0;
    }

    FILE *urandom = fopen("/dev/urandom", "rb");
    if (urandom) {
        size_t nread = fread(buffer, 1, size, urandom);
        fclose(urandom);
        if (nread == size) {
            /* Packet id bytes are overwritten by the sender. */
            buffer[0] = 0;
            if (size > 1) {
                buffer[1] = 0;
            }
            return 0;
        }
    }

    /* Fallback PRNG if /dev/urandom is unavailable. */
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)(size * 2654435761u);
    for (size_t i = 0; i < size; i++) {
        seed = seed * 1103515245u + 12345u;
        buffer[i] = (unsigned char)((seed >> 16) & 0xFF);
    }
    buffer[0] = 0;
    if (size > 1) {
        buffer[1] = 0;
    }
    return 0;
}
