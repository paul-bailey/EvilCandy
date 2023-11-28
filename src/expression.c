/*
 * expression.c - Code for parsing either a {...} block or a
 * single-line statement.
 */
#include "egq.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>

/* some return values for do_childof_r, better than a bunch of goto's */
enum {
        ER_INDEX_TYPE = 1,
        ER_BADTOK,
        ER_NOTASSOC,
};

static int do_childof_r(struct var_t *v, struct var_t *parent);

static void
add_new_child(struct var_t *parent, char *name_lit)
{
        unsigned int flags = 0;
        struct var_t *child;
        qlex();
        if (cur_oc->t == OC_CONST) {
                flags = VF_CONST;
                qlex();
        }
        expect(OC_EQ);
        child = var_new();
        child->name = name_lit;
        eval(child);
        child->flags = flags;
        object_add_child(parent, child);
}

/*
 * helper to handle_lbrack --> do_childof_r
 *      we got ["abc"]
 */
static int
handle_assoc_arr(struct var_t *parent, char *s)
{
        struct var_t *child = eobject_child_l(parent, s);
        if (child)
                return do_childof_r(child, parent);
        else
                add_new_child(parent, s);
        return 0;
}

/*
 * helper to do_childof_r
 * last token is [
 */
static int
handle_lbrack(struct var_t *parent)
{
        struct index_info_t ii;
        eval_index(&ii);

        switch (parent->magic) {
        case QARRAY_MAGIC:
                if (ii.magic != QINT_MAGIC)
                        return ER_INDEX_TYPE;
                return do_childof_r(earray_child(parent, ii.i), parent);

        case QOBJECT_MAGIC:
                if (ii.magic == QINT_MAGIC) {
                        return do_childof_r(eobject_nth_child(parent, ii.i),
                                            parent);
                } else if (ii.magic == QSTRING_MAGIC) {
                        return handle_assoc_arr(parent, ii.s);
                }
                return ER_INDEX_TYPE;

        default:
                return ER_NOTASSOC;
        }
}

static void
assert_settable(struct var_t *v)
{
        if (isconst(v))
                syntax("You may not modify const '%s'", nameof(v));
}

static int
do_childof_r(struct var_t *v, struct var_t *parent)
{
        qlex();
        switch (cur_oc->t) {
        case OC_PER:
            {
                qlex();
                expect('u');
                if (v->magic == QOBJECT_MAGIC) {
                        struct var_t *child;
                        child = object_child_l(v, cur_oc->s);
                        if (child)
                                return do_childof_r(child, v);
                        else
                                add_new_child(v, cur_oc->s);
                } else {
                        do_childof_r(ebuiltin_method(v, cur_oc->s), v);
                }
                break;
            }
        case OC_LPAR:
                q_unlex();
                call_function(v, NULL, parent);
                return 0;
        case OC_LBRACK:
                 return handle_lbrack(v);

        case OC_EQ:
                assert_settable(v);
                eval(v);
                break;

        case OC_PLUSPLUS:
                assert_settable(v);
                qop_incr(v);
                break;

        case OC_MINUSMINUS:
                assert_settable(v);
                qop_decr(v);
                break;
        default:
                /* TODO: +=, -=, <<=, >>=, &=, |=, etc. */
                return ER_BADTOK;
        }
        return 0;
}

static void
do_childof(struct var_t *parent, unsigned int flags)
{
        int res = do_childof_r(parent, NULL);
        if (res) {
                switch (res) {
                case ER_INDEX_TYPE:
                        syntax("Invalid type for array index");
                case ER_BADTOK:
                        syntax("Invalid token at location");
                case ER_NOTASSOC:
                        syntax("Array syntax is not valid for type");
                default:
                        bug();
                }
        }

        if (!(flags & FE_FOR)) {
                qlex();
                expect(OC_SEMI);
        }
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

/*
 * Helper to seek_eob - skip (...)
 * @lpar: Current depth, usu. 1
 */
static void
skip_par(int lpar)
{
        seek_eob_1line_(lpar, 0, false);
}

static void
must_skip_par(void)
{
        qlex();
        expect(OC_LPAR);
        skip_par(1);
}

/**
 * seek_eob - Seek the end of the block
 * @depth: brace depth, should be zero if you're calling it from
 *         outside of expression.c
 */
void
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
                        must_skip_par();
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
                        must_skip_par();
                        qlex();
                        expect(OC_SEMI);
                        break;
                case OC_WHILE:
                        must_skip_par();
                        seek_eob(0);
                        break;
                case OC_FOR:
                        must_skip_par();
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
                /*
                 * peek special case: if just ';',
                 * cond will be QEMPTY_MAGIC, which
                 * normally evaluates to false.
                 */
                qlex();
                if (cur_oc->t == OC_SEMI) {
                        ret = true;
                        goto done;
                } else {
                        q_unlex();
                        eval(cond);
                        qlex();
                        expect(OC_SEMI);
                }
        }
        ret = !qop_cmpz(cond);
