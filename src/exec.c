#include "egq.h"
#include <string.h>

static int expression(struct qvar_t *retval, bool top);

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
 * qcall_function - Call a function and execute it
 * @fn: Function handle, which may be user-defined or built-in
 * @retval: Return value of the function being called.
 */
void
qcall_function(struct qvar_t *fn, struct qvar_t *retval, struct qvar_t *owner)
{
        struct qvar_t *fpsav, *new_fp;
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
        qstack_push(&q_.lr);
        /*
         * can't change this yet because we need old frame pointer
         * while evaluating args.
         */
        new_fp = q_.sp;

        /* push "this" */
        qstack_push(owner ? owner :
                    (fn->magic == QINTL_MAGIC ? q_.gbl : fn->fn.owner));

        qlex();
        expect(OC_LPAR);

        qlex();
        if (cur_oc->t != OC_RPAR) {
                q_unlex();
                /* push args, don't name them yet */
                do {
                        struct qvar_t *v = qstack_getpush();
                        q_eval(v);
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
                                qsyntax("Expected %d args but got %d",
                                        fn->fni->minargs, nargs);
                        }
                }
                fn->fni->fn(retval);
        } else {
                /* User function */
                struct qvar_t *argptr;
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
                        qsyntax("Argument number mismatch");

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
                        qsyntax("Unexpected %s", exres == 2 ? "break" : "EOF");
                }

                /* restore PC */
                qop_mov(&q_.pc, &q_.lr);
                pcsanity();
        }

        /* Unwind stack to beginning of args */
        while (q_.sp != q_.fp)
                qstack_pop(NULL);

        /* restore LR */
        qstack_pop(&q_.lr);

        /* restore FP */
        q_.fp = fpsav;
}

/* Declare automatic variable */
static void
do_let(void)
{
        struct qvar_t *v, *p;

        qlex();
        expect('u');
        /* Make sure name is not same as other automatic vars */
        for (p = q_.fp + 1; p < q_.sp; p++) {
                if (p->name && !strcmp(cur_oc->s, p->name))
                        qsyntax("Variable `%s' is already declared", p->name);
        }

        v = qstack_getpush();
        v->name = cur_oc->s;

        qlex();
        switch (cur_oc->t) {
        case OC_SEMI:
                /* empty declaration, like "let x;" */
                break;
        case OC_EQ:
                /* assign v with the "something" of "let x = something" */
                q_eval(v);
                qlex();
                expect(OC_SEMI);
                break;
        }
}

static void
do_childof(struct qvar_t *parent)
{
        struct qvar_t dummy;
        qvar_init(&dummy);
        symbol_walk(&dummy, parent, true);
        qvar_reset(&dummy);
}

static void
do_identifier(void)
{
        struct qvar_t *v = symbol_seek(cur_oc->s);
        if (!v)
                qsyntax("Unrecognized symbol `%s'", cur_oc->s);

        do_childof(v);
}

static void
seek_eob_helper(int depth)
{
        while (depth && cur_oc->t != EOF) {
                qlex();
                if (cur_oc->t == OC_LBRACE)
                        ++depth;
                else if (cur_oc->t == OC_RBRACE)
                        --depth;
        }
}

static void
seek_eob(int depth)
{
        if (!depth) {
                qlex();
                if (cur_oc->t == OC_LBRACE) {
                        seek_eob_helper(1);
                } else while (cur_oc->t != OC_SEMI) {
                        qlex();
                        if (cur_oc->t == OC_LBRACE)
                                seek_eob_helper(1);
                }
        } else {
                seek_eob_helper(depth);
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
        struct qvar_t *cond = qstack_getpush();
        if (par) {
                qlex();
                expect(OC_LPAR);
                q_eval(cond);
                qlex();
                expect(OC_RPAR);
        } else {
                q_eval(cond);
        }
        ret = !qop_cmpz(cond);
        qstack_pop(NULL);
        return ret;
}

static int
do_if(struct qvar_t *retval)
{
        bool cond = get_condition(true);
        if (cond)
                return expression(retval, 0);
        seek_eob(0);
        return false;
}

static int
do_while(struct qvar_t *retval)
{
        struct qvar_t pc;
        qvar_init(&pc);
        qop_mov(&pc, &q_.pc);
        while (get_condition(true)) {
                int r;
                if ((r = expression(retval, 0)) != 0)
                        return r;
                qop_mov(&q_.pc, &pc);
        }
        seek_eob(0);
        return false;
}

/**
 * expression - execute a {...} statement
 * @retval:     Variable to store result, if "return xyz;"
 * @top:        1 if at the top level (not in a function)
 *              0 otherwise
 *
 * Return:      0       if encountered end of block
 *              1       if encountered return
 *              2       if encountered break
 *              3       if encountered EOF
 *
 * If we're not at the top level and EOF is encountered,
 * then an error will be thrown.
 */
static int
expression(struct qvar_t *retval, bool top)
{
        struct qvar_t *sp = q_.sp;
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
                case OC_LET:
                        do_let();
                        break;
                case OC_RBRACE:
                        if (!brace)
                                qsyntax("Unexpected '}'");
                        brace--;
                        break;
                case OC_RETURN:
                        qlex();
                        if (cur_oc->t != OC_SEMI) {
                                q_unlex();
                                q_eval(retval);
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
                case OC_THIS:
                        do_childof(get_this());
                        break;
                case EOF:
                        if (!top)
                                qsyntax("Unexpected EOF");
                        ret = 3;
                        goto done;
                default:
                        qsyntax("Token '%s' not allowed here", cur_oc->s);
                }
        } while (brace);
done:
        if (!top) {
                while (q_.sp != sp)
                        qstack_pop(NULL);
        }
        return ret;
}

void
exec_block(void)
{
        static struct qvar_t dummy;
        struct qvar_t *sp = q_.sp;
        for (;;) {
                qvar_init(&dummy);
                int ret = expression(&dummy, 1);
                if (ret) {
                        if (ret == 3)
                                break;
                        qsyntax("Cannot '%s' from top level",
                                ret == 1 ? "return" : "break");
                }
                qvar_reset(&dummy);
        }
        while (q_.sp != sp)
                qstack_pop(NULL);
}
