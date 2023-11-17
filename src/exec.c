#include "egq.h"
#include <string.h>

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
                qlex();
                expect(OC_LBRACE);

                /* execute it */
                exec_block(retval, 1);

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

/* We throw away the return value at this scope */
static void
call_empty_function(struct qvar_t *fn, struct qvar_t *owner)
{
        struct qvar_t dummy;
        qvar_init(&dummy);
        qcall_function(fn, &dummy, owner);
        qvar_reset(&dummy);
}

/*
 * helper to do_identifier.
 * non-object type acting object-ish,
 * maybe it's calling a built-in type method.
 */
static void
try_builtin(struct qvar_t *v)
{
        struct qvar_t *w;

        qlex();
        expect('u');
        w = builtin_method(v, cur_oc->s);
        if (!w) {
                qsyntax("type %s has no method %s",
                        q_typestr(v->magic), cur_oc->s);
        }
        call_empty_function(w, v);
}

static void
do_identifier(void)
{
        struct qvar_t *v = qsymbol_lookup(cur_oc->s, F_FIRST);
        if (!v)
                qsyntax("Unrecognized symbol `%s'", cur_oc->s);

        for (;;) {
                if (v->magic == QFUNCTION_MAGIC
                    || v->magic == QINTL_MAGIC) {
                        call_empty_function(v, NULL);
                        break;
                }
                qlex();
                if (cur_oc->t != OC_PER) {
                        struct qvar_t w;
                        qvar_init(&w);
                        expect(OC_EQ);
                        q_eval(&w);
                        qop_mov(v, &w);
                        qvar_reset(&w);
                        break;
                }

                do {
                        struct qvar_t *w;
                        if (v->magic != QOBJECT_MAGIC) {
                                try_builtin(v);
                                return;
                        }

                        qlex();
                        expect('u');
                        w = qobject_child(v, cur_oc->s);
                        if (!w) {
                                /* assigning a new member */
                                w = qvar_new();
                                w->name = cur_oc->s;
                                qlex();
                                expect(OC_EQ);
                                q_eval(w);
                                qobject_add_child(v, w);
                                goto out;
                        }
                        v = w;
                        qlex();
                } while (cur_oc->t == OC_PER);
                q_unlex();
        }

out:
        qlex();
        expect(OC_SEMI);
}

static void
seek_end_of_block(int depth)
{
        while (depth && cur_oc->t != EOF) {
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
        /*
         * TODO: In future, figure out a way to do this
         * for unbraced, single-line expressions.
         */
        qlex();
        expect(OC_LBRACE);
        if (cond)
                return exec_block(retval, 1);
        seek_end_of_block(1);
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
                qlex();
                expect(OC_LBRACE);
                if ((r = exec_block(retval, 1)) != 0)
                        return r;
                qop_mov(&q_.pc, &pc);
        }
        qlex();
        expect(OC_LBRACE);
        seek_end_of_block(1);
        return false;
}

/**
 * exec_block - execute a {...} statement
 * @retval:     Variable to store result, if "return xyz;"
 * @brace:      1 if PC points directly after opening '{',
 *              0 otherwise (ie we're at a script's top level)
 *
 * Return:      0       if encountered end of block
 *              1       if encountered return
 *              2       if encountered break
 *
 * If we're not at the top level and EOF is encountered,
 * then an error will be thrown.
 */
int
exec_block(struct qvar_t *retval, int brace)
{
        struct qvar_t *sp = q_.sp;
        int ret = 0;
        bool top = false;
        if (!brace) {
                top = true;
                brace++;
        }
        while (brace) {
                qlex();
                switch (cur_oc->t) {
                case 'u':
                        do_identifier();
                        break;
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
                                expect(OC_SEMI);
                        }
                        ret = 1;
                        goto done;
                case OC_BREAK:
                        qlex();
                        expect(OC_SEMI);
                        seek_end_of_block(brace);
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
                case EOF:
                        if (!top)
                                qsyntax("Unexpected EOF");
                        goto done;
                default:
                        qsyntax("Token '%s' not allowed here", cur_oc->s);
                }
        }
done:
        while (q_.sp != sp)
                qstack_pop(NULL);
        return ret;
}