done:
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
        struct marker_t lr;
        PC_SAVE(&lr);
        ret = expression(retval, 0);
        PC_GOTO(&lr);
        return ret;
}

/* Declare automatic variable */
static int
do_let(struct var_t *unused, unsigned int flags)
{
        unsigned vf = 0;
        struct var_t *v;

        if (!!(flags & FE_FOR))
                syntax("'let' not allowed at this part of 'for' header");

        qlex();
        expect('u');

        /*
         * Make sure name is not same as other automatic vars.
         * If the symbol name already exists elsewhere in the namespace,
         * that's fine, but this will have precedence in future
         * symbol_seek calls until we leave this varible's scope.
         */
        if (symbol_seek_stack_l(cur_oc->s))
                syntax("Variable `%s' is already declared", cur_oc->s);

        v = stack_getpush();
        v->name = cur_oc->s;

        qlex();
        if (cur_oc->t == OC_CONST) {
                vf = VF_CONST;
                qlex();
        }

        switch (cur_oc->t) {
        case OC_SEMI:
                /* empty declaration, like "let x;" */
                if (vf == VF_CONST)
                        syntax("Empty constants are invalid");
                break;
        case OC_EQ:
                /* assign v with the "something" of "let x = something" */
                eval(v);
                v->flags = vf;
                qlex();
                expect(OC_SEMI);
                break;
        default:
                syntax("Invalid token");
        }
        return 0;
}

static int
do_if(struct var_t *retval, unsigned int unused)
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
do_while(struct var_t *retval, unsigned int unused)
{
        int r = 0;
        bool have_seekstart = false;
        struct marker_t seekstart;
        struct marker_t pc;
        PC_SAVE(&pc);
        while (get_condition(true)) {
                if (!have_seekstart)
                        PC_SAVE(&seekstart);
                have_seekstart = true;
                if ((r = expression(retval, 0)) != 0)
                        break;
                PC_GOTO(&pc);
        }
        if (have_seekstart)
                PC_GOTO(&seekstart);
        seek_eob(0);

        /* don't double break */
        if (r == 2)
                r = 0;
        return r;
}

static int
do_for(struct var_t *retval, unsigned int unused)
{
        struct marker_t start, pc_cond, pc_op, pc_blk;
        struct var_t *sp;
        int r = 0;
        bool have_op = false;

        PC_SAVE(&start);

        /*
         * Where to unwind stack from, *before* the initializer
         * expression of the `for' loop, so user can write
         *      for (let i = 0;...
         * without adding `i' to the containing function's
         * namespace
         */
        sp = q_.sp;

        qlex();
        expect(OC_LPAR);
        if (expression(NULL, 0) != 0)
                syntax("Unexpected break from 'for' loop header");

        PC_SAVE(&pc_cond);

        for (;;) {
                if (!get_condition(false))
                        break;

                if (!have_op) {
                        PC_SAVE(&pc_op);
                        skip_par(1);
                        have_op = true;
                        PC_SAVE(&pc_blk);
                } else {
                        PC_GOTO(&pc_blk);
                }

                if ((r = expression(retval, 0)) != 0)
                        break;

                PC_GOTO(&pc_op);

                if (expression(NULL, FE_FOR) != 0)
                        syntax("Unexpected break from loop header");

                PC_GOTO(&pc_cond);
        }

        PC_GOTO(&start);
        seek_eob(0);

        /*
         * This looks superfluous, but in the case of something like:
         *      "for (let i = 0; ..."
         * It keeps i out of the upper-level scope
         */
        while (q_.sp != sp)
                stack_pop(NULL);

        /* don't double break */
        if (r == 2)
                r = 0;
        return r;
}

