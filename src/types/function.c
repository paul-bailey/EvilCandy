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
#include <evilcandy/global.h>
#include <evilcandy/err.h>
#include <evilcandy/errmsg.h>
#include <internal/errmsg.h>
#include <internal/type_registry.h>
#include <internal/types/xptr.h>
#include <internal/types/sequential_types.h>
#include <internal/vm.h>

/**
 * struct funcvar_t - Handle to a callable function
 * @f_magic:    If FUNC_INTERNAL, function is an internal function
 *              If FUNC_USER, function is in the user's script
 * @f_nargs:    Number of args that may be passed to the function
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
        short f_nargs;
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
        if (argc != fh->f_nargs) {
                if (argc < fh->f_nargs)
                        err_minargs(argc, fh->f_nargs);
                else
                        err_maxargs(argc, fh->f_nargs);
                return RES_ERROR;
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
function_call(Frame *fr, Object *func, Object *args, Object *kwargs)
{
        struct funcvar_t *fh;
        size_t nr_args;

        if (!isvar_function(func)) {
                err_setstr(ValueError, "Object is not callable");
                return ErrorVar;
        }

        fh = V2FUNC(func);
        bug_on(fh->f_magic != FUNC_INTERNAL && fh->f_magic != FUNC_USER);

        if (kwargs){
                if (fh->f_kwind < 0) {
                        err_setstr(ArgumentError,
                                   "Keyword arguments not supported for this function");
                        return ErrorVar;
                }

                /*
                 * Since we could be creating kwargs (the clause below),
                 * produce a reference here to keep it balanced.
                 */
                VAR_INCR_REF(kwargs);
        } else if (fh->f_kwind >= 0) {
                kwargs = dictvar_new();
        }
        /* else, leave kwargs NULL */

        if (vmframe_unpack_args(fr, fh->f_optind, args,
                                kwargs, &nr_args) == RES_ERROR) {
                if (kwargs)
                        VAR_DECR_REF(kwargs);
                return ErrorVar;
        }

        /*
         * If kwargs is non-NULL, ownership of its reference is with VM
         * now that it's on the stack, so we no longer fuss over its
         * reference count.
         */
        if (function_argc_check(fh, nr_args) != RES_OK)
                return ErrorVar;

        if (fh->f_magic == FUNC_INTERNAL) {
                bug_on(!fh->f_cb);
                return fh->f_cb(fr);
        } else {
                /* FUNC_USER */
                Object **closures = fh->f_closures
                                    ? array_get_data(fh->f_closures)
                                    : NULL;
                if (vmframe_finish_stack_setup(fr, fh->f_ex, closures)
                    == RES_ERROR) {
                        return ErrorVar;
                }
                return execute_loop(fr);
        }

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
        fh->f_nargs = 0;
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
funcvar_new_intl(Object *(*cb)(Frame *))
{
        Object *func = funcvar_alloc(FUNC_INTERNAL);
        struct funcvar_t *fh = V2FUNC(func);
        fh->f_cb = cb;
        fh->f_nargs = 2;
        fh->f_optind  = 0;
        fh->f_kwind   = 1;
        return func;
}

/*
 * funcvar_from_lut - Create a built-in function from an initialization
 *                    table
 */
Object *
funcvar_from_lut(const struct type_method_t *tbl)
{
        return funcvar_new_intl(tbl->fn);
}

/**
 * funcvar_new_user - create a user function var
 * @ex:         Executable code to assign to function
 * @argspec:    3-sized tuple containing argument specifications, in order of
 *              (nr_args, var_args_index, kwargs_index), or NULL to use the
 *              default for scripts and built-ins: (2, 0, 1)
 */
Object *
funcvar_new_user(Object *ex, Object *argspec)
{
        Object *func = funcvar_alloc(FUNC_USER);
        struct funcvar_t *fh = V2FUNC(func);
        bug_on(!isvar_xptr(ex));
        if (argspec) {
                enum result_t res;
                bug_on(seqvar_size(argspec) != 3);
                bug_on(!isvar_tuple(argspec));
                res = vm_getargs_sv(argspec, "(hhh)",
                                    &fh->f_nargs,
                                    &fh->f_optind,
                                    &fh->f_kwind);
                bug_on(fh->f_nargs < fh->f_optind);
                bug_on(fh->f_nargs < fh->f_kwind);
                bug_on(res == RES_ERROR);
                (void)res;
        } else {
                /* default argspec */
                fh->f_nargs = 2;
                fh->f_optind = 0;
                fh->f_kwind = 1;
        }
        fh->f_ex = (struct xptrvar_t *)ex;
        fh->f_closures = NULL;
        VAR_INCR_REF(ex);
        return func;
}

static Object *
func_str(Object *a)
{
        char buf[72];
        struct funcvar_t *f = V2FUNC(a);

        memset(buf, 0, sizeof(buf));
        if (f->f_magic == FUNC_USER) {
                evc_sprintf(buf, sizeof(buf),
                            "<function (user) at %p>", (void *)f->f_ex);
        } else {
                evc_sprintf(buf, sizeof(buf), "<function (intl)>");
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

static Object *
func_getcode(Object *self)
{
        struct xptrvar_t *x;
        Object *tp[2];

        if (V2FUNC(self)->f_magic == FUNC_INTERNAL) {
                err_setstr(TypeError,
                        "code for internal function not available");
                return ErrorVar;
        }
        x = V2FUNC(self)->f_ex;

        tp[0] = bytesvar_new((unsigned char *)x->instr,
                                x->n_instr * sizeof(instruction_t));
        tp[1] = VAR_NEW_REF(x->rodata);
        return tuplevar_from_stack(tp, 2, true);
}

static Object *
func_getrodata(Object *self)
{
        struct xptrvar_t *x;

        if (V2FUNC(self)->f_magic == FUNC_INTERNAL) {
                err_setstr(TypeError,
                        "code for internal function not available");
                return ErrorVar;
        }
        x = V2FUNC(self)->f_ex;
        return VAR_NEW_REF(x->rodata);
}

static const struct type_prop_t func_prop_getsets[] = {
        { .name = "__code__",   .getprop = func_getcode,   .setprop = NULL },
        { .name = "__rodata__", .getprop = func_getrodata, .setprop = NULL },
        /* TODO: Add #args read-only property */
        { .name = NULL },
};

struct type_t FunctionType = {
        .flags  = 0,
        .name   = "function",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct funcvar_t),
        .str    = func_str,
        .cmp    = NULL,
        .cmpz   = func_cmpz,
        .cmpeq  = NULL,
        .reset  = func_reset,
        .prop_getsets = func_prop_getsets,
        .hash   = NULL,
};

