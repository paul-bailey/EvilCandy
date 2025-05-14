/*
 * function.c - FunctionType stuff
 *
 * function_call is called from the VM to execute a function, user or
 * internal.
 *
 * funcvar_new_user, function_add_default, function_add_closure, and
 * function_setattr are called from VM when executing code that builds
 * user-defined functions.
 *
 * funcvar_new_intl is called during early initialization when creating
 * variables for built-in functions.
 */
#include <evilcandy.h>
#include <xptr.h>

/**
 * struct funcvar_t - Handle to a callable function
 * @f_magic:    If FUNC_INTERNAL, function is an internal function
 *              If FUNC_USER, function is in the user's script
 * @f_minargs:  Minimum number of args that may be passed to the
 *              function
 * @f_maxargs:  Maximum number of args that may be passed to the
 *              function, or -1 if no maximum
 * @f_optind:   If >0, argument index of optional args.
 * @f_kwind:    If >0, argument index of keyword args.
 * @f_cb:       If @magic is FUNC_INTERNAL, pointer to the builtin
 *              callback
 * @f_ex:       If @magic is FUNC_USER, pointer to code to execute
 * @f_closures: If @magic is FUNC_USER, ArrayType object containing
 *              closures.
 */
struct funcvar_t {
        Object base;
        enum {
                FUNC_INTERNAL = 1,
                FUNC_USER,
        } f_magic;
        /*
         * I'd make these char to save space, but <0 means something,
         * and signed-char is not 100% portable.
         */
        short f_minargs;
        short f_maxargs;
        short f_optind;
        short f_kwind;
        union {
                /* FUNC_INTERNAL exclusives */
                Object *(*f_cb)(Frame *);

                /* FUNC_USER exclusives */
                struct {
                        struct xptrvar_t *f_ex;
                        Object *f_closures;
                };
        };
};

#define V2FUNC(v)       ((struct funcvar_t *)(v))

static int
function_argc_check(struct funcvar_t *fh, int argc)
{
        int min = fh->f_minargs;
        int max = fh->f_maxargs;
        if (argc < min) {
                err_setstr(ArgumentError,
                           "Expected at least %d args but got %d",
                           min, argc);
                return RES_ERROR;
        }
        if (max >= 0 && argc > max) {
                err_setstr(ArgumentError,
                           "Expected at most %d args but got %d",
                           max, argc);
                return RES_ERROR;
        }
        return RES_OK;
}

/**
 * function_call - prep VM frame and call function
 * @fr: Frame used for this function.  Its stack base and AP have already
 *      been set up, except that the var-args are still spread out on the
 *      stack, leaving the kwarg dictionary at the wrong place
 * @have_dict: If true, then the top of the stack contains a dictionary
 *      of the keyword arguments provided.
 *
 * Return: The function result, or ErrorVar if there was an error here or
 *      in the function called.
 */
Object *
function_call(Frame *fr, bool have_dict)
{
        Object *dict;
        struct funcvar_t *fh;
        int i;

        if (!isvar_function(fr->func))
                goto err;

        fh = V2FUNC(fr->func);
        bug_on(fh->f_magic != FUNC_INTERNAL && fh->f_magic != FUNC_USER);

        /* Pull dict off the stack, since we may need to reposition it */
        dict = NULL;
        if (have_dict){
                if (fh->f_kwind < 0) {
                        err_setstr(ArgumentError,
                                   "Keyword arguments not supported for this function");
                        return ErrorVar;
                }
                dict = fr->stack[fr->ap - 1];
                fr->ap--;
        } else if (fh->f_kwind >= 0) {
                dict = dictvar_new();
        }
        /* else, leave dict NULL */

        /* Make sure starred arg is only in optind position */
        for (i = 0; i < fr->ap; i++) {
                Object *v = fr->stack[i];
                if (isvar_star(v) && i != fh->f_optind) {
                        err_setstr(ArgumentError,
                                "Positional arguments may not be starred");
                        goto err;
                }
        }

        /* Compact optional args into a list at optind */
        if (fh->f_optind >= 0) {
                Object *opts, **vargs;
                int n;

                vargs = &fr->stack[fh->f_optind];
                n = fr->ap - fh->f_optind;
                if (n < 0) {
                        err_setstr(ArgumentError, "Missing argument");
                        if (dict && !have_dict)
                                VAR_DECR_REF(dict);
                        goto err;
                }

                if (n > 0 && isvar_star(*vargs)) {
                        /* 'starred', already compacted as arg */
                        if (n != 1) {
                                err_setstr(ArgumentError,
                                        "Starred argument must be last non-keyword argument");
                                goto err;
                        }

                        Object *star = *vargs;
                        Object *arr = star_unpack(star);
                        VAR_DECR_REF(star);

                        *vargs = arr;

                        bug_on(!isvar_array(arr));
                        bug_on(fr->ap != fh->f_optind + 1);
                } else {
                        /* Unstarred, list <- 0 or more stack items */
                        opts = arrayvar_from_stack(vargs, n, true);

                        fr->ap -= n;
                        fr->stack[fr->ap++] = opts;
                }
        }

        /* Put dict back onto the stack at the correct spot */
        if (dict) {
                bug_on(fr->ap != fh->f_kwind);
                fr->stack[fr->ap++] = dict;
        }

        /* Finished setting up args, fr->ap */
        fr->stackptr = fr->stack + fr->ap;

        if (function_argc_check(fh, fr->ap) != RES_OK)
                goto err;

        if (fh->f_magic == FUNC_USER && fh->f_closures)
                fr->clo = array_get_data(fh->f_closures);
        else
                fr->clo = NULL;

        if (fh->f_magic == FUNC_USER)
                fr->ex = fh->f_ex;

        if (fh->f_magic == FUNC_INTERNAL) {
                bug_on(!fh->f_cb);
                return fh->f_cb(fr);
        } else {
                /* FUNC_USER */
                bug_on(!fr->ex);
                fr->ppii = fr->ex->instr;
                return execute_loop(fr);
        }

err:
        /*
         * fr->ap may have changed since we started this function,
         * so we need to update fr->stackptr accordingly.
         */
        fr->stackptr = fr->stack + fr->ap;
        return ErrorVar;
}

