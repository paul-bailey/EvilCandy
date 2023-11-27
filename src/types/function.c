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
        } f_magic;
        int f_minargs;
        int f_maxargs;
        void (*f_cb)(struct var_t *ret);
        struct marker_t f_mk;
        struct list_t f_args;
};

struct function_arg_t {
        struct list_t a_list;
        char *a_name;
        struct var_t *a_default;
};

#define list2arg(li) container_of(li, struct function_arg_t, a_list)

static struct function_handle_t *
function_handle_new(void)
{
        struct function_handle_t *ret = ecalloc(sizeof(*ret));
        list_init(&ret->f_args);
        ret->nref = 1;
        return ret;
}

static void
function_handle_reset(struct function_handle_t *fh)
{
        struct list_t *li, *tmp;
        list_foreach_safe(li, tmp, &fh->f_args) {
                struct function_arg_t *a = list2arg(li);
                if (a->a_default)
                        var_delete(a->a_default);
                list_remove(&a->a_list);
                free(a);
        }
        free(fh);
}

/* push @owner...or something...onto the stack */
static void
push_owner(struct var_t *fn, struct var_t *owner)
{
        if (!owner)
                owner = get_this();
        bug_on(!owner);
        stack_push(owner);
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

/*
 * Stack order after call is:
 *
 *      owner object handle     <-- FP
 *      arg1
 *      ...
 *      argN
 *                              <-- SP
 * (using the convention of a "descending" stack pointer)
 *
 * Return: old FP
 */
static struct var_t *
push_uargs(struct var_t *fn, struct var_t *owner)
{
        struct var_t *fpsav, *new_fp;
        struct function_arg_t *arg = NULL;
        struct function_handle_t *fh = fn->fn;

        /*
         * can't change this yet because we need old frame pointer
         * while evaluating args.
         */
        new_fp = q_.sp;
        push_owner(fn, owner);

        qlex();
        expect(OC_LPAR);

        qlex();
        if (cur_oc->t != OC_RPAR) {
                q_unlex();
                arg = arg_first_entry(fh);
                do {
                        struct var_t *v = stack_getpush();
                        eval(v);
                        qlex();
                        if (arg) {
                                v->name = arg->a_name;
                                arg = arg_next_entry(fh, arg);
                        }
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
                                syntax("Mandatory argument %s missing",
                                        arg->a_name);
                        }
                        v = stack_getpush();
                        v->name = arg->a_name;
                        qop_mov(v, arg->a_default);
                        arg = arg_next_entry(fh, arg);
                }
        }

        fpsav = q_.fp;
        q_.fp = new_fp;
        return fpsav;
}

/*
 * internal args were set up by an internal function
 * Set the stack up with these rather than parsing PC,
 * otherwise same stack order as with push_uargs.
 */
static struct var_t *
push_iargs(struct var_t *fn, struct var_t *owner,
           int argc, struct var_t *argv[])
{
        struct var_t *fpsav, *new_fp;
        struct function_arg_t *arg = NULL;
        struct function_handle_t *fh = fn->fn;
        int i;

        new_fp = q_.sp;
        push_owner(fn, owner);

        arg = arg_first_entry(fh);
        for (i = 0; i < argc; i++) {
                struct var_t *v = stack_getpush();
                qop_mov(v, argv[i]);
                if (arg) {
                        v->name = arg->a_name;
                        arg = arg_next_entry(fh, arg);
                }
        }
        while (arg) {
                struct var_t *v;
                if (!arg->a_default)
                        syntax("User requiring more arguments than builtin method promises");
                v = stack_getpush();
                v->name = arg->a_name;
                qop_mov(v, arg->a_default);
                arg = arg_next_entry(fh, arg);
        }

        fpsav = q_.fp;
        q_.fp = new_fp;
        return fpsav;
}

/* Unwind the stack, restore old link register, and restore old fp */
static void
pop_args(struct var_t *fpsav)
{
        /* Unwind stack to beginning of args */
        while (q_.sp != q_.fp)
                stack_pop(NULL);

        /* restore FP */
        q_.fp = fpsav;
}

