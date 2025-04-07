#include <evilcandy.h>
#include "token.h"
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define CSI "\033["
#define COLOR_RED CSI "31m"
#define COLOR_GRN CSI "32m"
#define COLOR_YEL CSI "33m"
#define COLOR_DEF CSI "39m"

/*
 * TODO: set some file-scope variables to COLOR(...) or "",
 * depending on whether stderr is a TTY or not.
 */
#define COLOR(what, str)      COLOR_##what str COLOR_DEF

static struct buffer_t msg_buf;
static bool msg_buf_init = false;
static char *msg_last = NULL;
static struct var_t *exception_last = NULL;

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
 * fail - Print an error message to stderr and die
 *
 * Not for user errors.  This is for fatal system errors
 * like being out of memory.
 * @msg: Formatted message to print before dying
 */
void
fail(const char *msg, ...)
{
        va_list ap;

        /* FIXME: if isatty(fileno(stderr))... */
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
err_vsetstr(struct var_t *exc, const char *msg, va_list ap)
{
        if (!msg_buf_init) {
                buffer_init(&msg_buf);
                /*
                 * put a dummy in just to be sure its string pointer
                 * is valid.
                 */
                buffer_putc(&msg_buf, '1');
                msg_buf_init = true;
        }

        buffer_reset(&msg_buf);

        /*
         * buffer_vprintf calls s[n]printf twice.
         * I assume that we're only here because we hit the slow path.
         */

        buffer_vprintf(&msg_buf, msg, ap);

        msg_last = msg_buf.s;
        exception_last = exc;
}

void
err_setstr(struct var_t *exc, const char *msg, ...)
{
        va_list ap;
        va_start(ap, msg);
        err_vsetstr(exc, msg, ap);
        va_end(ap);
}

void
err_get(struct var_t **exc, char **msg)
{
        *exc = exception_last;
        *msg = msg_last;
        /*
         * Don't decrement the reference.
         * exc is a global exception object
         */
        exception_last = NULL;
        msg_last = NULL;
}

bool
err_exists(void)
{
        return exception_last != NULL;
}

void
err_print(FILE *fp, struct var_t *exc, char *msg)
{
        char *errtype;
        bool tty;

        if (!exc || !msg)
                return;

        errtype = string_get_cstring(exc);
        bug_on(!errtype);
        tty = !!isatty(fileno(fp));

        fprintf(fp, "[EvilCandy] %s%s%s %s\n",
                tty ? COLOR_RED : "",
                errtype,
                tty ? COLOR_DEF : "",
                msg);
}

/* print last error and clear it */
void
err_print_last(FILE *fp)
{
        char *emsg;
        struct var_t *exc;
        err_get(&exc, &emsg);
        err_print(fp, exc, emsg);
}

/*
 *      Below are some syntactic sugar wrappers to err_setstr()
 */

/* @getorset: either "get" or "set" */
void
err_attribute(const char *getorset, struct var_t *deref, struct var_t *obj)
{
        err_setstr(RuntimeError, "Cannot %s attribute '%s' of type %s",
                   getorset, attr_str(deref), typestr(obj));
}

/* @what: name of expected argument type */
void
err_argtype(const char *what)
{
        err_setstr(RuntimeError, "Expected argument: %s", what);
}

void
err_locked(void)
{
        err_setstr(RuntimeError,
                   "Operation not permitted while object is locked");
}

/* @op: string expression of operation, eg "*", "+", "<<", etc. */
void
err_mismatch(const char *op)
{
        /* You can't do that with me! I'm not your type! */
        err_setstr(RuntimeError,
                   "Invalid/mismatched type for '%s' operator", op);
}

/*
 * slow-path completion of arg_type_check() in uarg.h.
 * figure out what error message to print and return an error value
 */
int
arg_type_check_failed(struct var_t *v, int want)
{
        if (!v) {
                /* XXX trapped at function_prepare_frame() time? */
                err_setstr(RuntimeError,
                           "%s argument missing",
                           typestr_(want));
        } else {
                bug_on(v->magic == want);
                err_setstr(RuntimeError,
                           "Expected argument %s but got %s",
                           typestr_(want), typestr(v));
        }
        return -1;
}


