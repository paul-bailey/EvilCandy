#ifndef EVILCANDY_DEBUG_H
#define EVILCANDY_DEBUG_H

#include "config.h"
#include <stdio.h>

/*
 * DBUG_xxx_xxx... debug-mode parameters: 1 enables them and 0 disables
 * them.
 *
 * These slow the program down, and should be set to zero before checking
 * the project back in to version control.
 */

/* splash some debug data about var allocation to stderr upon exit */
#define DBUG_REPORT_VARS_ON_EXIT 0

/*
 * Check return value of each opcode callback against err_occurred().
 * Only prints message if something suspicious was detected.
 */
#define DBUG_CHECK_GHOST_ERRORS 0

/* Report memory usage at exit (WARNING!! CRIPPINGLY SLOW!) */
#define DBUG_PROFILE_MALLOC_USAGE 0

/* Print load time of input file to stderr */
#define DBUG_PROFILE_LOAD_TIME 0

/*
 * DBUG, DBUG1, and DBUG_FN print verbose debug info to stderr.
 * Invocations of these macros should not be left in the code at
 * check-in; they clutter up the output, so they're only useful when
 * temporarily debugging something specific.
 *
 * DBUG1 would == DBUG except that __VA_ARGS__ has a zero-length bug, and
 * not every compiler supports gcc's ugly-but-it-works "#args..."
 * parameter-name convention.
 */

#ifndef NDEBUG
# define DBUG(msg, ...) \
        fprintf(stderr, "[EvilCandy DEBUG]: " msg "\n", __VA_ARGS__)
# define DBUG1(msg) DBUG("%s", msg)
# define DBUG_FN(msg)   \
        DBUG("function %s line %d: %s", __FUNCTION__, __LINE__, msg)
# define bug() bug__(__FILE__, __LINE__)
# define bug_on(cond_) do { if (cond_) bug(); } while (0)
#else

# define DBUG(...)      do { (void)0; } while (0)
# define DBUG1(...)     do { (void)0; } while (0)
# define DBUG_FN(msg)   do { (void)0; } while (0)
# define bug()          do { (void)0; } while (0)
# define bug_on(...)    do { (void)0; } while (0)

# undef DBUG_REPORT_VARS_ON_EXIT
# define DBUG_REPORT_VARS_ON_EXIT 0
# undef DBUG_CHECK_GHOST_ERRORS
# define DBUG_CHECK_GHOST_ERRORS 0
# undef DBUG_PROFILE_MALLOC_USAGE
# define DBUG_PROFILE_MALLOC_USAGE 0
# undef DBUG_PROFILE_LOAD_TIME
# define DBUG_PROFILE_LOAD_TIME 0

#endif

/* This macro requires clock() and <time.h> */
#ifndef HAVE_CLOCK
# undef DBUG_PROFILE_LOAD_TIME
# define DBUG_PROFILE_LOAD_TIME 0
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
