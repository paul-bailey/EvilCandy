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
 * @nref:       Number of vars with access to this handle
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
 */
struct function_handle_t {
        int nref;
        enum {
                FUNC_INTERNAL = 1,
                FUNC_USER,
        } f_magic;
        int f_minargs;
        int f_maxargs;
        void (*f_cb)(struct var_t *ret);
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

static struct function_handle_t *
function_handle_new(void)
{
        struct function_handle_t *ret = ecalloc(sizeof(*ret));
        ret->nref = 1;
        return ret;
}

static void
remove_args(struct var_t **arr, int count)
{
        int i;
        for (i = 0; i < count; i++)
                var_delete(arr[i]);
        free(arr);
}

static void
function_handle_reset(struct function_handle_t *fh)
{
        remove_args(fh->f_argv, fh->f_argc);
        remove_args(fh->f_clov, fh->f_cloc);
        free(fh);
}

/*
 * Helper to call_function and call_function_from_intl
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
                if (fn->magic == QFUNCTION_MAGIC) {
                        goto done;
                } else if (fn->magic == QOBJECT_MAGIC) {
                        if (!callable)
                                callable = literal_put("__callable__");

                        new_owner = fn;
                        fn = object_child_l(fn, callable);
                } else {
                        fn = NULL;
                }
        }
        syntax("Value is not callable");
done:
        *owner = new_owner;
        return fn;
}

/*
 * return: either @fn or the callable descendant of @fn to pass
 *         to call_vmfunction
 */
struct var_t *
call_vmfunction_prep_frame(struct var_t *fn,
                           struct vmframe_t *fr, struct var_t *owner)
{
        struct function_handle_t *fh;
        int i, argc;

        fn = function_of(fn, &owner);
        fh = fn->fn;
        bug_on(!fh);

        argc = (fh->f_magic == FUNC_INTERNAL)
               ? fh->f_minargs : fh->f_argc;
        for (i = fr->ap; i < argc; i++) {
                struct var_t *v = fh->f_argv[i];
                if (!v)
                        syntax("Missing non-optional arg #%d", i);
                fr->stack[fr->ap++] = qop_mov(var_new(), v);
        }
        if (!owner)
                owner = get_this();
        fr->owner = qop_mov(var_new(), owner);
        fr->func  = qop_mov(var_new(), fn);
        fr->clo   = fh->f_clov;

        if (fh->f_magic == FUNC_USER)
                fr->ex = fh->f_ex;
        return fr->func;
}

/*
 * return function result if INTERNAL, NULL if VM (let vm.c code
 * execute it)
 */
struct var_t *
call_vmfunction(struct var_t *fn)
{
        struct var_t *ret = NULL;
        struct function_handle_t *fh = fn->fn;
        bug_on(fn->magic != QFUNCTION_MAGIC);
        bug_on(!fh);
        switch (fh->f_magic) {
        case FUNC_INTERNAL:
                ret = var_new();
                bug_on(!fh->f_cb);
                fh->f_cb(ret);
                break;
        case FUNC_USER:
                break;
        default:
                bug();
                break;
        }
        return ret;
}

/**
 * call_function_from_intl - Call a function (user or internal) from
 *                           within an internal built-in function.
 * @fn:         Function handle, which must be type QPTRXI_MAGIC or
 *              QFUNCTION_MAGIC.
 * @retval:     Return value of the function being called, or NULL to
 *              ignore
 * @owner:      Owner of the function, or NULL to have
 *              call_function_from_intl figure it out
 * @argc:       Number of args
 * @argv:       Arg array
 *
 * This is for things like an object or array's .foreach method, where
 * "foreach" is a built-in function in C code, but it will call a function
 * from the "foreach" argument.
 */
void
call_function_from_intl(struct var_t *fn, struct var_t *retval,
                        struct var_t *owner, int argc, struct var_t *argv[])
{
        syntax("Cannot currently support callbacks in VM mode");
}

void
function_vmadd_closure(struct var_t *func, struct var_t *clo)
{
        struct function_handle_t *fh = func->fn;
        bug_on(func->magic != QFUNCTION_MAGIC);
        bug_on(!fh);
        bug_on(fh->f_magic != FUNC_USER);

        if (GROW_ARG_ARRAY(fh, clo) < 0)
                fail("OOM");
        fh->f_clov[fh->f_cloc] = clo;
        fh->f_cloc++;
}

void
function_vmadd_default(struct var_t *func,
                        struct var_t *deflt, int argno)
{
        struct function_handle_t *fh = func->fn;
        size_t needsize;
        bug_on(func->magic != QFUNCTION_MAGIC);
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
 * function_init_internal - Initialize @func to be a callable
 *                          builtin function
 * @func: Empty variable to configure
 * @cb: Callback that executes the function
 * @minargs: Minimum number of args used by the function
 * @maxargs: Maximum number of args used by the function
 */
void
function_init_internal(struct var_t *func, void (*cb)(struct var_t *),
                       int minargs, int maxargs)
{
        struct function_handle_t *fh;
        bug_on(func->magic != QEMPTY_MAGIC);

        fh = function_handle_new();
        fh->f_magic = FUNC_INTERNAL;
        fh->f_cb = cb;
        fh->f_minargs = minargs;
        fh->f_maxargs = maxargs;
        func->fn = fh;
        func->magic = QFUNCTION_MAGIC;
}

void
function_init_vm(struct var_t *func, struct executable_t *ex)
{
        struct function_handle_t *fh;
        bug_on(func->magic != QEMPTY_MAGIC);

        fh = function_handle_new();
        fh->f_magic = FUNC_USER;
        fh->f_ex = ex;

        func->magic = QFUNCTION_MAGIC;
        func->fn = fh;
}


static bool
func_cmpz(struct var_t *func)
{
        return false;
}

static void
func_mov(struct var_t *to, struct var_t *from)
{
        if (from->magic != QFUNCTION_MAGIC ||
            (to->magic != QEMPTY_MAGIC &&
             to->magic != QFUNCTION_MAGIC)) {
                syntax("Mov operation not permitted for this type");
        }
        bug_on(!from->fn);
        to->fn = from->fn;
        to->fn->nref++;
}

static void
func_reset(struct var_t *func)
{
        --func->fn->nref;
        if (func->fn->nref <= 0) {
                bug_on(func->fn->nref < 0);
                function_handle_reset(func->fn);
                func->fn = NULL;
        }
}

static const struct operator_methods_t function_primitives = {
        .cmpz = func_cmpz,
        .mov  = func_mov,
        .reset = func_reset,
};

void
typedefinit_function(void)
{
        var_config_type(QFUNCTION_MAGIC,
                        "function",
                        &function_primitives,
                        NULL);
}


