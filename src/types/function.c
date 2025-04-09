/*
 * function.c - two part module
 *      1. Code that calls a function (call_function() & family)
 *      2. Code dealing specifically with struct var_t with
 *         function-like magic number
 */
#include "var.h"
#include <stdlib.h>
#include <string.h>

/**
 * struct function_handle_t - Handle to a callable function
 * @f_magic:    If FUNC_INTERNAL, function is an internal function
 *              If FUNC_USER, function is in the user's script
 * @f_minargs:  Minimum number of args that may be passed to the
 *              function, if internal function.  (TODO: need to set
 *              this for FUNC_USER too.)
 * @f_maxargs:  Maximum number of args that may be passed to the
 *              function (if it's FUNC_INTERNAL), or -1 if no maximum
 * @f_cb:       If @magic is FUNC_INTERNAL, pointer to the builtin
 *              callback
 * @f_mk:       If @magic is FUNC_USER, pointer to the user callback
 * @f_argv:     Array of defaults to fill in where arguments are not
 *              provided by the caller.  Blank slots mean the argument
 *              is mandatory.
 * @f_clov:     Array of closures.
 * @f_argc:     Highest argument number that has a default
 *
 * TODO: Some cleanup and simplification is in order:
 *      f_argv and f_clov could be TYPE_LIST vars now, since list
 *      no longer requires its members to all be the same type.
 *      f_cb and f_ex could be in a union.
 *      f_minargs/f_maxargs/f_argc are all confusing, and I'm not
 *      sure I'm even still using maxargs
 */
struct function_handle_t {
        enum {
                FUNC_INTERNAL = 1,
                FUNC_USER,
        } f_magic;
        int f_minargs;
        int f_maxargs;
        struct var_t *(*f_cb)(struct vmframe_t *);
        struct executable_t *f_ex;
        struct var_t **f_argv;
        struct var_t **f_clov;
        int f_argc;
        int f_cloc;
        size_t f_arg_alloc;
        size_t f_clo_alloc;
};

/* @arr is either 'arg' or 'clo' */
#define GROW_ARG_ARRAY(fh, arr) \
        assert_array_pos((fh)->f_##arr##c + 1, \
                         (void **)&(fh)->f_##arr##v, \
                         &(fh)->f_##arr##_alloc, \
                         sizeof(struct var_t *))

static void
remove_args(struct var_t **arr, int count)
{
        int i;
        for (i = 0; i < count; i++) {
                /* these arrays could be sparse */
                if (arr[i])
                        VAR_DECR_REF(arr[i]);
        }
        free(arr);
}

static void
function_handle_reset(void *h)
{
        struct function_handle_t *fh = h;
        remove_args(fh->f_argv, fh->f_argc);
        remove_args(fh->f_clov, fh->f_cloc);
        if (fh->f_magic == FUNC_USER && fh->f_ex)
                EXECUTABLE_RELEASE(fh->f_ex);
}

static struct function_handle_t *
function_handle_new(void)
{
        return type_handle_new(sizeof(struct function_handle_t),
                               function_handle_reset);
}

/*
 * Helper to function_prep_frame
 * If @fn is a function, return that.
 * If @fn is a callable object, return the callable function, and
 *      update @owner accordingly.
 * If @fn is anything else, throw a syntax error.
 */
static struct var_t *
function_of(struct var_t *fn, struct var_t **owner)
{
        static char *callable = NULL;
        struct var_t *new_owner = *owner;

        /*
         * If @fn is a callable object, don't just get the first child.
         * __callable__ may be a function or another callable object.
         * Descend until we get to the function.
         */
        while (fn) {
                if (fn->magic == TYPE_FUNCTION) {
                        goto done;
                } else if (fn->magic == TYPE_DICT) {
                        if (!callable)
                                callable = literal_put("__callable__");

                        new_owner = fn;
                        fn = object_getattr(fn, callable);
                } else {
                        fn = NULL;
                }
        }
        err_setstr(RuntimeError, "Object is not callable");
        return NULL;

done:
        *owner = new_owner;
        return fn;
}

/*
 * return: If success: either @fn or the callable descendant of @fn to pass
 *         to call_function()
 *         If error or not callable: ErrorVar
 */
struct var_t *
function_prep_frame(struct var_t *fn,
                    struct vmframe_t *fr, struct var_t *owner)
{
        struct function_handle_t *fh;
        int i, argc;

        fn = function_of(fn, &owner);
        if (!fn)
                return ErrorVar;
        fh = fn->fn;
        bug_on(!fh);

        argc = (fh->f_magic == FUNC_INTERNAL)
               ? fh->f_minargs : fh->f_argc;
        for (i = fr->ap; i < argc; i++) {
                struct var_t *v;

                /*
                 * XXX shouldn't this be caught earlier?  f_minargs
                 * should never be larger than f_argc.  I put this
                 * trap here because if no optional args exist,
                 * f_argv/f_argc will never get initialized.  But it's
                 * sloppy...
                 */
                if (i > fh->f_argc)
                        goto er;
                v = fh->f_argv[i];
                if (!v)
                        goto er;

                fr->stack[fr->ap++] = v;
                VAR_INCR_REF(v);
        }
        fr->owner = owner;
        fr->func  = fn;
        fr->clo   = fh->f_clov;

        VAR_INCR_REF(owner);
        VAR_INCR_REF(fn);

        if (fh->f_magic == FUNC_USER)
                fr->ex = fh->f_ex;
        return fr->func;

er:
        err_setstr(RuntimeError, "Missing non-optional arg #%d", i + 1);
        return ErrorVar;
}

