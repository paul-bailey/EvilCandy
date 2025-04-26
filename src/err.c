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

static void
replace_exception(Object *newexc, char *newmsg)
{
        if (msg_last)
                efree(msg_last);
        if (exception_last)
                VAR_DECR_REF(exception_last);

        /* make sure these persist */
        if (newmsg)
                newmsg = estrdup(newmsg);
        if (newexc)
                VAR_INCR_REF(newexc);

        exception_last = newexc;
        msg_last = newmsg;
}

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

        replace_exception(exc, msg_buf);
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
 * err_setexc - Like err_setstr, except pass a tuple
 * @exc: Exception to set.  (The reference for this will be consumed.)
 *       This must be either a tuple containing two printable objects
 *       (to interpret as value and message, in that order), an integer
 *       (which will be regarded as the value), or a string (the message,
 *       in which RuntimeError is assumed to be the value).
 */
void
err_setexc(Object *exc)
{
        if (!isvar_tuple(exc)) {
                Object *tmp = tuplevar_new(2);
                if (isvar_int(exc)) {
                        array_setitem(tmp, 0, exc);
                        array_setitem(tmp, 0, NullVar);
                } else if (isvar_string(exc)) {
                        /*
                         * XXX more likely 'throw "some message"',
                         *     but possible 'throw RuntimeError',
                         *     which is also a string.
                         */
                        array_setitem(tmp, 0, RuntimeError);
                        array_setitem(tmp, 1, exc);
                } else {
                        goto invalid;
                }
                VAR_DECR_REF(exc);
                exc = tmp;
        }

        if (seqvar_size(exc) != 2)
                goto invalid;

        Object *v = array_getitem(exc, 0);
        Object *s = array_getitem(exc, 1);
        if (!isvar_string(v)) {
                Object *vs = var_str(v);
                VAR_DECR_REF(v);
                v = vs;
        }
        if (!isvar_string(s)) {
                Object *vs = var_str(s);
                VAR_DECR_REF(s);
                s = vs;
        }

        replace_exception(v, string_get_cstring(s));

        VAR_DECR_REF(s);
        VAR_DECR_REF(exc);
        return;

invalid:
        err_setstr(RuntimeError, "Throwing invalid exception");
        VAR_DECR_REF(exc);
}

/*
 * If *msg is non-NULL, calling code is responsible for calling free().
 */
void
err_get(Object **exc, char **msg)
{
        *exc = exception_last;
        *msg = estrdup(msg_last);
        VAR_INCR_REF(exception_last);
        replace_exception(NULL, NULL);
}

/**
 * Get error as a tuple res[0] is the exception value, res[1] is the message
 */
Object *
err_get_tup(void)
{
        Object *tup, *msg, *exc;
        char *cmsg;

        if (!err_occurred())
                return NULL;

        err_get(&exc, &cmsg);
        msg = cmsg ? stringvar_nocopy(cmsg) : stringvar_new("");
        tup = tuplevar_new(2);
        array_setitem(tup, 0, exc);
        array_setitem(tup, 1, msg);
        return tup;
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
        replace_exception(NULL, NULL);
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
        return RES_ERROR;
}


