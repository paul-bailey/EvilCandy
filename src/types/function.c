/*
 * function.c - FunctionType stuff
 *
 * function_prep_frame and call_function are part of same function
 * call (see vm_exec_func).
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
 *              function, if internal function.
 * @f_maxargs:  Maximum number of args that may be passed to the
 *              function (if it's FUNC_INTERNAL), or -1 if no maximum
 * @f_cb:       If @magic is FUNC_INTERNAL, pointer to the builtin
 *              callback
 * @f_ex:       If @magic is FUNC_USER, pointer to code to execute
 * @f_closures: If @magic is FUNC_USER, ArrayType object containing
 *              closures.
 * @f_defaults: If @magic is FUNC_USER, ArrayType object containing
 *              defaults to fill in where arguments are not provided by
 *              the caller.  Blank slots mean the argument is mandatory.
 */
struct funcvar_t {
        Object base;
        enum {
                FUNC_INTERNAL = 1,
                FUNC_USER,
        } f_magic;
        int f_minargs;
        int f_maxargs;
        union {
                /* FUNC_INTERNAL exclusives */
                Object *(*f_cb)(Frame *);

                /* FUNC_USER exclusives */
                struct {
                        struct xptrvar_t *f_ex;
                        Object *f_closures;
                        Object *f_defaults;
                };
        };
};

#define V2FUNC(v)       ((struct funcvar_t *)(v))

/*
 * Helper to function_prep_frame
 * If @fn is a function, return that.
 * If @fn is a callable object, return the callable function, and
 *      update @owner accordingly.
 * If @fn is anything else, return NULL and report an error.
 */
static Object *
function_of(Object *fn, Object **owner)
{
        static Object *callable_key = NULL;
        Object *new_owner = *owner;

        /*
         * If @fn is a callable object, don't just get the first child.
         * __callable__ may be a function or another callable object.
         * Descend until we get to the function.
         */
        while (fn) {
                if (isvar_function(fn)) {
                        goto done;
                } else if (isvar_dict(fn)) {
                        if (!callable_key)
                                callable_key = stringvar_new("__callable__");

                        new_owner = fn;
                        fn = dict_getitem(fn, callable_key);
                } else {
                        fn = NULL;
                }
        }
        err_setstr(TypeError, "Object is not callable");
        return NULL;

done:
        *owner = new_owner;
        return fn;
}

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

/*
 * return: If success: either @fn or the callable descendant of @fn to pass
 *         to call_function()
 *         If error or not callable: ErrorVar
 */
Object *
function_prep_frame(Object *fn,
                    Frame *fr, Object *owner)
{
        struct funcvar_t *fh;

        fn = function_of(fn, &owner);
        if (!fn)
                return ErrorVar;

        fh = V2FUNC(fn);

        if (function_argc_check(fh, fr->ap) != RES_OK)
                return ErrorVar;

        if (fh->f_magic == FUNC_USER && fh->f_defaults) {
                /* Fill in default args. */
                int i, n = seqvar_size(fh->f_defaults);
                for (i = fr->ap; i < n; i++) {
                        Object *v = array_getitem(fh->f_defaults, i);
                        bug_on(v == NULL || v == ErrorVar);
                        fr->stack[fr->ap++] = v;
                }
        }

        fr->owner = owner;
        fr->func  = fn;
        fr->clo   = fh->f_closures ?
                        array_get_data(fh->f_closures) : NULL;

        VAR_INCR_REF(owner);
        VAR_INCR_REF(fn);

        if (fh->f_magic == FUNC_USER)
                fr->ex = fh->f_ex;
        return fr->func;
}

/**
 * call_function - Call a function
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
Object *
call_function(Frame *fr, Object *fn)
{
        struct funcvar_t *fh = V2FUNC(fn);

        bug_on(!isvar_function(fn));
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
function_add_closure(Object *func, Object *clo)
{
        struct funcvar_t *fh = V2FUNC(func);

        bug_on(!isvar_function(func));
        bug_on(fh->f_magic != FUNC_USER);

        if (!fh->f_closures)
                fh->f_closures = arrayvar_new(0);
        array_append(fh->f_closures, clo);
}

/**
 * function_add_default - Add a default argument to a function
 * @func: Function to add default to
 * @deflt: Default value
 * @argno: Argument's place in function calls, indexed from zero.
 */
void
function_add_default(Object *func,
                     Object *deflt, int argno)
{
        struct funcvar_t *fh = V2FUNC(func);

        bug_on(!isvar_function(func));
        bug_on(!fh);
        bug_on(fh->f_magic != FUNC_USER);
        bug_on(argno < 0);

        /*
         * Fill in mandatory slots with 'this is not a default'.  Array
         * is filled to size with NullVar upon creation.  Since NullVar
         * could be interpreted as a valid user-default, replace it with
         * ErrorVar instead.
         */
        if (!fh->f_defaults) {
                int i;
                fh->f_defaults = arrayvar_new(argno);
                for (i = 0; i < argno; i++)
                        array_setitem(fh->f_defaults, i, ErrorVar);
        } else while (seqvar_size(fh->f_defaults) < argno) {
                array_append(fh->f_defaults, ErrorVar);
        }

        array_append(fh->f_defaults, deflt);
        bug_on(seqvar_size(fh->f_defaults) != argno + 1);
}

static Object *
funcvar_alloc(int magic)
{
        Object *func = var_new(&FunctionType);
        struct funcvar_t *fh = V2FUNC(func);
        fh->f_minargs = 0;
        fh->f_maxargs = -1;
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
        default:
                err_setstr(TypeError,
                           "Type funcion does not have enumerated attribute %d",
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
        fh->f_defaults = NULL;
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
                         "<function (user) at '%s'>", f->f_ex->uuid);
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
                if (fh->f_defaults)
                        VAR_DECR_REF(fh->f_defaults);
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

