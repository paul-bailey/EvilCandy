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
                err_minargs(argc, min);
                return RES_ERROR;
        }
        if (max >= 0 && argc > max) {
                err_maxargs(argc, max);
                return RES_ERROR;
        }
        return RES_OK;
}

enum result_t
function_unpack_args(Frame *fr, struct funcvar_t *fh, Object *args)
{
        size_t i;
        if (!args) {
                fr->ap = 0;
        } else if (fh->f_optind >= 0) {
                if (fh->f_optind > seqvar_size(args)) {
                        err_setstr(ArgumentError,
                                "expected %ld args but got %ld",
                                (long)fh->f_optind, (long)seqvar_size(args));
                        fr->ap = 0;
                        return RES_ERROR;;
                }

                /*
                 * XXX: extern hook to array_delete_chunk at the end of
                 * the loop is faster than array_setitem(...NULL) inside
                 * the loop.  Replace this with:
                 *
                 *      data = array_get_data(args);
                 *      memcpy(fr->stack, data, fh->f_optind * sizeof(Object *));
                 *      for (i = 0; i < fh->f_optind; i++)
                 *              VAR_INCR_REF(fr->stack[i]);
                 *      array_delete_chunk(args, 0, fh->f_optind);
                 */
                for (i = 0; i < fh->f_optind; i++) {
                        fr->stack[i] = array_getitem(args, 0);
                        array_setitem(args, 0, NULL);
                }

                fr->stack[fh->f_optind] = VAR_NEW_REF(args);
                fr->ap = fh->f_optind + 1;
        } else {
                fr->ap = seqvar_size(args);
                if (!vm_pointers_in_stack(fr->stack, fr->stack + fr->ap)) {
                        err_setstr(ArgumentError,
                                "Cannot uppack args: stack would overflow");
                        return RES_ERROR;
                }
                for (i = 0; i < fr->ap; i++) {
                        fr->stack[i] = array_getitem(args, i);
                        bug_on(!fr->stack[i]);
                }
        }
        return RES_OK;
}

/**
 * function_call - prep VM frame and call function
 * @fr: Frame used for this function.  Its stack base and AP have already
 *      been set up, but the args have not yet been added.
 * @args: Non-keyword args, an array.  This will likely be mutated during
 *      the function call.
 * @kwargs: If non-NULL, a dictionary of caller's keyword arguments
 *
 * Return: The function result, or ErrorVar if there was an error here or
 *      in the function called.
 */
Object *
function_call(Frame *fr, Object *args, Object *kwargs)
{
        struct funcvar_t *fh;

        if (!isvar_function(fr->func)) {
                err_setstr(ValueError, "Object is not callable");
                goto err_consume_kwargs;
        }

        fh = V2FUNC(fr->func);
        bug_on(fh->f_magic != FUNC_INTERNAL && fh->f_magic != FUNC_USER);

        if (kwargs){
                /*
                 * This looks asymmetric, but the idea is if we created
                 * kwargs in function_call(), then we destroy it; if not,
                 * then calling code decides whether or not to destroy it.
                 * This produce here makes the consume below balanced.
                 */
                VAR_INCR_REF(kwargs);
                if (fh->f_kwind < 0) {
                        err_setstr(ArgumentError,
                                   "Keyword arguments not supported for this function");
                        goto err_consume_kwargs;
                }
        } else if (fh->f_kwind >= 0) {
                /*
                 * I would love to change this to something like
                 * "dict = VAR_NEW_REF(gbl.empty_dict)", but it's
                 * possible for a script function to stuff an item
                 * into its keyword-arg dictionary.  So make them
                 * always be unique.
                 */
                kwargs = dictvar_new();
        }
        /* else, leave kwargs NULL */

        if (function_unpack_args(fr, fh, args) == RES_ERROR)
                goto err_consume_kwargs;

        /* Put dict onto the stack at the correct spot */
        if (kwargs)
                fr->stack[fr->ap++] = kwargs;

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

err_consume_kwargs:
        if (kwargs)
                VAR_DECR_REF(kwargs);
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
 * function_get_executable - Return XptrType object if @func is user-
 *                           defined, or NULL if not.
 *
 * This produces a reference for the return value;
 */
Object *
function_get_executable(Object *func)
{
        struct funcvar_t *fh = V2FUNC(func);
        bug_on(!isvar_function(func));

        if (fh->f_magic != FUNC_USER)
                return NULL;

        return VAR_NEW_REF((Object *)fh->f_ex);
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
        .flags  = 0,
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