void
function_add_closure(Object *func, Object *clo)
{
        struct funcvar_t *fh = V2FUNC(func);

        bug_on(!isvar_function(func));
        bug_on(fh->f_magic != FUNC_USER);

        if (!fh->f_closures)
                fh->f_closures = arrayvar_new(0);
        array_append(fh->f_closures, clo);
}



static Object *
funcvar_alloc(int magic)
{
        Object *func = var_new(&FunctionType);
        struct funcvar_t *fh = V2FUNC(func);
        fh->f_minargs = 0;
        fh->f_maxargs = -1;
        fh->f_optind = -1;
        fh->f_kwind = -1;
        fh->f_magic = magic;
        return func;
}

/**
 * funcvar_new_intl - create a builtin function var
 * @cb: Callback that executes the function.  It may pass the Frame
 *      to vm_get_this and vm_get_arg to retrieve its "this" and
 *      arguments.  It must return ErrorVar if it encountered an error,
 *      or some other Object return value.  If there's nothing
 *      to return, return NULL; this saves us the double-task of
 *      creating and destroying a return value that won't be used;
 *      the wrapping function will change it to NullVar for
 *      callers that must receive a return value.
 * @minargs: Minimum number of args used by the function
 * @maxargs: Maximum number of args used by the function, or -1 if
 *      number of args is variable
 */
Object *
funcvar_new_intl(Object *(*cb)(Frame *),
                 int minargs, int maxargs)
{
        Object *func = funcvar_alloc(FUNC_INTERNAL);
        struct funcvar_t *fh = V2FUNC(func);
        fh->f_cb = cb;
        fh->f_minargs = minargs;
        fh->f_maxargs = maxargs;
        return func;
}

/*
 * funcvar_from_lut - Create a built-in function from an initialization
 *                    table
 */
Object *
funcvar_from_lut(const struct type_inittbl_t *tbl)
{
        Object *func;

        func = funcvar_new_intl(tbl->fn, tbl->minargs, tbl->maxargs);
        if (tbl->optind >= 0)
                function_setattr(func, IARG_FUNC_OPTIND, tbl->optind);
        if (tbl->kwind >= 0)
                function_setattr(func, IARG_FUNC_KWIND, tbl->kwind);
        return func;
}

/**
 * function_setattr - Set a function attribute
 * @attr: An IARG_FUNC_xxx enum
 * @value: The value to set the attribute to
 */
int
function_setattr(Object *func, int attr, int value)
{
        struct funcvar_t *fh = V2FUNC(func);

        if (!isvar_function(func)) {
                err_setstr(TypeError,
                           "Cannot set function attribute for type %s",
                           typestr(func));
                return RES_ERROR;
        }

        switch (attr) {
        case IARG_FUNC_MINARGS:
                fh->f_minargs = value;
                break;
        case IARG_FUNC_MAXARGS:
                fh->f_maxargs = value;
                break;
        case IARG_FUNC_OPTIND:
                fh->f_optind = value;
                break;
        case IARG_FUNC_KWIND:
                fh->f_kwind = value;
                break;
        default:
                err_setstr(TypeError,
                           "Type function does not have enumerated attribute %d",
                           attr);
                return RES_ERROR;
        }
        return RES_OK;
}

/**
 * funcvar_new_user - create a user function var
 * @ex:         Executable code to assign to function
 */
Object *
funcvar_new_user(Object *ex)
{
        Object *func = funcvar_alloc(FUNC_USER);
        struct funcvar_t *fh = V2FUNC(func);
        bug_on(!isvar_xptr(ex));
        fh->f_ex = (struct xptrvar_t *)ex;
        fh->f_closures = NULL;
        VAR_INCR_REF((Object *)ex);
        return func;
}

static int
func_cmp(Object *a, Object *b)
{
        if (!isvar_function(b))
                return -1;
        /* wrapper function already checked b == a */
        return 1;
}

static Object *
func_str(Object *a)
{
        char buf[72];
        struct funcvar_t *f = V2FUNC(a);

        memset(buf, 0, sizeof(buf));
        if (f->f_magic == FUNC_USER) {
                snprintf(buf, sizeof(buf)-1,
                         "<function (user) at %p>", (void *)f->f_ex);
        } else {
                snprintf(buf, sizeof(buf)-1,
                         "<function (intl) at %p>", (void *)f->f_cb);
        }
        return stringvar_new(buf);
}

static bool
func_cmpz(Object *func)
{
        return false;
}

static void
func_reset(Object *func)
{
        struct funcvar_t *fh = V2FUNC(func);
        if (fh->f_magic == FUNC_USER) {
                if (fh->f_closures)
                        VAR_DECR_REF(fh->f_closures);
                if (fh->f_ex)
                        VAR_DECR_REF((Object *)fh->f_ex);
        }
}

struct type_t FunctionType = {
        .name   = "function",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct funcvar_t),
        .str    = func_str,
        .cmp    = func_cmp,
        .cmpz   = func_cmpz,
        .reset  = func_reset,
};

