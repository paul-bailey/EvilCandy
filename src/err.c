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

static void
syntax_noexit__(const char *filename, unsigned int line,
                const char *what, const char *msg, va_list ap)
{
        fprintf(stderr, "[EvilCandy] %s", what);
        if (filename != NULL)
                fprintf(stderr, " in file %s line %u", filename, line);
        fprintf(stderr, ": ");
        vfprintf(stderr, msg, ap);
        fputc('\n', stderr);
}

void
syntax_noexit_(const char *filename, unsigned int line, const char *msg, ...)
{
        va_list ap;
        va_start(ap, msg);
        syntax_noexit__(filename, line, COLOR(RED, "ERROR"), msg, ap);
        va_end(ap);
}

void
syntax_noexit(const char *msg, ...)
{
        va_list ap;
        va_start(ap, msg);
        syntax_noexit__(NULL, 0, COLOR(RED, "ERROR"), msg, ap);
        va_end(ap);
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
        syntax_noexit__(NULL, 0, COLOR(YEL, "WARNING"), msg, ap);
        va_end(ap);
}

void
warning_(const char *filename, unsigned int line, const char *msg, ...)
{
        va_list ap;

        va_start(ap, msg);
        syntax_noexit__(filename, line, COLOR(YEL, "WARNING"), msg, ap);
        va_end(ap);
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
        syntax_noexit__(NULL, 0, COLOR(RED, "ERROR"), msg, ap);
        va_end(ap);
        exit(1);
}

