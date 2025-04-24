#include <evilcandy.h>
#include <token.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
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

static char *msg_last = NULL;
static Object *exception_last = NULL;

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
err_vsetstr(Object *exc, const char *msg, va_list ap)
{
        char msg_buf[100];
        size_t len = sizeof(msg_buf);
        memset(msg_buf, 0, len);

        vsnprintf(msg_buf, len - 1, msg, ap);
        if (msg_last)
                efree(msg_last);
        msg_last = estrdup(msg_buf);
        exception_last = exc;
}

void
err_setstr(Object *exc, const char *msg, ...)
{
        va_list ap;
        /*
         * XXX REVISIT: @exc could be NULL if a function is called and
         * encounters an error during early initialization, before the
         * XxxError pointers have been set.  But fatal system errors do
         * not call err_setstr, and users can't cause an error so early,
         * only bad C code can.  So it's a bug, right?
         */
        bug_on(exc == NULL);
        va_start(ap, msg);
        err_vsetstr(exc, msg, ap);
        va_end(ap);
}

/*
 * If *msg is non-NULL, calling code is responsible for calling free().
 */
void
err_get(Object **exc, char **msg)
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
err_print(FILE *fp, Object *exc, char *msg)
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
        Object *exc;
        err_get(&exc, &emsg);
        err_print(fp, exc, emsg);
        if (emsg)
                efree(emsg);
}

/*
 *      Below are some syntactic sugar wrappers to err_setstr()
 */

/* @getorset: either "get" or "set" */
void
err_attribute(const char *getorset, Object *deref, Object *obj)
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
err_permit(const char *op, Object *var)
{
        err_setstr(RuntimeError,
                   "%s operator not permitted for type %s",
                   op, typestr(var));
}

/* @op same as with err_permit */
void
err_permit2(const char *op, Object *a, Object *b)
{
        err_setstr(RuntimeError,
                   "%s operator not permitted between %s and %s",
                   op, typestr(a), typestr(b));
}

void
err_errno(const char *msg, ...)
{
        char msgbuf[100];
        va_list ap;
        ssize_t n;

        memset(msgbuf, 0, sizeof(msgbuf));
        va_start(ap, msg);
        n = vsnprintf(msgbuf, sizeof(msgbuf), msg, ap);
        va_end(ap);

        if (n <= 0) {
                if (errno)
                        err_setstr(SystemError, "%s", strerror(errno));
                else
                        err_setstr(SystemError, "(possible bug)");
        } else {
                if (errno) {
                        err_setstr(SystemError, "%s: %s",
                                   msgbuf, strerror(errno));
                } else {
                        err_setstr(SystemError, "%s", msgbuf);
                }
        }
}

bool
err_occurred(void)
{
        return exception_last != NULL;
}

void
err_clear(void)
{
        if (msg_last)
                efree(msg_last);
        exception_last = NULL;
        msg_last = NULL;
}

/*
 * slow-path completion of arg_type_check() in uarg.h.
 * figure out what error message to print and return an error value
 */
int
arg_type_check_failed(Object *v, struct type_t *want)
{
        if (!v) {
                /* XXX trapped at function_prepare_frame() time? */
                err_setstr(RuntimeError,
                           "%s argument missing",
                           want->name);
        } else {
                bug_on(v->v_type == want);
                err_setstr(RuntimeError,
                           "Expected argument %s but got %s",
                           want->name, typestr(v));
        }
        return -1;
}