/**
 * call_function - Call a function if it's a builtin C function,
 *                 otherwise just finish setting up the frame
 *                 (started with function_prep_frame) and return
 * @fr: Frame to pass to execute_loop if it's a user-defined
 *      function.
 * @fn: Function to call.
 *
 * XXX: @fn is a field of @fr, do we need this extra stack variable
 *      for every recursion into call_function?
 *
 * Return:      ErrorVar if error encountered
 *              Return value of function otherwise
 */
struct var_t *
call_function(struct vmframe_t *fr, struct var_t *fn)
{
        struct function_handle_t *fh = fn->fn;

        bug_on(fn->magic != TYPE_FUNCTION);
        bug_on(!fh);
        bug_on(fh->f_magic != FUNC_INTERNAL && fh->f_magic != FUNC_USER);

        if (fh->f_magic == FUNC_INTERNAL) {
                bug_on(!fh->f_cb);
                return fh->f_cb(fr);
        } else {
                /* FUNC_USER */
                return execute_loop(fr);
        }
}

void
function_add_closure(struct var_t *func, struct var_t *clo)
{
        struct function_handle_t *fh = func->fn;
        bug_on(func->magic != TYPE_FUNCTION);
        bug_on(!fh);
        bug_on(fh->f_magic != FUNC_USER);

        if (GROW_ARG_ARRAY(fh, clo) < 0)
                fail("OOM");
        fh->f_clov[fh->f_cloc] = clo;
        fh->f_cloc++;
}

void
function_add_default(struct var_t *func,
                        struct var_t *deflt, int argno)
{
        struct function_handle_t *fh = func->fn;
        size_t needsize;
        bug_on(func->magic != TYPE_FUNCTION);
        bug_on(!fh);
        bug_on(fh->f_magic != FUNC_USER);
        bug_on(argno < 0);

        /*
         * Do this manually, since not every impl. of realloc
         * zeros the array, and NULL is meaningful here.
         *
         * FIXME: non-sparse array for usu. sparse fields,
         * not very mem. efficient.
         */
        needsize = (argno + 1) * sizeof(void *);
        if (!fh->f_argv) {
                fh->f_argv = ecalloc(needsize);
                fh->f_arg_alloc = needsize;
        } else if (fh->f_arg_alloc < needsize) {
                struct var_t **new_arr;
                size_t new_alloc = fh->f_arg_alloc;
                while (new_alloc < needsize)
                        new_alloc *= 2;
                new_arr = ecalloc(new_alloc);
                memcpy(new_arr, fh->f_argv,
                       fh->f_argc * sizeof(void *));
                free(fh->f_argv);
                fh->f_argv = new_arr;
                fh->f_arg_alloc = new_alloc;
        }
        fh->f_argv[argno] = deflt;
        fh->f_argc = argno + 1;
}

/**
 * funcvar_new_intl - create a builtin function var
 * @cb: Callback that executes the function.  It may pass the vmframe_t
 *      to vm_get_this and vm_get_arg to retrieve its "this" and
 *      arguments.  It must return ErrorVar if it encountered an error,
 *      or some other struct var_t return value.  If there's nothing
 *      to return, return NULL; this saves us the double-task of
 *      creating and destroying a return value that won't be used;
 *      the wrapping function will convert it to a TYPE_EMPTY for
 *      callers that must receive a return value.
 * @minargs: Minimum number of args used by the function
 * @maxargs: Maximum number of args used by the function
 */
struct var_t *
funcvar_new_intl(struct var_t *(*cb)(struct vmframe_t *),
                 int minargs, int maxargs)
{
        struct var_t *func = var_new();
        struct function_handle_t *fh;

        fh = function_handle_new();
        fh->f_magic = FUNC_INTERNAL;
        fh->f_cb = cb;
        fh->f_minargs = minargs;
        fh->f_maxargs = maxargs;
        func->fn = fh;
        func->magic = TYPE_FUNCTION;
        return func;
}

/**
 * funcvar_new_user - create a user function var
 * @ex:         Executable code to assign to function
 */
struct var_t *
funcvar_new_user(struct executable_t *ex)
{
        struct var_t *func = var_new();
        struct function_handle_t *fh;

        fh = function_handle_new();
        fh->f_magic = FUNC_USER;
        fh->f_ex = ex;
        EXECUTABLE_CLAIM(ex);

        func->magic = TYPE_FUNCTION;
        func->fn = fh;
        return func;
}

static int
func_cmp(struct var_t *a, struct var_t *b)
{
        if (b->magic != TYPE_FUNCTION || b->fn != a->fn)
                return -1;
        return 0;
}

static bool
func_cmpz(struct var_t *func)
{
        return false;
}

static struct var_t *
func_cp(struct var_t *v)
{
        struct var_t *ret = var_new();
        ret->fn = v->fn;
        TYPE_HANDLE_INCR_REF(ret->fn);
        ret->magic = TYPE_FUNCTION;
        return ret;
}

static void
func_reset(struct var_t *func)
{
        TYPE_HANDLE_DECR_REF(func->fn);
        func->fn = NULL;
}

static const struct operator_methods_t function_primitives = {
        .cmp = func_cmp,
        .cmpz = func_cmpz,
        .reset = func_reset,
        .cp = func_cp,
};

void
typedefinit_function(void)
{
        var_config_type(TYPE_FUNCTION,
                        "function",
                        &function_primitives,
                        /*
                         * TODO: function methods, like
                         *      strbuf = myfunc.disassemble();
                         */
                        NULL);
}


