/*
 * ewrappers.c - "Do the other function but throw an error if it fails"
 *
 * This should make the error reporting cleaner and more uniform.
 */
#include <evilcandy.h>
#include <stdlib.h>

#define REPORT_MEM_ON_EXIT 0

#ifdef NDEBUG
# undef REPORT_MEM_ON_EXIT
# define REPORT_MEM_ON_EXIT 0
#endif /* NDEBUG */

#if REPORT_MEM_ON_EXIT

static long n_alloc_calls = 0;
static long n_free_calls = 0;

static inline void DBUG_LOG_MALLOC(void *p, size_t size)
        { n_alloc_calls++; }

static inline void DBUG_LOG_FREE(void *p)
        { n_free_calls++; }

#define DBUG_LOG_MALLOC_IF(cond, p, size) do { \
        if (cond)                         \
                DBUG_LOG_MALLOC(p, size);      \
} while (0)

static void
report_alloc_stats(void)
{
        DBUG("n_alloc_calls=%ld", n_alloc_calls);
        DBUG("n_free_calls=%ld", n_free_calls);
}

#else
# define DBUG_LOG_MALLOC(...)        do { (void)0; } while (0)
# define DBUG_LOG_MALLOC_IF(...)     do { (void)0; } while (0)
# define DBUG_LOG_FREE(...)          do { (void)0; } while (0)
#endif /* REPORT_MEM_ON_EXIT */

/**
 * estrdup - error-handling wrapper to strdup
 */
char *
estrdup(const char *s)
{
        char *res = strdup(s);
        DBUG_LOG_MALLOC(res, strlen(s));
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
        DBUG_LOG_MALLOC(res, size);
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
        DBUG_LOG_MALLOC(res, size);
        memset(res, 0, size);
        return res;
}

/**
 * erealloc - error-handling wrapper to realloc
 */
void *
erealloc(void *buf, size_t size)
{
        void *res = realloc(buf, size);
        DBUG_LOG_MALLOC_IF(buf == NULL, res, size);
        if (!res)
                fail("realloc failed");
        return res;
}

void *
ememdup(void *buf, size_t size)
{
        void *ret;
        if (!size)
                size = 1;
        ret = emalloc(size);
        DBUG_LOG_MALLOC(buf, size);
        memcpy(ret, buf, size);
        return ret;
}

/**
 * efree - Free memory allocated with emalloc or one of his friends
 *
 * This isn't so much an error wrapper as it is a way to funnel all
 * the malloc/free calls through the same place.  Makes adding debug
 * hooks to track memory leaks a lot easier, and fewer <stdlib.h>
 * includes are needed.
 */
void
efree(void *ptr)
{
        bug_on(!ptr);
        DBUG_LOG_FREE(ptr);
        free(ptr);
}

void
moduleinit_ewrappers(void)
{
#if REPOR_MEM_ON_EXIT
        atexit(report_alloc_stats);
#endif
}
