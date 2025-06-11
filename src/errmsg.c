/*
 * errmsg.c - Helper functions for frequently-printed error messages.
 *
 * Most of these are syntactic-sugar wrappers to err_setstr.
 */
#include <evilcandy.h>
#include <errno.h>

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
err_notreal(const char *tpname)
{
        err_setstr(TypeError, "Expected real number but got %s", tpname);
}

void
err_doublearg(const char *argname)
{
        err_setstr(ArgumentError, "Argument '%s' already set", argname);
}

static void
err_nargs(int nargs, int expect, const char *what)
{
        err_setstr(ArgumentError, "Expected %s%d args but got %d",
                   what, expect, nargs);
}

void
err_exactargs(int nargs, int expect)
{
        err_nargs(nargs, expect, "");
}

void
err_minargs(int nargs, int expect)
{
        err_nargs(nargs, expect, "at least ");
}

void
err_frame_minargs(Frame *fr, int expect)
{
        err_minargs(vm_get_argc(fr), expect);
}

void
err_va_minargs(Object *varargs, int expect)
{
        bug_on(!isvar_seq(varargs));
        err_minargs(seqvar_size(varargs), expect);
}

void
err_maxargs(int nargs, int expect)
{
        err_nargs(nargs, expect, "at most ");
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
                           "'%s' argument missing",
                           want->name);
        } else {
                bug_on(v->v_type == want);
                err_setstr(TypeError,
                           "Invalid type for argument '%s': '%s'",
                           want->name, typestr(v));
        }
        return RES_ERROR;
}

