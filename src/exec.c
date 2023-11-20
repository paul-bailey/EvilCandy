#include "egq.h"
#include <string.h>

static int expression(struct var_t *retval, bool top);

/*
 * We just popped lr to pc, make sure it's valid
 * TODO: Wrap this with #ifndef NDEBUG
 */
static void
pcsanity(void)
{
        struct list_t *i;
        bool ok = false;
        list_foreach(i, &q_.ns) {
                struct ns_t *ns = container_of(i, struct ns_t, list);
                if (ns == cur_ns) {
                        struct token_t *t = &ns->pgm;
                        ok = cur_oc >= t->oc && cur_oc < &t->oc[t->p];
                        break;
                }
        }

        if (!ok)
                bug();
}

/**
 * call_function - Call a function and execute it
 * @fn: Function handle, which may be user-defined or built-in
 * @retval: Return value of the function being called.
 */
void
call_function(struct var_t *fn, struct var_t *retval, struct var_t *owner)
{
        struct var_t *fpsav, *new_fp;
        int nargs; /* for sanity checking */

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
         */

        /* push lr */
        stack_push(&q_.lr);
        /*
         * can't change this yet because we need old frame pointer
         * while evaluating args.
         */
        new_fp = q_.sp;

        /* push "this" */
        stack_push(owner ? owner :
                    (fn->magic == QINTL_MAGIC ? q_.gbl : fn->fn.owner));

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

        nargs = q_.sp - 1 - q_.fp;
        if (fn->magic == QINTL_MAGIC) {
                /* Internal function, we don't touch LR or PC for this */
                bug_on(!fn->fni);
                bug_on(!fn->fni->fn);
                if (nargs != fn->fni->minargs) {
                        if (nargs < fn->fni->minargs ||
                            (fn->fni->maxargs > 0 &&
                             nargs > fn->fni->maxargs)) {
                                syntax("Expected %d args but got %d",
                                        fn->fni->minargs, nargs);
                        }
                }
                fn->fni->fn(retval);
        } else {
                /* User function */
                struct var_t *argptr;
                int exres;

                /*
                 * Return address is _before_ semicolon, not after,
                 * since we don't always expect a semicolon afterward.
                 */
                /* move PC into LR */
                qop_mov(&q_.lr, &q_.pc);

                /* move destination into PC */
                qop_mov(&q_.pc, fn);

                /*
                 * Functions should have their PC saved
                 * to the 1st token after the opening
                 * parenthesis of the declaration of their arguments.
                 */
                argptr = q_.fp + 1;
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

                /* peek but don't stay;
                 * expression() expects us to be before the brace
                 */
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
                pcsanity();
        }

        /* Unwind stack to beginning of args */
        while (q_.sp != q_.fp)
                stack_pop(NULL);

        /* restore LR */
        stack_pop(&q_.lr);

        /* restore FP */
        q_.fp = fpsav;
}

/* Declare automatic variable */
static void
do_let(void)
{
        struct var_t *v, *p;

        qlex();
        expect('u');
        /* Make sure name is not same as other automatic vars */
        for (p = q_.fp + 1; p < q_.sp; p++) {
                if (p->name && !strcmp(cur_oc->s, p->name))
                        syntax("Variable `%s' is already declared", p->name);
        }

        v = stack_getpush();
        v->name = cur_oc->s;

        qlex();
        switch (cur_oc->t) {
        case OC_SEMI:
                /* empty declaration, like "let x;" */
                break;
        case OC_EQ:
                /* assign v with the "something" of "let x = something" */
                eval(v);
                qlex();
                expect(OC_SEMI);
                break;
        }
}

/* either returns pointer to new parent, or NULL, meaning "wrap it up" */
static struct var_t *
walk_obj_helper(struct var_t *result, struct var_t *parent)
{
        do {
                struct var_t *child;
                qlex();
                expect('u');
                if (parent->magic != QOBJECT_MAGIC) {
                        /*
                         * calling a typedef's built-in methods.
                         * All types are slightly objects, slightly not.
                         */
                        struct var_t *method;
                        method = ebuiltin_method(parent, cur_oc->s);
                        call_function(method, result, parent);
                        return NULL;
                }
                child = object_child(parent, cur_oc->s);
                if (!child) {
                        /*
                         * Parsing "alice.bob = something;", where
                         * "alice" exists and "bob" does not.
                         * Append "bob" as a new child of "alice",
                         * and evaluate the "something" of "bob".
                         */
                        child = var_new();
                        child->name = cur_oc->s;
                        qlex();
                        expect(OC_EQ);
                        eval(child);
                        object_add_child(parent, child);
                        qlex();
                        expect(OC_SEMI);
                        return NULL;
                }
                parent = child;
                qlex();
        } while (cur_oc->t == OC_PER);
        return parent;
}

