#include "egq.h"
#include <string.h>

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

/*
 * symbol_walk: walk down the ".child.grandchild..." path of a parent
 *              and take certain actions.
 * @result: If the symbol is assigned, this stores the result.  It may
 *          be a dummy, but it may not be NULL
 * @parent: Parent of the ".child.grandchild..."
 * @flags:  Same as @flags arg to expression(), so we can tell if we
 *          need to skip looking for semicolon at the end.
 *
 * Program counter must be before the first '.', if it exists.
 * Note, the operation might happen with the parent itself, if no
 * '.something' is expressed.
 */
static void
symbol_walk(struct var_t *result, struct var_t *parent, unsigned int flags)
{
        for (;;) {
                if (parent->magic == QFUNCTION_MAGIC
                    || parent->magic == QINTL_MAGIC) {
                        call_function(parent, result, NULL);
                        goto expect_semi;
                        /* else, it's a variable assignment, fall through */
                }
                qlex();
                switch (cur_oc->t) {
                case OC_PER:
                        parent = walk_obj_helper(result, parent);
                        if (!parent)
                                goto expect_semi;
                        q_unlex();
                        break;
                case OC_LBRACK:
                        parent = walk_arr_helper(result, parent);
                        break;
                case OC_EQ:
                        eval(parent);
                        goto expect_semi;
                case OC_PLUSPLUS:
                        qop_incr(parent);
                        goto expect_semi;
                case OC_MINUSMINUS:
                        qop_decr(parent);
                        goto expect_semi;
                default:
                        /* TODO: +=, -=, <<=, >>=, &=, |=, etc. */
                        syntax("Invalid token %s at location", cur_oc->s);
                }
        }

expect_semi:
        if (!(flags & FE_FOR)) {
                qlex();
                expect(OC_SEMI);
        }
}

static void
do_childof(struct var_t *parent, unsigned int flags)
{
        struct var_t dummy;
        var_init(&dummy);
        symbol_walk(&dummy, parent, flags);
        var_reset(&dummy);
}

static void
seek_eob_1line_(int par, int brace, bool check_semi)
{
        while ((check_semi && cur_oc->t != OC_SEMI) || par || brace) {
                qlex();
                if (cur_oc->t == OC_LPAR)
                        ++par;
                else if (cur_oc->t == OC_RPAR)
                        --par;
                else if (cur_oc->t == OC_LBRACE)
                        ++brace;
                else if (cur_oc->t == OC_RBRACE)
                        --brace;
                else if (cur_oc->t == EOF)
                        break;
        }
}

/*
 * helper to seek_eob.
 * this is somehow more complicated than
 * just looking for a brace
 */
static void
seek_eob_1line(void)
{
        seek_eob_1line_(0, 0, true);
}

/* Helper to seek_eob - skip (...) */
static void
skip_par(int lpar)
{
        seek_eob_1line_(lpar, 0, false);
}

static void
seek_eob(int depth)
{
        if (!depth) {
                qlex();
                switch (cur_oc->t) {
                case OC_LBRACE:
                        seek_eob(1);
                        break;
                case OC_LPAR:
                        skip_par(1);
                        seek_eob(depth);
                        break;
                case OC_IF:
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
                        break;
                case OC_DO:
                        seek_eob(0);
                        qlex();
                        expect(OC_WHILE);
                        qlex();
                        expect(OC_LPAR);
                        skip_par(1);
                        qlex();
                        expect(OC_SEMI);
                        break;
                case OC_WHILE:
                        qlex();
                        expect(OC_LPAR);
                        skip_par(1);
                        seek_eob(0);
                        break;
                case OC_FOR:
                        qlex();
                        expect(OC_LPAR);
                        skip_par(1);
                        seek_eob(0);
                        break;
                case OC_SEMI:
                        break;
                default:
                        seek_eob_1line();
                        break;
                }
        } else while (depth && cur_oc->t != EOF) {
                qlex();
                switch (cur_oc->t) {
                case OC_LBRACE:
                        ++depth;
                        break;
                case OC_RBRACE:
                        --depth;
                        break;
                default:
                        break;
                }
        }
}

/*
 * pc is at first parenthesis
 * @par:        true    if we start before a parenthesis and expect a
 *                      closing parenthesis.
 *              false   if we're in the middle of a for loop header
 *                      and expect a semicolon
 *
 * return true if condition is true.
 */
