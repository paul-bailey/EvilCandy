/*
 * ewrappers.c - "Do the other function but throw an error if it fails"
 *
 * This should make the error reporting cleaner and more uniform.
 */
#include <evilcandy.h>
#include <stdlib.h>
#include <string.h>

/**
 * estrdup - error-handling wrapper to strdup
 */
char *
estrdup(const char *s)
{
        char *res = strdup(s);
        if (!res)
                fail("strdup failed");
        return res;
}

/**
 * emalloc - error-handling wrapper to malloc
 */
void *
emalloc(size_t size)
{
        void *res = malloc(size);
        if (!res)
                fail("malloc failed");
        return res;
}

/**
 * ecalloc - like emalloc but it initializes allocated memory to 0
 */
void *
ecalloc(size_t size)
{
        void *res = emalloc(size);
        memset(res, 0, size);
        return res;
}