/* Assumes stack is already set up. */
static inline int n_args(void) { return q_.sp - 1 - q_.fp; }

/* Call an internal built-in function */
static void
ifunction_helper(struct var_t *fn, struct var_t *retval)
{
        /* Internal function, we don't touch LR or PC for this */
        int nargs;
        struct function_handle_t *fh = fn->fn;
        bug_on(!fh);
        bug_on(!fh->f_cb);
        if ((nargs = n_args()) != fh->f_minargs) {
                if (nargs < fh->f_minargs ||
                    (fh->f_maxargs > 0 &&
                     nargs > fh->f_maxargs)) {
                        syntax("Expected %d args but got %d",
                                fh->f_minargs, nargs);
                }
        }
        fh->f_cb(retval);
}

/* Call a user-defined function */
static void
ufunction_helper(struct var_t *fn, struct var_t *retval)
{
        int exres;
        struct function_handle_t *fh = fn->fn;
        struct marker_t lr; /* virtual link register */

        bug_on(!fh);

        /* Return address is just after ')' of function call */
        PC_BL(&fh->f_mk, &lr);

        /* need to peek */
        qlex();
        expect(OC_LBRACE);
        q_unlex();

        /* execute it */
        exres = expression(retval, 0);
        if (exres != 1 && exres != 0) {
                syntax("Unexpected %s", exres == 2 ? "break" : "EOF");
        }

        /* restore PC */
        PC_GOTO(&lr);
}

static void
call_function_common(struct var_t *fn, struct var_t *retval,
                     struct var_t *owner, struct var_t *fpsav)
{
        struct var_t *retval_save = retval;

        /*
         * This guarantees that callbacks' retval may always be
         * de-referenced
         */
        if (!retval_save)
                retval = tstack_getpush();

        if (fn->fn->f_magic == FUNC_INTERNAL) {
                ifunction_helper(fn, retval);
        } else {
                bug_on(fn->fn->f_magic != FUNC_USER);
                ufunction_helper(fn, retval);
        }

        if (!retval_save)
                tstack_pop(NULL);

        pop_args(fpsav);
}

/**
 * call_function - Call a function from user code and execute it
 * @fn:         Function handle, which must be type QPTRXI_MAGIC or
 *              QFUNCTION_MAGIC.
 * @retval:     Return value of the function being called, or NULL to
 *              ignore
 * @owner:      Owner of the function, or NULL to have call_function
 *              figure it out
 *
 * PC must be at the opening parenthesis of the code calling the function
 * (immediately after the invocation of the function name).
 */
void
call_function(struct var_t *fn, struct var_t *retval, struct var_t *owner)
{
        bug_on(fn->magic != QFUNCTION_MAGIC);
        struct var_t *fpsav = push_uargs(fn, owner);
        call_function_common(fn, retval, owner, fpsav);
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
        bug_on(fn->magic != QFUNCTION_MAGIC);
        struct var_t *fpsav = push_iargs(fn, owner, argc, argv);
        call_function_common(fn, retval, owner, fpsav);
}

/**
 * function_set_user - Set empty function to be user-defined
 * @func: Function initialized with function_init
 * @pc: Location in script of first '(' of argument list.
 */
void
function_set_user(struct var_t *func, const struct marker_t *pc)
{
        struct function_handle_t *fh = func->fn;
        bug_on(func->magic != QFUNCTION_MAGIC);
        bug_on(!fh);
        bug_on(fh->f_magic != 0);

        fh->f_magic = FUNC_USER;
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

/**
 * function_add_arg - Add user arg to a user-defined function
 * @func:  A user-defined function
 * @name:  Name that will be used by the function for the argument,
 *         must be from an opcode or a return value of literal()
 * @deflt: Default value to set the argument to if caller does not
 *         provide it.  If this is NULL, the argument will be treated
 *         as mandatory.
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

        arg->a_name = name;
        arg->a_default = deflt;
        list_init(&arg->a_list);
        list_add_tail(&arg->a_list, &fh->f_args);
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

