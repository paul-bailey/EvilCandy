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
 *              function
 * @f_maxargs:  Maximum number of args that may be passed to the
 *              function, or -1 if no maximum (cf. the string
 *              format built-in method)
 * @f_cb:       If @magic is FUNC_INTERNAL, pointer to the builtin
 *              callback
 * @f_mk:       If @magic is FUNC_USER, pointer to the user callback
 */
struct function_handle_t {
        int nref;
        enum {
                FUNC_INTERNAL = 1,
                FUNC_USER,
                FUNC_LAMBDA,
        } f_magic;
        int f_minargs;
        int f_maxargs;
        void (*f_cb)(struct var_t *ret);
        struct marker_t f_mk;
        struct list_t f_args;
        struct list_t f_closures;
};

struct function_arg_t {
        char *a_name;
        struct list_t a_list;
        struct var_t *a_default;
};

#define list2arg(li) container_of(li, struct function_arg_t, a_list)

static struct function_handle_t *
function_handle_new(void)
{
        struct function_handle_t *ret = ecalloc(sizeof(*ret));
        list_init(&ret->f_args);
        list_init(&ret->f_closures);
        ret->nref = 1;
        return ret;
}

static void
remove_args(struct list_t *which)
{
        struct list_t *li, *tmp;
        list_foreach_safe(li, tmp, which) {
                struct function_arg_t *a = list2arg(li);
                if (a->a_default)
                        var_delete(a->a_default);
                list_remove(&a->a_list);
                free(a);
        }
}

static void
function_handle_reset(struct function_handle_t *fh)
{
        remove_args(&fh->f_args);
        remove_args(&fh->f_closures);
        free(fh);
}

static void
push_copy_of(struct var_t *v)
{
        struct var_t *cp = var_new();
        qop_mov(cp, v);
        stack_push(cp);
}

/* push @owner...or something...onto the stack */
static void
push_owner(struct var_t *fn, struct var_t *owner)
{
        if (!owner)
                owner = get_this();
        bug_on(!owner);
        push_copy_of(owner);
}

/* always returns NULL for FUNC_INTERNAL */
static struct function_arg_t *
arg_first_entry(struct function_handle_t *fh)
{
        struct list_t *list = fh->f_args.next;
        return list == &fh->f_args ? NULL : list2arg(list);
}

static struct function_arg_t *
arg_next_entry(struct function_handle_t *fh, struct function_arg_t *arg)
{
        struct list_t *list = arg->a_list.next;
        return list == &fh->f_args ? NULL : list2arg(list);
}

static int frame_stack[CALL_DEPTH_MAX];
static int call_depth_fp = 0;

static void
frame_push(int new_fp)
{
        if (call_depth_fp >= CALL_DEPTH_MAX)
                syntax("Function calls nested too deep");
        frame_stack[call_depth_fp] = q_.fp;
        q_.fp = new_fp;
        call_depth_fp++;
}

static void
frame_pop(void)
{
        call_depth_fp--;
        bug_on(call_depth_fp < 0);
        q_.fp = frame_stack[call_depth_fp];
}

/*
 * Stack order after call is:
 *
 *      owner object handle     <-- FP
 *      current function
 *      arg1
 *      ...
 *      argN
 *                              <-- SP
 * (using the convention of a "descending" stack pointer)
 *
 * Return: old FP
 */
static void
push_uargs(struct var_t *fn, struct var_t *owner)
{
        int new_fp;
        struct function_arg_t *arg = NULL;
        struct function_handle_t *fh = fn->fn;

        /*
         * can't change this yet because we need old frame pointer
         * while evaluating args.
         */
        new_fp = q_.sp;
        push_owner(fn, owner);

        push_copy_of(fn);

        qlex();
        expect(OC_LPAR);

        qlex();
        if (cur_oc->t != OC_RPAR) {
                q_unlex();
                arg = arg_first_entry(fh);
                do {
                        struct var_t *v = var_new();
                        eval(v);
                        qlex();
                        if (arg) {
                                v->name = arg->a_name;
                                arg = arg_next_entry(fh, arg);
                        }
                        stack_push(v);
                } while (cur_oc->t == OC_COMMA);
                expect(OC_RPAR);

                /*
                 * Caller didn't send all the args.  If all remaining
                 * args are optional, use their defaults.  Otherwise
                 * throw an error.
                 */
                while (arg) {
                        struct var_t *v;
                        if (!arg->a_default) {
                                syntax("Mandatory argument missing",
                                        arg->a_name);
                        }
                        v = var_new();
                        v->name = arg->a_name;
                        qop_mov(v, arg->a_default);
                        stack_push(v);
                        arg = arg_next_entry(fh, arg);
                }
        }
        frame_push(new_fp);
}