/* he he... he he... "dodo" */
static int
do_do(struct var_t *retval, unsigned int unused)
{
        int r = 0;
        struct marker_t saved_pc;
        PC_SAVE(&saved_pc);
        for (;;) {
                if ((r = expression(retval, 0)) != 0)
                        break;
                qlex();
                expect(OC_WHILE);
                if (get_condition(true)) {
                        PC_GOTO(&saved_pc);
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

        /* if we break'd out of loop, don't double-break */
        if (r == 2)
                r = 0;
        return r;
}

static int
do_return(struct var_t *retval, unsigned int unused)
{
        qlex();
        if (cur_oc->t != OC_SEMI) {
                q_unlex();
                eval(retval);
                qlex();
                expect(OC_SEMI);
        }
        return 1;
}

static int
do_break(struct var_t *retval, unsigned int unused)
{
        qlex();
        expect(OC_SEMI);
        return 2;
}

static int
do_this(struct var_t *unused, unsigned int flags)
{
        do_childof(get_this(), flags);
        return 0;
}

static int
do_load(struct var_t *unused, unsigned int flags)
{
        const char *filename;
        if (!(flags & FE_TOP))
                syntax("Cannot load file except at top level execution");
        qlex();
        expect('q');
        filename = cur_oc->s;
        qlex();
        expect(OC_SEMI);
        load_file(filename);
        return 0;
}

/* when the first token of a statement is a keyword */
static int
do_keyword(struct var_t *retval, unsigned int flags)
{
        typedef int (*lufn_t)(struct var_t*, unsigned int);
        static const lufn_t lut[N_KW] = {
                NULL,           /*  vvv keywords start at 1 vvv */
                NULL,           /* KW_FUNC */
                do_let,         /* KW_LET */
                do_this,        /* KW_THIS */
                do_return,      /* KW_RETURN */
                do_break,       /* KW_BREAK */
                do_if,          /* KW_IF */
                do_while,       /* KW_WHILE */
                NULL,           /* KW_ELSE */
                do_do,          /* KW_DO */
                do_for,         /* KW_FOR */
                do_load,        /* KW_LOAD */
                NULL,           /* KW_CONST */
                NULL,           /* KW_PRIV */
        };

        unsigned int k = tok_keyword(cur_oc->t);
        if (k > N_KW || lut[k] == NULL)
                return -1;
        return lut[k](retval, flags);
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
        struct var_t *sp = NULL;
        int ret = 0;
        int brace = 0;

        RECURSION_INCR();

        if (!(flags & FE_TOP)) {
                qlex();
                if (cur_oc->t == OC_LBRACE) {
                        sp = q_.sp;
                        brace++;
                } else {
                        /* single line statement */
                        q_unlex();
                }
        }

        do {
                qlex();
                if (cur_oc->t == 'u') {
                        /*
                         * identifier, line is probably
                         * "this = that;" or "this();"
                         */
                        do_childof(esymbol_seek(cur_oc->s), flags);
                } else if (tok_type(cur_oc->t) == 'd') {
                        /* delimiter */
                        switch (cur_oc->t) {
                        case OC_SEMI:
                                /* empty statement */
                                break;
                        case OC_RBRACE:
                                if (!brace)
                                        syntax("Unexpected '}'");
                                brace--;
                                break;
                        case OC_RPAR:
                                /*
                                 * If FE_FOR, then this is the closing brace
                                 * of the for loop's header. Fake it by
                                 * setting "brace" to zero.
                                 */
                                if (!(flags & FE_FOR))
                                        goto bad_tok;
                                q_unlex();
                                brace = 0;
                                break;
                        case OC_LPAR:
                                /* Maybe it's an iffe */
                                q_unlex();
                                eval(NULL);
                                qlex();
                                expect(OC_SEMI);
                                break;
                        default:
                                goto bad_tok;
                        }
                } else if (tok_type(cur_oc->t) == 'k') {
                        ret = do_keyword(retval, flags);
                        if (ret) {
                                if (ret < 0)
                                        goto bad_tok;
                        }
                } else if (cur_oc->t == EOF) {
                        if (!(flags & FE_TOP))
                                syntax("Unexpected EOF");
                        ret = 3;
                } else {
                        goto bad_tok;
                }
        } while (brace && !ret);

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

        RECURSION_DECR();

        return ret;

bad_tok:
        syntax("Token '%s' not allowed here", cur_oc->s);
        return 0;
}


