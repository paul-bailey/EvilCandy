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
 * @minargs:    Minimum number of args that may be passed to the
 *              function
 * @maxargs:    Maximum number of args that may be passed to the
 *              function, or -1 if no maximum (cf. the string
 *              format built-in method)
 * @fni:        If @magic is FUNC_INTERNAL, pointer to the builtin
 *              callback
 * @mk:         If @magic is FUNC_USER, pointer to the user callback
 */
struct function_handle_t {
        int nref;
        enum {
                FUNC_INTERNAL = 1,
                FUNC_USER,
        } f_magic;
        int minargs;
        int maxargs;
        void (*fni)(struct var_t *ret);
        struct marker_t mk;
};

static struct function_handle_t *
function_handle_new(void)
{
        struct function_handle_t *ret = ecalloc(sizeof(*ret));
        ret->nref = 1;
        return ret;
}

static void
function_handle_reset(struct function_handle_t *fh)
{
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
 *
 * TODO: Make an internal-pointer variable type, so we can push FP onto
 * the stack... it wouldn't be any cleaner for real, but it would
 * philosophically, since we're doing everything the same way.  We could
 * push it before new FP, so calling code cannot de-reference it.
 */
static struct var_t *
push_uargs(struct var_t *fn, struct var_t *owner)
{
        struct var_t *fpsav, *new_fp;

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
                /* push args, don't name them yet */
                do {
                        struct var_t *v = stack_getpush();
                        eval(v);
                        qlex();
                } while (cur_oc->t == OC_COMMA);
                expect(OC_RPAR);
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
        int i;

        new_fp = q_.sp;
        push_owner(fn, owner);

        for (i = 0; i < argc; i++)
                stack_push(argv[i]);

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

/*
 * With PC now at the first token _after_ the opening parenthesis of
 * the function _definition_, give the arguments names.
 *
 * Return with PC after the closing parenthesis.
 */
static void
resolve_uarg_names(void)
{
        struct var_t *argptr;
        int nargs = n_args();
        for (argptr = q_.fp + 1; argptr < q_.sp; argptr++) {
                qlex();
                expect('u');
                bug_on(argptr->name != NULL);
                argptr->name = cur_oc->s;
                qlex();
                --nargs;

                /* If not vararg, we should break here */
                if (cur_oc->t != OC_COMMA) {
                        q_unlex();
                        break;
                }
        }

        /*
         * TODO:
         * 1.   If not all args filled by caller, add some syntax where
         *      function may set defaults for optional args.
         * 2.   If *more* args are filled in than are asked for, say it's
         *      a vararg function like dear old C's printf, need some way
         *      of naming them too, perhaps a built-in 'environment'
         *      variable that we set here on every function call.
         */

        if (nargs)
                syntax("Argument number mismatch");
        qlex();
        expect(OC_RPAR);
}

/* Call an internal built-in function */
static void
ifunction_helper(struct var_t *fn, struct var_t *retval)
{
        /* Internal function, we don't touch LR or PC for this */
        int nargs;
        struct function_handle_t *fh = fn->fn;
        bug_on(!fh);
        bug_on(!fh->fni);
        if ((nargs = n_args()) != fh->minargs) {
                if (nargs < fh->minargs ||
                    (fh->maxargs > 0 &&
                     nargs > fh->maxargs)) {
                        syntax("Expected %d args but got %d",
                                fh->minargs, nargs);
                }
        }
        fh->fni(retval);
}

/* Call a user-defined function */
static void
ufunction_helper(struct var_t *fn, struct var_t *retval)
{
        int exres;
        struct function_handle_t *fh = fn->fn;
        struct marker_t lr;

        bug_on(!fh);

        /* Return address is just after ')' of function call */
        PC_BL(&fh->mk, &lr);

        resolve_uarg_names();

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
 * function_init_user - Initialize @func to be a user-defined function
 * @func: Empty variable to configure
 * @pc: Location in script of first '(' of argument list.
 */
void
function_init_user(struct var_t *func, const struct marker_t *pc)
{
        struct function_handle_t *fh;
        bug_on(func->magic != QEMPTY_MAGIC);

        fh = function_handle_new();
        fh->f_magic = FUNC_USER;
        memcpy(&fh->mk, pc, sizeof(fh->mk));
        func->fn = fh;
        func->magic = QFUNCTION_MAGIC;
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
        fh->fni = cb;
        fh->minargs = minargs;
        fh->maxargs = maxargs;
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

