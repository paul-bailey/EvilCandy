#include <evilcandy.h>
#include "token.h"
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#define CSI "\033["
#define COLOR_RED CSI "31m"
#define COLOR_GRN CSI "32m"
#define COLOR_YEL CSI "33m"
#define COLOR_DEF CSI "39m"

#define COLOR(what, str)      COLOR_##what str COLOR_DEF

/*
 * Get the line number in file of the program counter,
 * for error message printing
 */
/* helper to bug__ and breakpoint__ */
static void
trap(const char *what, const char *file, int line)
{
        fprintf(stderr, "%s trapped in %s line %d\n",
                what, file, line);
        exit(1);
}

void
bug__(const char *file, int line)
{
        trap(COLOR(RED, "BUG"), file, line);
}

void
breakpoint__(const char *file, int line)
{
        trap(COLOR(GRN, "BREAKPOINT"), file, line);
}

static void
syntax_msg__(const char *msg, const char *what, va_list ap)
{
        const char *file_name;
        unsigned int line;

        line = get_location(&file_name);
        if (!file_name)
                file_name = "(null)";

        fprintf(stderr, "[EvilCandy] %s in file %s line %u: ", what, file_name, line);
        vfprintf(stderr, msg, ap);
        fputc('\n', stderr);
}

/**
 * warning - Like syntax(), except that it only warns.
 * @msg: Formatted message to print
 */
void
warning(const char *msg, ...)
{
        va_list ap;

        va_start(ap, msg);
        syntax_msg__(msg, COLOR(YEL, "WARNING"), ap);
        va_end(ap);
}

/**
 * fail - Like syntax, but for system failures or library funciton failures
 * @msg: Formatted message to print before dying
 */
void
fail(const char *msg, ...)
{
        va_list ap;

        fprintf(stderr,
                "[EvilCandy] System " COLOR(RED, "ERROR") ": ");

        va_start(ap, msg);
        vfprintf(stderr, msg, ap);
        va_end(ap);

        if (errno) {
                fprintf(stderr, " (%s)\n", strerror(errno));
        } else {
                fputc('\n', stderr);
        }

        exit(1);
}


/**
 * syntax - Like syntax, except that it gets the line number and file
 *              from the program counter in @state
 */
void
syntax(const char *msg, ...)
{
        va_list ap;

        va_start(ap, msg);
        syntax_msg__(msg, COLOR(RED, "ERROR"), ap);
        va_end(ap);
        exit(1);
}