/*
 * FIXME: "array[x]=y;" should be valid if x is out of bounds of the array.
 * It just means we have to grow the array.
 */
static struct var_t *
walk_arr_helper(struct var_t *result, struct var_t *parent)
{
        struct var_t *idx;
        struct var_t *child;
        /* Don't try to evaluate associative-array indexes this way */
        if (parent->magic != QARRAY_MAGIC) {
                syntax("Cannot de-reference type %s with [",
                        typestr(parent->magic));
        }
        idx = stack_getpush();
        eval(idx);
        if (idx->magic != QINT_MAGIC)
                syntax("Array index must be integer");
        child = array_child(parent, idx->i);
        if (!child)
                syntax("Array de-reference out of bounds");
        stack_pop(NULL);
        return child;
}

/**
 * symbol_walk: walk down the ".child.grandchild..." path of a parent
 *              and take certain actions.
 * @result: If the symbol is assigned, this stores the result.  It may
 *          be a dummy, but it may not be NULL
 * @parent: Parent of the ".child.grandchild..."
 *
 * Program counter must be before the first '.', if it exists.
 * Note, the operation might happen with the parent itself, if no
 * '.something' is expressed.
 */
static void
symbol_walk(struct var_t *result, struct var_t *parent)
{
        for (;;) {
                if (parent->magic == QFUNCTION_MAGIC
                    || parent->magic == QINTL_MAGIC) {
                        call_function(parent, result, NULL);
                        qlex();
                        expect(OC_SEMI);
                        break;
                        /* else, it's a variable assignment, fall through */
                }
                qlex();
                if (cur_oc->t == OC_PER) {
                        parent = walk_obj_helper(result, parent);
                        if (!parent)
                                break;
                        q_unlex();
                } else if (cur_oc->t == OC_LBRACK) {
                        parent = walk_arr_helper(result, parent);
                } else {
                        /*
                         * we're at the "that" of "this = that;",
                         * where @parent is the "this".
                         */
                        expect(OC_EQ);
                        eval_safe(parent);
                        qlex();
                        expect(OC_SEMI);
                        break;
                }
        }
}

static void
do_childof(struct var_t *parent)
{
        struct var_t dummy;
        var_init(&dummy);
        symbol_walk(&dummy, parent);
        var_reset(&dummy);
}

static void
do_identifier(void)
{
        struct var_t *v = symbol_seek(cur_oc->s);
        if (!v)
                syntax("Unrecognized symbol `%s'", cur_oc->s);

        do_childof(v);
}

/*
 * helper to seek_eob.
 * this is somehow more complicated than
 * just looking for a brace
 */
static void
seek_eob_1line(void)
{
        int par = 0;
        int brace = 0;
        while (cur_oc->t != OC_SEMI || par || brace) {
                if (cur_oc->t == OC_LPAR)
                        ++par;
                else if (cur_oc->t == OC_RPAR)
                        --par;
                else if (cur_oc->t == OC_LBRACE)
                        ++brace;
                else if (cur_oc->t == OC_RBRACE)
                        --brace;
                qlex();
                if (cur_oc->t == EOF)
                        break;
        }
}

/* Helper to seek_eob - skip (...) */
static void
skip_par(int lpar)
{
        do {
                qlex();
                if (cur_oc->t == OC_LPAR)
                        lpar++;
                else if (cur_oc->t == OC_RPAR)
                        lpar--;
        } while (lpar);
}

static void
seek_eob(int depth)
{
        if (!depth) {
                qlex();
                if (cur_oc->t == OC_LBRACE) {
                        seek_eob(1);
                } else if (cur_oc->t == OC_IF) {
                        /* special case: there might be an ELSE */
                        qlex();
                        expect(OC_LPAR);
                        skip_par(1);
                        seek_eob(0);
                        qlex();
                        if (cur_oc->t == OC_ELSE)
                                seek_eob(0);
                        else
                                q_unlex();
                } else if (cur_oc->t == OC_DO) {
                        seek_eob(0);
                        qlex();
                        expect(OC_WHILE);
                        qlex();
                        expect(OC_LPAR);
                        skip_par(1);
                        qlex();
                        expect(OC_SEMI);
                } else {
                        seek_eob_1line();
                }
        } else while (depth && cur_oc->t != EOF) {
                qlex();
                if (cur_oc->t == OC_LBRACE)
                        ++depth;
                else if (cur_oc->t == OC_RBRACE)
                        --depth;
        }
}

/*
 * pc is at first parenthesis
 * if not @par, we're in a for loop
 *
 * return true if condition is true.
 */
static bool
get_condition(bool par)
{
        bool ret;
        struct var_t *cond = stack_getpush();
        if (par) {
                qlex();
                expect(OC_LPAR);
                eval(cond);
                qlex();
                expect(OC_RPAR);
        } else {
                eval(cond);
        }
        ret = !qop_cmpz(cond);
        stack_pop(NULL);
        return ret;
}

