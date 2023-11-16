#include "egq.h"
#include <string.h>

static void
qstack_pop(struct qvar_t *to)
{
        bug_on(q_.sp <= &q_.stack[0]);
        q_.sp--;
        if (to)
                qop_mov(to, q_.sp);
        if (q_.sp->name) {
                q_literal_free(q_.sp->name);
                q_.sp->name = NULL;
        }
        qvar_reset(q_.sp);
}

static struct qvar_t *
qstack_getpush(void)
{
        struct qvar_t *res = q_.sp;
        if (res >= &q_.stack[QSTACKMAX])
                qsyntax("Stack overflow");
        ++q_.sp;
        qvar_init(res);
        return res;
}

static void
qstack_push(struct qvar_t *v)
{
        struct qvar_t *to = qstack_getpush();
        qop_mov(to, v);
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
                if (ns == q_.pc.px.ns) {
                        struct token_t *t = &ns->pgm;
                        const char *pc = q_.pc.px.s;
                        ok = pc >= t->s && pc < &t->s[t->p];
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
        if (q_.t != TO_DTOK(QD_LPAR))
                qerr_expected("(");

        /* push args, don't name them yet */
        do {
                struct qvar_t *v = qstack_getpush();
                q_eval(v);
                qlex();
        } while (q_.t == TO_DTOK(QD_COMMA));
        if (q_.t != TO_DTOK(QD_RPAR))
                qerr_expected(")");

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
                        if (q_.t != 'u')
                                qerr_expected("identifier");
                        bug_on(argptr->name != NULL);
                        /*
                         * FIXME: The whole point of a stack array
                         * is to avoid all this mallocing and freeing
                         * data in small amounts.  But I still have
                         * to do that for stack-variable names.
                         */
                        argptr->name = q_literal(q_.tok.s);
                        qlex();

                        /* If not vararg, we should break here */
                        if (q_.t != TO_DTOK(QD_COMMA))
                                break;
                }

                if (argptr != q_.sp - 1)
                        qsyntax("Argument number mismatch");

                /*
                 * XXX: if varargs, q_.t is for ',' and next tok is "..."
                 */
                if (q_.t != TO_DTOK(QD_RPAR))
                        qerr_expected(")");
                qlex();
                if (q_.t != TO_DTOK(QD_LBRACE))
                        qerr_expected("{");

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
        if (q_.t != 'u')
                qerr_expected("identifier");
        /* Make sure name is not same as other automatic vars */
        for (p = q_.fp + 1; p < q_.sp; p++) {
                if (p->name && !strcmp(q_.tok.s, p->name))
                        qsyntax("Variable `%s' is already declared", p->name);
        }

        v = qstack_getpush();
        v->name = q_literal(q_.tok.s);

        qlex();
        switch (q_.t) {
        case TO_DTOK(QD_SEMI):
                /* empty declaration, like "let x;" */
                break;
        case TO_DTOK(QD_EQ):
                /* assign v with the "something" of "let x = something" */
                q_eval(v);
                qlex();
                if (q_.t != TO_DTOK(QD_SEMI))
                        qerr_expected(";");
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
                switch (q_.t) {
                case 'u':
                    {
                        struct qvar_t *v = qsymbol_lookup(q_.tok.s);
                        if (!v) {
                                qsyntax("Unrecognized symbol `%s'",
                                        q_.tok.s);
                        }
                        if (v->magic == QFUNCTION_MAGIC
                            || v->magic == QINTL_MAGIC) {
                                struct qvar_t dummy;
                                qvar_init(&dummy);
                                qcall_function(v, &dummy);
                                qvar_reset(&dummy);

                                qlex();
                                if (q_.t != TO_DTOK(QD_SEMI))
                                        qerr_expected(";");
                        } else {
                                qlex();
                                if (q_.t != TO_DTOK(QD_EQ))
                                        qerr_expected("assignment");
                                q_eval(v);
                                qlex();
                                if (q_.t != TO_DTOK(QD_SEMI))
                                        qerr_expected(";");
                        }
                        break;
                    }
                case TO_KTOK(KW_LET):
                        do_let();
                        break;
                case TO_DTOK(QD_RBRACE):
                        brace--;
                        if (!brace && q_.fp == q_.stack)
                                qsyntax("Unexpected '}'");
                        break;
                case TO_KTOK(KW_RETURN):
                        if (q_.fp == q_.stack)
                                qsyntax("Cannot return from global scope");
                        qlex();
                        if (q_.t != TO_DTOK(QD_SEMI)) {
                                q_unlex();
                                q_eval(retval);
                                qlex();
                                if (q_.t != TO_DTOK(QD_SEMI))
                                        qerr_expected(";");
                        }
                        while (brace && q_.t != EOF)
                                qlex();
                        return true;
                case TO_KTOK(KW_BREAK):
                        qlex();
                        if (q_.t != TO_DTOK(QD_SEMI))
                                qerr_expected(";");
                        while (brace && q_.t != EOF)
                                qlex();
                        return false;

                case EOF:
                        if (q_.fp != q_.stack)
                                qsyntax("Unexpected end of script");
                        return false;
                default:
                        qsyntax("Token not allowed here");
                }
        }
        return false;
}

int
exec_script(struct ns_t *ns)
{
        int t;

        /* Initialize program counter */
        q_.pc.px.ns = q_.pclast.px.ns = ns;
        q_.pc.px.s  = q_.pclast.px.s  = ns->pgm.s;
        /* Initialize stack regs */
        q_.sp = q_.stack;
        q_.fp = q_.stack;

        /* Point initial frame pointer to "__gbl__" */
        qstack_push(q_.gbl);

        qvar_init(&q_.lr);
        qop_mov(&q_.lr, &q_.pc);

        t = qlex();
        if (t == EOF)
                return -1;

        q_unlex();

        /*
         * interpret_block won't fill retval, because "return" is
         * invalidated before that could happen, so NULL
         */
        interpret_block(NULL);

        return 0;
}