static bool
get_condition(bool par)
{
        bool ret;
        struct var_t *cond = tstack_getpush();
        if (par) {
                qlex();
                expect(OC_LPAR);
                eval(cond);
                qlex();
                expect(OC_RPAR);
        } else {
                eval(cond);
                qlex();
                expect(OC_SEMI);
        }
        ret = !qop_cmpz(cond);
        tstack_pop(NULL);
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
        struct var_t *seekstart = tstack_getpush();
        struct var_t *pc        = tstack_getpush();
        qop_mov(pc, &q_.pc);
        while (get_condition(true)) {
                if (seekstart->magic == QEMPTY_MAGIC)
                        qop_mov(seekstart, &q_.pc);
                if ((r = expression(retval, 0)) != 0)
                        break;
                qop_mov(&q_.pc, pc);
        }
        if (seekstart->magic == QPTRX_MAGIC)
                qop_mov(&q_.pc, seekstart);
        tstack_pop(NULL);
        tstack_pop(NULL);
        seek_eob(0);
        return r;
}

static int
do_for(struct var_t *retval)
{
        struct var_t *start   = tstack_getpush();
        struct var_t *pc_cond = tstack_getpush();
        struct var_t *pc_op   = tstack_getpush();
        struct var_t *pc_blk  = tstack_getpush();
        struct var_t *sp = q_.sp;
        int r = 0;

        qop_mov(start, &q_.pc);
        qlex();
        expect(OC_LPAR);
        if (expression(NULL, 0) != 0)
                syntax("Unexpected break from for loop header");
        qop_mov(pc_cond, &q_.pc);
        for (;;) {
                if (!get_condition(false))
                        break;
                if (pc_op->magic == QEMPTY_MAGIC) {
                        qop_mov(pc_op, &q_.pc);
                        seek_eob_1line_(1, 0, false);
                        qop_mov(pc_blk, &q_.pc);
                } else {
                        qop_mov(&q_.pc, pc_blk);
                }

                if ((r = expression(retval, 0)) != 0)
                        break;
                qop_mov(&q_.pc, pc_op);
                if (expression(NULL, FE_FOR) != 0)
                        syntax("Unexpected break from loop header");
                qop_mov(&q_.pc, pc_cond);
        }

        qop_mov(&q_.pc, start);
        tstack_pop(NULL);
        tstack_pop(NULL);
        tstack_pop(NULL);
        tstack_pop(NULL);

        seek_eob(0);

        /*
         * This looks superfluous, but in the case of something like:
         *      "for (let i = 0; ..."
         * It keeps i out of the upper-level scope
         */
        while (q_.sp != sp)
                stack_pop(NULL);

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
 * @flags:      If FE_TOP, we're at the top level of a script (ie. not
 *              in a function)
 *              If FE_FOR, we're parsing the third part of the header of
 *              a for loop, so we do not look for a semicolon.
 *
 * Return:      0       if encountered end of statement
 *              1       if encountered return
 *              2       if encountered break
 *              3       if encountered EOF
 *
 * If we're not at the top level and EOF is encountered,
 * then an error will be thrown.
 */
int
expression(struct var_t *retval, unsigned int flags)
{
        /*
         * Save position of the stack pointer so we unwind lower-scope
         * variables at the end.
         */
        struct var_t *sp = NULL;
        int ret = 0;
        int brace = 0;
        if (!(flags & FE_TOP)) {
                qlex();
                if (cur_oc->t == OC_LBRACE) {
                        sp = q_.sp;
                        brace++;
                } else {
                        q_unlex();
                }
        }
        do {
                qlex();
                switch (cur_oc->t) {
                case 'u':
                        do_childof(esymbol_seek(cur_oc->s), flags);
                        break;
                case OC_SEMI: /* empty statement */
                        break;
                case OC_LET:
                        if (!!(flags & FE_FOR))
                                syntax("'let' not allowed at this part of 'for' header");
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
                case OC_FOR:
                        if ((ret = do_for(retval)) == 1)
                                goto done;
                        ret = 0;
                        break;
                case OC_DO:
                        if ((ret = do_do(retval)) == 1)
                                goto done;
                        ret = 0;
                        break;
                case OC_THIS:
                        do_childof(get_this(), flags);
                        break;
                case EOF:
                        if (!(flags & FE_TOP))
                                syntax("Unexpected EOF");
                        ret = 3;
                        goto done;
                default:
                        syntax("Token '%s' not allowed here", cur_oc->s);
                }
        } while (brace);
done:
        if (sp) {
                /*
                 * This was not a single-line expression,
                 * so unwind the stack pointer to where it was
                 * before the brace.  This means things like
                 *      if (cond) {
                 *              let x = ....
                 * will delete x after escaping the block
                 */
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
                int ret = expression(&dummy, FE_TOP);
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