/*
 * wrapper to expression() which returns PC to beginning of the
 * expression.  In some cases, this is superfluous, but it's not a lot of
 * overhead and it makes it a lot easier to keep track of program counter
 * at end of a possibly deep thread.
 */
static int
expression_and_back(struct var_t *retval)
{
        int ret;
        stack_push(&q_.pc);
        ret = expression(retval, 0);
        stack_pop(&q_.pc);
        return ret;
}

static int
do_if(struct var_t *retval)
{
        int ret = 0;
        bool cond = get_condition(true);
        if (cond)
                ret = expression_and_back(retval);
        seek_eob(0);
        qlex();
        if (cur_oc->t == OC_ELSE) {
                if (!cond)
                        ret = expression_and_back(retval);
                seek_eob(0);
        } else {
                q_unlex();
        }
        return ret;
}

static int
do_while(struct var_t *retval)
{
        int r = 0;
        struct var_t *pc = stack_getpush();
        qop_mov(pc, &q_.pc);
        while (get_condition(true)) {
                if ((r = expression(retval, 0)) != 0)
                        break;
                qop_mov(&q_.pc, pc);
        }
        stack_pop(&q_.pc);
        seek_eob(0);
        return r;
}

/* he he... he he... "dodo" */
static int
do_do(struct var_t *retval)
{
        int r = 0;
        struct var_t *saved_pc = stack_getpush();
        qop_mov(saved_pc, &q_.pc);
        for (;;) {
                if ((r = expression(retval, 0)) != 0)
                        break;
                qlex();
                expect(OC_WHILE);
                if (get_condition(true)) {
                        qop_mov(&q_.pc, saved_pc);
                } else {
                        qlex();
                        expect(OC_SEMI);
                        break;
                }
        }
        /*
         * we are either at the end of "while(cond);" or
         * we encountered "break;", so no need to restore
         * PC and find the end of block, etc.
         */
        stack_pop(NULL);
        return r;
}

/**
 * expression - execute a {...} statement, which may be unbraced and on
 *              a single line.
 * @retval:     Variable to store result, if "return xyz;"
 * @top:        1 if at the top level (not in a function)
 *              0 otherwise
 *
 * Return:      0       if encountered end of statement
 *              1       if encountered return
 *              2       if encountered break
 *              3       if encountered EOF
 *
 * If we're not at the top level and EOF is encountered,
 * then an error will be thrown.
 */
static int
expression(struct var_t *retval, bool top)
{
        /*
         * Save position of the stack pointer so we unwind lower-scope
         * variables at the end.
         */
        struct var_t *sp = q_.sp;
        int ret = 0;
        int brace = 0;
        if (!top) {
                qlex();
                if (cur_oc->t == OC_LBRACE)
                        brace++;
                else
                        q_unlex();
        }
        do {
                qlex();
                switch (cur_oc->t) {
                case 'u':
                        do_identifier();
                        break;
                case OC_SEMI: /* empty statement */
                        break;
                case OC_LET:
                        do_let();
                        break;
                case OC_RBRACE:
                        if (!brace)
                                syntax("Unexpected '}'");
                        brace--;
                        break;
                case OC_RETURN:
                        qlex();
                        if (cur_oc->t != OC_SEMI) {
                                q_unlex();
                                eval(retval);
                                qlex();
                                expect(OC_SEMI);
                        }
                        ret = 1;
                        goto done;
                case OC_BREAK:
                        qlex();
                        expect(OC_SEMI);
                        ret = 2;
                        goto done;
                case OC_IF:
                        ret = do_if(retval);
                        /* "break" descends "if" */
                        if (ret != 0)
                                goto done;
                        break;
                case OC_WHILE:
                        if ((ret = do_while(retval)) == 1)
                                goto done;
                        /* don't accidentally double-break */
                        ret = 0;
                        break;
                case OC_DO:
                        if ((ret = do_do(retval)) == 1)
                                goto done;
                        ret = 0;
                        break;
                case OC_THIS:
                        do_childof(get_this());
                        break;
                case EOF:
                        if (!top)
                                syntax("Unexpected EOF");
                        ret = 3;
                        goto done;
                default:
                        syntax("Token '%s' not allowed here", cur_oc->s);
                }
        } while (brace);
done:
        if (!top) {
                while (q_.sp != sp)
                        stack_pop(NULL);
        }
        return ret;
}

void
exec_block(void)
{
        static struct var_t dummy;
        struct var_t *sp = q_.sp;
        for (;;) {
                var_init(&dummy);
                int ret = expression(&dummy, 1);
                if (ret) {
                        if (ret == 3)
                                break;
                        syntax("Cannot '%s' from top level",
                                ret == 1 ? "return" : "break");
                }
                var_reset(&dummy);
        }
        while (q_.sp != sp)
                stack_pop(NULL);
}