/*
 * internal args were set up by an internal function
 * Set the stack up with these rather than parsing PC,
 * otherwise same stack order as with push_uargs.
 */
static void
push_iargs(struct var_t *fn, struct var_t *owner,
           int argc, struct var_t *argv[])
{
        int new_fp;
        struct function_arg_t *arg = NULL;
        struct function_handle_t *fh = fn->fn;
        int i;

        new_fp = q_.sp;
        push_owner(fn, owner);
        push_copy_of(fn);

        arg = arg_first_entry(fh);
        for (i = 0; i < argc; i++) {
                struct var_t *v = var_new();
                qop_mov(v, argv[i]);
                if (arg) {
                        v->name = arg->a_name;
                        arg = arg_next_entry(fh, arg);
                }
                stack_push(v);
        }
        while (arg) {
                struct var_t *v;
                if (!arg->a_default)
                        syntax("User requiring more arguments than builtin method promises");
                v = var_new();
                v->name = arg->a_name;
                qop_mov(v, arg->a_default);
                stack_push(v);
                arg = arg_next_entry(fh, arg);
        }

        frame_push(new_fp);
}

/* Call an internal built-in function */
static void
ifunction_helper(struct var_t *fn, struct var_t *retval)
{
        /* Internal function, we don't touch LR or PC for this */
        int nargs;
        struct function_handle_t *fh = fn->fn;
        bug_on(!fh);
        bug_on(!fh->f_cb);
        if ((nargs = arg_count()) != fh->f_minargs) {
                if (nargs < fh->f_minargs ||
                    (fh->f_maxargs > 0 &&
                     nargs > fh->f_maxargs)) {
                        syntax("Expected %d args but got %d",
                                fh->f_minargs, nargs);
                }
        }
        fh->f_cb(retval);
}

static struct marker_t lrstack[CALL_DEPTH_MAX];
/*
 * this can't be same as call_depth_fp because we do not
 * always call user functions.
 */
static int call_depth_lr = 0;

static void
lrpush(struct function_handle_t *fh)
{
        if (call_depth_lr >= CALL_DEPTH_MAX)
                syntax("Function calls nested too deeply");
        PC_BL(&fh->f_mk, &lrstack[call_depth_lr]);
        call_depth_lr++;
}

static void
lrpop(void)
{
        call_depth_lr--;
        bug_on(call_depth_lr < 0);
        PC_GOTO(&lrstack[call_depth_lr]);
}

/* Call a user-defined function */
static void
ufunction_helper(struct var_t *fn, struct var_t *retval)
{
        int exres;

        bug_on(!fn->fn);

        /* Return address is just after ')' of function call */
        lrpush(fn->fn);

        if (fn->fn->f_magic == FUNC_LAMBDA) {
                /* lambda */
                int t;
                qlex();
                t = cur_oc->t;
                q_unlex();
                if (t != OC_LBRACE) {
                        eval(retval);
                        goto done;
                }
                /* fall through */
        }

        /* FUNC_USER or lambda with braces */
        exres = expression(retval, 0);
        if (exres != 1 && exres != 0) {
                syntax("Unexpected %s", exres == 2 ? "break" : "EOF");
        }

done:
        /* restore PC */
        lrpop();
}

static void
call_function_common(struct var_t *fn, struct var_t *retval,
                     struct var_t *owner)
{
        struct var_t *retval_save = retval;

        /*
         * This guarantees that callbacks' retval may always be
         * de-referenced
         */
        if (!retval_save)
                retval = var_new();

        if (fn->fn->f_magic == FUNC_INTERNAL) {
                ifunction_helper(fn, retval);
        } else {
                bug_on(fn->fn->f_magic != FUNC_USER
                        && fn->fn->f_magic != FUNC_LAMBDA);
                ufunction_helper(fn, retval);
        }

        if (!retval_save)
                var_delete(retval);

        /* Unwind stack to beginning of args */
        stack_unwind_to_frame();
        frame_pop();
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
        syntax("Value is not callable", nameof(fn));
done:
        *owner = new_owner;
        return fn;
}

/**
 * call_function - Call a function from user code and execute it
 * @fn:         Function handle, which must be callalbe or a syntax error
 *              will be thrown.
 * @retval:     Return value of the function being called, or NULL to
 *              ignore
 * @owner:      Owner of the function, or NULL to have call_function
 *              figure it out.  If @fn is a callable object, @owner will
 *              be ignored.
 *
 * PC must be at the opening parenthesis of the code calling the function
 * (immediately after the invocation of the function name).
 */
