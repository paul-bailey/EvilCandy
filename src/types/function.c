/*
 * function.c - two part module
 *      1. Code that calls a function (call_function() & family)
 *      2. Code dealing specifically with struct var_t with
 *         function-like magic number
 */
#include "var.h"

/*
 * We just popped lr to pc, make sure it's valid
 * TODO: Wrap this with #ifndef NDEBUG
 */
#ifndef NDEBUG
static void
pcsanity(struct marker_t *mk)
{
        struct list_t *i;
        bool ok = false;
        struct buffer_t *t = &mk->ns->pgm;
        list_foreach(i, &q_.ns) {
                struct ns_t *ns = container_of(i, struct ns_t, list);
                if (ns == mk->ns) {
                        char *ocs = (char *)mk->oc;
                        ok = ocs >= t->s && ocs < &t->s[t->p];
                        break;
                }
        }

        bug_on(!ok);
}
#else
# define pcsanity(...) do { (void)0; } while (0)
#endif

/* push @owner...or something...onto the stack */
static void
push_owner(struct var_t *fn, struct var_t *owner)
{
        /* push "this" */
        if (!owner) {
                if (fn->magic == QFUNCTION_MAGIC)
                        owner = fn->fn.owner;
                if (!owner)
                        owner = get_this();
                bug_on(!owner);
        }
        stack_push(owner);
}

/*
 * Stack order after call is:
 *
 *      LR
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

        /* push lr */
        stack_push(&q_.lr);
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

        stack_push(&q_.lr);
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

        /* restore LR */
        stack_pop(&q_.lr);

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

        if (nargs)
                syntax("Argument number mismatch");
        /*
         * XXX: if varargs, cur_oc->t is for ',' and next tok is "..."
         */
        qlex();
        expect(OC_RPAR);
}

/* Call an internal built-in function */
static void
ifunction_helper(struct var_t *fn, struct var_t *retval)
{
        /* Internal function, we don't touch LR or PC for this */
        bug_on(!fn->fni);
        bug_on(!fn->fni->fn);
        int nargs;
        if ((nargs = n_args()) != fn->fni->minargs) {
                if (nargs < fn->fni->minargs ||
                    (fn->fni->maxargs > 0 &&
                     nargs > fn->fni->maxargs)) {
                        syntax("Expected %d args but got %d",
                                fn->fni->minargs, nargs);
                }
        }
        fn->fni->fn(retval);
}

/* Call a user-defined function */
static void
ufunction_helper(struct var_t *fn, struct var_t *retval)
{
        int exres;

        /*
         * Return address is _before_ semicolon, not after,
         * since we don't always expect a semicolon afterward.
         */
        /* move PC into LR */
        qop_mov(&q_.lr, &q_.pc);

        pcsanity(&fn->fn.mk);

        /* move destination into PC */
        qop_mov(&q_.pc, fn);

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
        qop_mov(&q_.pc, &q_.lr);
        pcsanity(cur_mk);
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
        struct var_t *fpsav = push_uargs(fn, owner);
        struct var_t *retval_save = retval;

        if (!retval_save)
                retval = tstack_getpush();

        if (fn->magic == QPTRXI_MAGIC) {
                ifunction_helper(fn, retval);
        } else {
                bug_on(fn->magic != QFUNCTION_MAGIC);
                ufunction_helper(fn, retval);
        }

        if (!retval_save)
                tstack_pop(NULL);

        pop_args(fpsav);
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
        struct var_t *fpsav = push_iargs(fn, owner, argc, argv);
        struct var_t *retval_save = retval;

        if (!retval_save)
                retval = tstack_getpush();

        if (fn->magic == QPTRXI_MAGIC) {
                ifunction_helper(fn, retval);
        } else {
                bug_on(fn->magic != QFUNCTION_MAGIC);
                ufunction_helper(fn, retval);
        }

        if (!retval_save)
                tstack_pop(NULL);

        pop_args(fpsav);
}

static void
ptrxu_mov(struct var_t *to, struct var_t *from)
{
        if (from->magic == QFUNCTION_MAGIC) {
                to->px.ns = from->fn.mk.ns;
                to->px.oc = from->fn.mk.oc;
        } else if (from->magic == QPTRXU_MAGIC) {
                to->px.ns = from->px.ns;
                to->px.oc = from->px.oc;
        } else {
                syntax("MOV type mismatch");
        }
}

static const struct operator_methods_t ptrxu_primitives = {
        .mov = ptrxu_mov,
};

static void
ptrxi_mov(struct var_t *to, struct var_t *from)
{
        if (from->magic != QPTRXI_MAGIC)
                syntax("MOV type mismatch");
        to->fni = from->fni;
}

static const struct operator_methods_t ptrxi_primitives = {
        .mov = ptrxi_mov,
};

static bool
func_cmpz(struct var_t *func)
{
        return false;
}

static void
func_mov(struct var_t *to, struct var_t *from)
{
        if (from->magic == QPTRXU_MAGIC) {
                to->fn.mk.ns = from->px.ns;
                to->fn.mk.oc = from->px.oc;
        } else {
                to->fn.mk.oc = from->fn.mk.oc;
                to->fn.mk.ns = from->fn.mk.ns;
                if (to->magic == QEMPTY_MAGIC || !to->fn.owner)
                        to->fn.owner = from->fn.owner;
        }
}

static const struct operator_methods_t function_primitives = {
        .cmpz = func_cmpz,
        .mov  = func_mov,
};

void
typedefinit_function(void)
{
        var_config_type(QFUNCTION_MAGIC,
                        "function",
                        &function_primitives,
                        NULL);
        var_config_type(QPTRXU_MAGIC,
                        "code_pointer",
                        &ptrxu_primitives,
                        NULL);
        var_config_type(QPTRXI_MAGIC,
                        "built_in_function",
                        &ptrxi_primitives,
                        NULL);
}

