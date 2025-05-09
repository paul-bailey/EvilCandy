/*
 * ewrappers.c - "Do the other function but throw an error if it fails"
 *
 * This should make the error reporting cleaner and more uniform.
 */
#include <evilcandy.h>
#include <stdlib.h>

#if DBUG_PROFILE_MALLOC_USAGE

enum { ALLOC_PROFILE_CAP = 16 * 1024 };

static bool can_profile = 1;
static unsigned long total_alloc_bytes = 0;
static unsigned long max_alloc_bytes = 0;

static struct alloc_profile_t {
        void *p;
        size_t size;
} ALLOC_PROFILES[ALLOC_PROFILE_CAP];

#define PROFILE_END (&ALLOC_PROFILES[ALLOC_PROFILE_CAP])
static struct alloc_profile_t *
find_unused_profile_slot(void)
{
        struct alloc_profile_t *prof;
        for (prof = ALLOC_PROFILES; prof < PROFILE_END; prof++) {
                if (prof->p == NULL)
                        return prof;
        }
        can_profile = 0;
        return NULL;
}

static struct alloc_profile_t *
find_this_profile_slot(void *p)
{
        struct alloc_profile_t *prof;
        for (prof = ALLOC_PROFILES; prof < PROFILE_END; prof++) {
                if (prof->p == p)
                        return prof;
        }
        /*
         * if not found, it's probably for one of our two places where
         * getline is used
         */
        return NULL;
}

static ssize_t
count_outstanding_mem(void)
{
        size_t count;
        struct alloc_profile_t *prof;

        if (!can_profile)
                return (ssize_t)-1;

        count = 0;
        for (prof = ALLOC_PROFILES; prof < PROFILE_END; prof++) {
                if (prof->p)
                        count += prof->size;
        }
        return count;
}
#undef PROFILE_END

static void
DBUG_LOG_MALLOC(void *p, size_t size)
{
        struct alloc_profile_t *prof;
        if (!can_profile)
                return;

        if ((prof = find_unused_profile_slot()) == NULL)
                return;

        total_alloc_bytes += size;
        if (total_alloc_bytes > max_alloc_bytes)
                max_alloc_bytes = total_alloc_bytes;
        prof->p = p;
        prof->size = size;
}

static void
DBUG_LOG_FREE(void *p)
{
        struct alloc_profile_t *prof;
        if (!can_profile)
                return;

        if ((prof = find_this_profile_slot(p)) == NULL)
                return;

        total_alloc_bytes -= prof->size;

        prof->p = NULL;
        prof->size = 0;
}

#define DBUG_LOG_MALLOC_IF(cond, p, size) do { \
        if (cond)                         \
                DBUG_LOG_MALLOC(p, size);      \
} while (0)

static void
report_alloc_stats(void)
{
        ssize_t count = count_outstanding_mem();
        if (count < 0)
                DBUG1("could not profile memory bytes");
        else
                DBUG("outstanding memory=%ld bytes", count);
        DBUG("Largest amount of memory allocated at one time: %ld",
             max_alloc_bytes);
}

#else
# define DBUG_LOG_MALLOC(...)        do { (void)0; } while (0)
# define DBUG_LOG_MALLOC_IF(...)     do { (void)0; } while (0)
# define DBUG_LOG_FREE(...)          do { (void)0; } while (0)
#endif /* DBUG_PROFILE_MALLOC_USAGE */

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
        DBUG_LOG_FREE(buf);
        DBUG_LOG_MALLOC(res, size);
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
#if DBUG_PROFILE_MALLOC_USAGE
        atexit(report_alloc_stats);
#endif
}
