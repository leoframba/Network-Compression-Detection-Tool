#ifndef NCD_UTIL_H
#define NCD_UTIL_H

#include <stddef.h>

/** Print perror(msg) and exit with EXIT_FAILURE. */
void fatal(const char *msg);

/** Print a formatted error to stderr and exit with EXIT_FAILURE. */
void fatalf(const char *fmt, ...);

/** Allocate zeroed memory or exit on failure. */
void *xmalloc(size_t size);

/** Duplicate a C string or exit on failure. */
char *xstrdup(const char *s);

#endif /* NCD_UTIL_H */
