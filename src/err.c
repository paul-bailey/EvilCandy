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

#define exception_validate(X) tuple_validate(X, "*s", 0)

/* this consume references for args if they are Objects */
static Object *
mkexception(char *fmt, void *exc_arg, void *msg_arg)
{
        Object *tup, **tupdata;
        int i;
        void *args[2] = { exc_arg, msg_arg };

        bug_on(strlen(fmt) != 2);
        tup = tuplevar_new(2);
        tupdata = tuple_get_data(tup);
        for (i = 0; i < 2; i++) {
                switch (fmt[i]) {
                case 's':
                        tupdata[i] = stringvar_new((char *)args[i]);
                        break;
                case 'S':
                case 'O':
                        tupdata[i] = (Object *)args[i];
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
        _Exit(EXIT_FAILURE);
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

        _Exit(EXIT_FAILURE);
}

static void
err_vsetstr(Object *exc, const char *msg, va_list ap)
{
        char msg_buf[100];
        size_t len = sizeof(msg_buf);
        Object *new_exc;
        memset(msg_buf, 0, len);

        vsnprintf(msg_buf, len - 1, msg, ap);

        /*
         * @exc is not from the user stack.  It's a meant-to-be-immortal
         * 'XxxxError' exception value, provided by internal code.  For
         * convenience, the caller did not produce a reference.  So we
         * produce a reference here to keep it from getting destroyed when
         * err_get consumes it next.
         */
        VAR_INCR_REF(exc);

        new_exc = mkexception("Os", exc, msg_buf);
        replace_exception(new_exc);
        VAR_DECR_REF(new_exc);
}

/**
 * err_setstr - Set an exception by value and string
 * @exc: Value, a XxxxError object, like RuntimeError
 * @msg: Formatted text to write to the exception message
 */
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
 * err_set_from_user - Like err_setstr, except pass a tuple
 * @exc: Exception to set.  (The reference for this will be consumed.)
 *       This must be either a tuple containing two printable objects
 *       ('value' and 'message', in that order), an integer (which will
 *       be regarded as the value), or a string (the message, in which
 *       RuntimeError is assumed to be the value).
 */
void
err_set_from_user(Object *exc)
{
        if (!isvar_tuple(exc)) {
                Object *tmp;
                tmp = mkexception("Os", exc, "(no message provided)");
                /*
                 * Do not consume @exc's reference here.
                 * The mkexception calls above did that for us.
                 */
                exc = tmp;
        } else if (exception_validate(exc) != RES_OK) {
                goto invalid;
        }

        replace_exception(exc);
        VAR_DECR_REF(exc);
        return;

invalid:
        err_setstr(TypeError, "Throwing invalid exception");
        VAR_DECR_REF(exc);
}

/*
 * Calling code is responsible for consuming reference on return value
 * when their done with it.
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
        const char *errval, *errmsg;
        bool tty;
        Object *v, *msg;

        if (!exc)
                return;

        bug_on(exception_validate(exc) != RES_OK);

        v = var_str_swap(tuple_getitem(exc, 0));
        errval = string_cstring(v);

        msg = var_str_swap(tuple_getitem(exc, 1));
        errmsg = string_cstring(msg);

        tty = !!isatty(fileno(fp));

        fprintf(fp, "[EvilCandy] %s%s%s %s\n",
                tty ? COLOR_RED : "", errval,
                tty ? COLOR_DEF : "", errmsg);

        VAR_DECR_REF(v);
        VAR_DECR_REF(msg);
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
        Object *key = var_str(deref);
        err_setstr(TypeError, "Cannot %s attribute %s of type %s",
                   getorset, string_cstring(key), typestr(obj));
        VAR_DECR_REF(key);
}

void
err_index(Object *index)
{
        Object *key = var_str(index);
        err_setstr(IndexError, "Subscript %s out of range",
                   string_cstring(key));
        VAR_DECR_REF(key);
}

/* @what: name of expected argument type */
void
err_argtype(const char *what)
{
        err_setstr(TypeError, "Expected argument type: %s", what);
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
        err_setstr(TypeError,
                   "%s operator not permitted for type %s",
                   op, typestr(var));
}

/* @op same as with err_permit */
void
err_permit2(const char *op, Object *a, Object *b)
{
        err_setstr(TypeError,
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

void
err_notreal(const char *tpname)
{
        err_setstr(TypeError, "Expected real number but got %s", tpname);
}

void
err_doublearg(const char *argname)
{
        err_setstr(ArgumentError, "Argument '%s' already set", argname);
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
                err_setstr(ArgumentError,
                           "%s argument missing",
                           want->name);
        } else {
                bug_on(v->v_type == want);
                err_setstr(TypeError,
                           "Expected argument %s but got %s",
                           want->name, typestr(v));
        }
        return RES_ERROR;
}


