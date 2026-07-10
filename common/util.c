#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fatal(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

void fatalf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

void *xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr) {
        fatal("malloc");
    }
    memset(ptr, 0, size);
    return ptr;
}

char *xstrdup(const char *s)
{
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (!copy) {
        fatal("malloc");
    }
    memcpy(copy, s, len);
    return copy;
}
