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

static Object *exception_last = NULL;

#define exception_validate(X) tuple_validate(X, "ss", 0)

/*
 * XXX: I should just create an ExceptionType object,
 * but is that overkill?
 */
/* this consume references for args if they are Objects */
static Object *
mkexception(char *fmt, void *exc_arg, void *msg_arg)
{
        Object *tup;
        int i;
        void *args[2] = { exc_arg, msg_arg };

        bug_on(strlen(fmt) != 2);
        tup = tuplevar_new(2);
        for (i = 0; i < 2; i++) {
                Object *tmp;
                switch (fmt[i]) {
                case 's':
                        tmp = stringvar_new((char *)args[i]);
                        array_setitem(tup, i, tmp);
                        VAR_DECR_REF(tmp);
                        break;
                case 'S':
                        array_setitem(tup, i, (Object *)args[i]);
                        VAR_DECR_REF((Object *)args[i]);
                        break;
                case 'O':
                        tmp = (Object *)args[i];
                        if (!isvar_string(tmp))
                                tmp = var_str(tmp);

                        array_setitem(tup, i, tmp);
                        VAR_DECR_REF(tmp);
                        if (tmp != (Object *)args[i])
                                VAR_DECR_REF((Object *)args[i]);
                        break;
                default:
                        bug();
                }
        }
        return tup;
}

static void
replace_exception(Object *newexc)
{
        if (exception_last)
                VAR_DECR_REF(exception_last);

        if (newexc)
                VAR_INCR_REF(newexc);

        exception_last = newexc;
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
        Object *new_exc;
        memset(msg_buf, 0, len);

        vsnprintf(msg_buf, len - 1, msg, ap);

        new_exc = mkexception("Os", exc, msg_buf);
        replace_exception(new_exc);
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
err_set_from_user(Object *exc)
{
        if (!isvar_tuple(exc)) {
                Object *tmp;
                if (isvar_int(exc)) {
                        VAR_INCR_REF(NullVar);
                        tmp = mkexception("OO", exc, NullVar);
                } else if (isvar_string(exc)) {
                        /*
                         * XXX more likely 'throw "some message"',
                         *     but possible 'throw RuntimeError',
                         *     which is also a string.
                         */
                        VAR_INCR_REF(RuntimeError);
                        tmp = mkexception("SS", RuntimeError, exc);
                } else {
                        goto invalid;
                }
                VAR_DECR_REF(exc);
                exc = tmp;
        } else if (exception_validate(exc) != RES_OK) {
                if (seqvar_size(exc) != 2)
                        goto invalid;

                /* exc might not be all strings, stringify it */
                Object *tmp;
                Object *v1 = array_getitem(exc, 0);
                Object *v2 = array_getitem(exc, 1);
                tmp = mkexception("OO", v1, v2);
                VAR_DECR_REF(exc);
                exc = tmp;
        }

        replace_exception(exc);
        return;

invalid:
        err_setstr(RuntimeError, "Throwing invalid exception");
        VAR_DECR_REF(exc);
}

/*
 * If *msg is non-NULL, calling code is responsible for calling free().
 */
Object *
err_get(void)
{
        Object *ret = exception_last;
        VAR_INCR_REF(exception_last);
        replace_exception(NULL);
        return ret;
}

void
err_print(FILE *fp, Object *exc)
{
        char *errval, *errmsg;
        bool tty;
        Object *v, *msg;

        if (!exc)
                return;

        bug_on(exception_validate(exc) != RES_OK);

        v   = array_getitem(exc, 0);
        msg = array_getitem(exc, 1);

        bug_on(!isvar_string(v));
        bug_on(!isvar_string(msg));

        errval = string_get_cstring(v);
        errmsg = string_get_cstring(msg);

        tty = !!isatty(fileno(fp));

        fprintf(fp, "[EvilCandy] %s%s%s %s\n",
                tty ? COLOR_RED : "", errval,
                tty ? COLOR_DEF : "", errmsg);
}

/* get last error, print it, then clear it */
void
err_print_last(FILE *fp)
{
        Object *exc;

        if(!err_occurred())
                return;

        exc = err_get();
        err_print(fp, exc);
        VAR_DECR_REF(exc);
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
        replace_exception(NULL);
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


