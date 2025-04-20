#ifndef EVILCANDY_DEBUG_H
#define EVILCANDY_DEBUG_H

#include <stdio.h>

#ifndef NDEBUG
# define DBUG(msg, ...) \
        fprintf(stderr, "[EvilCandy DEBUG]: " msg "\n", __VA_ARGS__)
# define DBUG1(msg) DBUG("%s", msg)
# define DBUG_FN(msg)   \
        DBUG("function %s line %d: %s", __FUNCTION__, __LINE__, msg)
#else
# define DBUG(...)      do { (void)0; } while (0)
# define DBUG_FN(msg)   do { (void)0; } while (0)
#endif

#ifndef NDEBUG
# define bug() bug__(__FILE__, __LINE__)
# define bug_on(cond_) do { if (cond_) bug(); } while (0)
#else
# define bug()          do { (void)0; } while (0)
# define bug_on(...)    do { (void)0; } while (0)
#endif
/*
 * these should never be in C code for more than a few seconds
 * while testing something.
 */
#define breakpoint() breakpoint__(__FILE__, __LINE__)
#define breakpoint_if(cond)     do { if (cond) breakpoint(); } while (0)

/* err.c */
extern void bug__(const char *, int);
extern void breakpoint__(const char *file, int line);

#endif /* EVILCANDY_DEBUG_H */