void
call_function(struct var_t *fn, struct var_t *retval, struct var_t *owner)
{
        fn = function_of(fn, &owner);
        push_uargs(fn, owner);
        call_function_common(fn, retval, owner);
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
        fn = function_of(fn, &owner);
        push_iargs(fn, owner, argc, argv);
        call_function_common(fn, retval, owner);
}

/**
 * function_set_user - Set empty function to be user-defined
 * @func: Function initialized with function_init
 * @pc: Location in script of first '(' of argument list.
 * @lambda: If true, this used lambda notation.
 */
void
function_set_user(struct var_t *func, const struct marker_t *pc, bool lambda)
{
        struct function_handle_t *fh = func->fn;
        bug_on(func->magic != QFUNCTION_MAGIC);
        bug_on(!fh);
        bug_on(fh->f_magic != 0);

        fh->f_magic = lambda ? FUNC_LAMBDA : FUNC_USER;
        memcpy(&fh->f_mk, pc, sizeof(fh->f_mk));
}

/**
 * function_init - precursor to function_set_user
 * @func: Empty variable to configure as a function
 *
 * This and function_set_user would just be a single function call
 * named function_init_user to parallel function_init_internal,
 * but the pc is not yet determined at function_add_arg time.
 */
void
function_init(struct var_t *func)
{
        struct function_handle_t *fh;
        bug_on(func->magic != QEMPTY_MAGIC);

        fh = function_handle_new();
        fh->f_magic = 0;
        func->fn = fh;
        func->magic = QFUNCTION_MAGIC;
}

/*
 * helper to function_add_arg|closure().
 * @parent is either f_args or f_closures
 */
static void
new_arg_or_closure(struct list_t *parent, char *name, struct var_t *deflt)
{
        struct function_arg_t *arg = emalloc(sizeof(*arg));
        arg->a_default = deflt;
        arg->a_name = name;
        list_init(&arg->a_list);
        list_add_tail(&arg->a_list, parent);
}

/**
 * function_add_arg - Add user arg to a user-defined function
 * @func:  A user-defined function
 * @name:  Name that will be used by the function for the argument,
 *         must be from an opcode or a return value of literal()
 * @deflt: Default value to set the argument to if caller does not
 *         provide it.  If this is NULL, the argument will be treated
 *         as mandatory.  Otherwise, @deflt should have been allocated
 *         with var_new; function_add_arg() takes this directly, it
 *         does not copy it.
 *
 * The order of arguments will be the same as the order of calls to this
 * function while compiling @func
 */
void
function_add_arg(struct var_t *func, char *name, struct var_t *deflt)
{
        struct function_handle_t *fh = func->fn;
        struct function_arg_t *arg = emalloc(sizeof(*arg));

        bug_on(func->magic != QFUNCTION_MAGIC);
        bug_on(!fh);
        bug_on(fh->f_magic == FUNC_INTERNAL);

        if (deflt)
                deflt->name = name;

        new_arg_or_closure(&fh->f_args, name, deflt);
}

/**
 * function_add_closure - Add closure to a user-defined function
 * @func:       A user-defined function
 * @name:       Name that will be userd by the function for the closure,
 *              must be from an opcode or a return value of literal()
 * @init:       Initialization value. This may not be NULL.
 *              It must have been allocated with var_new();
 *              function_add_closure takes it directly, it does not
 *              copy it.
 */
void
function_add_closure(struct var_t *func, char *name, struct var_t *init)
{
        struct function_handle_t *fh = func->fn;
        struct function_arg_t *clo = emalloc(sizeof(*clo));

        bug_on(func->magic != QFUNCTION_MAGIC);
        bug_on(!fh);
        bug_on(fh->f_magic == FUNC_INTERNAL);

        init->name = name;
        new_arg_or_closure(&fh->f_closures, name, init);
}

/**
 * function_seek_closure - Get closure from function
 * @func: Function to seek, usu. get_this_function()
 * @name: Name of the closure
 *
 * Return: Closure matching @name, or NULL if not found.
 */
struct var_t *
function_seek_closure(struct var_t *func, const char *name)
{
        struct list_t *li;

        bug_on(func->magic != QFUNCTION_MAGIC);
        bug_on(!func->fn);
        /*
         * XXX: I'm assuming this will never be more
         * than a half-dozen or so, so I'm using the
         * same linked-list version as the arguments.
         */
        list_foreach(li, &func->fn->f_closures) {
                struct function_arg_t *clo = list2arg(li);
                bug_on(!clo->a_default);
                if (clo->a_default->name == (char *)name)
                        return clo->a_default;
        }
        return NULL;
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

