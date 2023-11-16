#include "egq.h"
#include <string.h>

static void
expect(int opcode, const char *what)
{
        if (cur_oc->t != opcode)
                qerr_expected(what);
}

static bool interpret_block(struct qvar_t *retval);

/*
 * We just popped lr to pc, make sure it's valid
 * TODO: Wrap this with #ifndef NDEBUG
 */
static void
pcsanity(void)
{
        struct ns_t *ns;
        bool ok = false;
        for (ns = q_.ns_top; ns != NULL; ns = ns->next) {
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
qcall_function(struct qvar_t *fn, struct qvar_t *retval)
{
        struct qvar_t *fpsav, *new_fp;

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
        qstack_push(fn->magic == QINTL_MAGIC ? q_.gbl : fn->fn.owner);

        qlex();
        expect(OC_LPAR, "(");

        /* push args, don't name them yet */
        do {
                struct qvar_t *v = qstack_getpush();
                q_eval(v);
                qlex();
        } while (cur_oc->t == OC_COMMA);
        expect(OC_RPAR, ")");

        fpsav = q_.fp;
        q_.fp = new_fp;

        /*
         * Return address is _before_ semicolon, not after,
         * since we don't always expect a semicolon afterward.
         */
        /* move PC into LR */
        qop_mov(&q_.lr, &q_.pc);

        if (fn->magic == QINTL_MAGIC) {
                /* Internal function, we don't touch LR or PC for this */
                int nargs = (q_.sp - 1 - q_.fp);
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

                /* move destination into PC */
                qop_mov(&q_.pc, fn);

                /*
                 * Functions should have their PC saved
                 * to the 1st token after the opening
                 * parenthesis of the declaration of their arguments.
                 */
                for (argptr = q_.fp + 1; argptr < q_.sp; argptr++) {
                        qlex();
                        expect('u', "identifier");
                        bug_on(argptr->name != NULL);
                        /*
                         * FIXME: The whole point of a stack array
                         * is to avoid all this mallocing and freeing
                         * data in small amounts.  But I still have
                         * to do that for stack-variable names.
                         */
                        argptr->name = cur_oc->s;
                        qlex();

                        /* If not vararg, we should break here */
                        if (cur_oc->t != OC_COMMA)
                                break;
                }

                if (argptr != q_.sp - 1)
                        qsyntax("Argument number mismatch");

                /*
                 * XXX: if varargs, cur_oc->t is for ',' and next tok is "..."
                 */
                expect(OC_RPAR, ")");
                qlex();
                expect(OC_LBRACE, "{");

                /* execute it */
                interpret_block(retval);

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
        expect('u', "identifier");
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
                expect(OC_SEMI, ";");
                break;
        }
}

/*
 * PC points directly after opening '{',
 * unless we are at top level
 * (not running in a function).
 *
 * Return:      true    if encountered "return"
 *              false   if encountered "break" or end of block
 */
static bool
interpret_block(struct qvar_t *retval)
{
        int brace = 1;
        while (brace) {
                qlex();
                switch (cur_oc->t) {
                case 'u':
                    {
                        /* TODO: Add F_FORCE, we might be assigning new member */
                        struct qvar_t *v = qsymbol_lookup(cur_oc->s, 0);
                        if (!v) {
                                qsyntax("Unrecognized symbol `%s'",
                                        cur_oc->s);
                        }
                        if (v->magic == QFUNCTION_MAGIC
                            || v->magic == QINTL_MAGIC) {
                                struct qvar_t dummy;
                                qvar_init(&dummy);
                                qcall_function(v, &dummy);
                                qvar_reset(&dummy);

                                qlex();
                                expect(OC_SEMI, ";");
                        } else {
                                qlex();
                                expect(OC_EQ, "assignment");
                                q_eval(v);
                                qlex();
                                expect(OC_SEMI, ";");
                        }
                        break;
                    }
                case OC_LET:
                        do_let();
                        break;
                case OC_RBRACE:
                        brace--;
                        if (!brace && q_.fp == q_.stack)
                                qsyntax("Unexpected '}'");
                        break;
                case OC_RETURN:
                        if (q_.fp == q_.stack)
                                qsyntax("Cannot return from global scope");
                        qlex();
                        if (cur_oc->t != OC_SEMI) {
                                q_unlex();
                                q_eval(retval);
                                qlex();
                                expect(OC_SEMI, ";");
                        }
                        while (brace && cur_oc->t != EOF)
                                qlex();
                        return true;
                case OC_BREAK:
                        qlex();
                        expect(OC_SEMI, ";");
                        while (brace && cur_oc->t != EOF)
                                qlex();
                        return false;

                case EOF:
                        if (q_.fp != q_.stack)
                                qsyntax("Unexpected end of script");
                        return false;
                default:
                        qsyntax("Token '%s' not allowed here", cur_oc->s);
                }
        }
        return false;
}

void
exec_script(struct ns_t *ns)
{
        bug_on(!ns->pgm.oc);

        /* Initialize program counter */
        cur_ns = ns;
        cur_oc = ns->pgm.oc;
        /* Initialize stack regs */
        q_.sp = q_.stack;
        q_.fp = q_.stack;

        /* Point initial frame pointer to "__gbl__" */
        qstack_push(q_.gbl);

        qvar_init(&q_.lr);
        qop_mov(&q_.lr, &q_.pc);

        if (cur_oc->t == EOF)
                return;

        /*
         * dirty, but we only do it here: we want the first
         * call to qlex() to get the FIRST opcode, not the SECOND.
         * We don't call q_unlex, because that will trap an
         * out-of-bounds bug.  Like I said, it's dirty.
         */
        cur_oc--;

        /*
         * interpret_block won't fill retval, because "return" is
         * invalidated before that could happen, so NULL
         */
        interpret_block(NULL);
}
