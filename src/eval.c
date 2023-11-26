/* q-eval.c - Recursive descent parser for scripter */
#include "egq.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

enum tok_class_t {
        /*
         * Note, some of these tokens have multiple uses
         * depending on where they lie in the evaluation,
         * so these need to be flags instead of a simple
         * translation table.
         */
        T_LOGICAL       = 0x01, /* && or || */
        T_BINARY        = 0x02, /* & | ^ */
        T_COMPARISON    = 0x04, /* ==, <=, >=, !=, <, > */
        T_SHIFT         = 0x08, /* <<, >> */
        T_ADDSUBTRACT   = 0x10, /* + - */
        T_UNARY         = 0x20, /* ~ - ! + */
        T_MULDIVMOD     = 0x40, /* * / % */
        T_INDIRECT      = 0x80, /* . [ ( */
};

/* set up in moduleinit_eval */
static enum tok_class_t tokflag_tbl[QD_NCODES];

/* Used to determine evaluation class of token. */
static inline bool
istokflag(int t, enum tok_class_t flg)
{
        return tok_type(t) == 'd' && !!(tokflag_tbl[tok_delim(t)] & flg);
}

static void eval0(struct var_t *v);

/* find value of number, string, function, or object */
static void
eval_atomic(struct var_t *v)
{
        switch (cur_oc->t) {
        case 'u':
                qop_mov(v, esymbol_seek(cur_oc->s));
                break;
        case 'i':
                qop_assign_int(v, cur_oc->i);
                break;
        case 'f':
                qop_assign_float(v, cur_oc->f);
                break;
        case 'q':
                qop_assign_cstring(v, cur_oc->s);
                break;
        case OC_FUNC:
                compile_function(v);
                break;
        case OC_LBRACK:
                compile_array(v);
                break;
        case OC_LBRACE:
                compile_object(v);
                break;
        case OC_THIS:
                qop_mov(v, get_this());
                break;
        default:
                syntax("Cannot evaluate atomic expression '%s'", cur_oc->s);
        }

        qlex();
}

/* Process parenthesized expression */
static void
eval9(struct var_t *v)
{
        int t = cur_oc->t;
        if (t == OC_LPAR) {
                qlex();
                eval0(v);
                expect(OC_RPAR);
                qlex();
        } else {
                /* Not parenthesized, carry on */
                eval_atomic(v);
        }
}

static bool
clobbershift(struct var_t *v1, struct var_t *v2, struct var_t *v3)
{
        qop_clobber(v1, v2);
        qop_clobber(v2, v3);
        return true;
}

/*
 * Process indirection:
 *  v followed by '.' ... v is parent of parent.child
 *  v followed by '[' ... v is parent of parent['child'] or array[n]
 *  v followed by '(' ... v is function to get a return value of
 */
static void
eval8(struct var_t *v)
{
        /*
         * FIXME: This would be so much cleaner were it not that
         * owner needs to be faked when calling built-in methods,
         * since they do not have copies each with different
         * ownership pointers.
         */
        bool have_parent = false;
        struct var_t *parent = tstack_getpush();

        eval9(v);
        while (istokflag(cur_oc->t, T_INDIRECT)) {
                struct var_t *w;
                switch (cur_oc->t) {
                case OC_PER:
                        qlex();
                        expect('u');
                        if (v->magic != QOBJECT_MAGIC)
                                w = ebuiltin_method(v, cur_oc->s);
                        else
                                w = eobject_child(v, cur_oc->s);
                        clobbershift(parent, v, w);
                        have_parent = true;
                        break;
                case OC_LBRACK:
                    {
                        struct index_info_t ii;
                        eval_index(&ii);
                        switch (v->magic) {
                        case QOBJECT_MAGIC:
                                if (ii.magic == QSTRING_MAGIC)
                                        w = eobject_child_l(v, ii.s);
                                else
                                        w = eobject_nth_child(v, ii.i);
                                clobbershift(parent, v, w);
                                break;
                        case QSTRING_MAGIC:
                                if (ii.magic != QINT_MAGIC)
                                        syntax("Invalid type for array index");
                                qop_clobber(parent, v);
                                var_reset(v);
                                qop_assign_char(v, string_substr(parent, ii.i));
                                break;
                        case QARRAY_MAGIC:
                                if (ii.magic != QINT_MAGIC)
                                        syntax("Invalid type for array index");
                                w = earray_child(v, ii.i);
                                clobbershift(parent, v, w);
                                break;
                        default:
                                syntax("Associative array syntax invalid for type %s",
                                       typestr(v->magic));
                        }
                        have_parent = true;
                        break;
                    }
                case OC_LPAR:
                        if (!isfunction(v))
                                syntax("%s is not a function", nameof(v));
                        w = tstack_getpush();
                        q_unlex();
                        call_function(v, w, have_parent ? parent : NULL);
                        qop_clobber(v, w);
                        tstack_pop(NULL);
                        have_parent = false;
                        break;
                default:
                        goto done;
                }
                qlex();
        }
done:
        /* pop parent */
        tstack_pop(NULL);
}

/* process unary operators, left to right */
static void
eval7(struct var_t *v)
{
        int t;
        if (istokflag(t = cur_oc->t, T_UNARY)) {
                qlex();
                eval8(v);
                switch (t) {
                case OC_TILDE:
                        qop_bit_not(v);
                        break;
                case OC_MINUS:
                        qop_negate(v);
                        break;
                case OC_EXCLAIM:
                        qop_lnot(v);
                        break;
                case OC_PLUS:
                        /*
                         * TODO: I don't need to check this,
                         * but that means I'm allowing user to
                         * express things like +"string"
                         */
                        break;
                default:
                        bug();
                }
        } else {
                eval8(v);
        }
}

/* multiply, divide, modulo, left to right */
static void
eval6(struct var_t *v)
{
        int t;

        eval7(v);
        while (istokflag(t = cur_oc->t, T_MULDIVMOD)) {
                struct var_t *w = tstack_getpush();
                qlex();
                eval7(w);
                switch (t) {
                case OC_MUL:
                        qop_mul(v, w);
                        break;
                case OC_DIV:
                        qop_div(v, w);
                        break;
                default:
                case OC_MOD:
                        qop_mod(v, w);
                        break;
                }
                tstack_pop(NULL);
        }
}

/* add or subtract two terms, left to right */
static void
eval5(struct var_t *v)
{
        int t;

        eval6(v);
        while (istokflag(t = cur_oc->t, T_ADDSUBTRACT)) {
                struct var_t *w = tstack_getpush();
                qlex();
                eval6(w);
                if (t == OC_PLUS)
                        qop_add(v, w);
                else  /* minus */
                        qop_sub(v, w);
                tstack_pop(NULL);
        }
}

/* process shift operation, left to right */
static void
eval4(struct var_t *v)
{
        int t;

        eval5(v);
        while (istokflag(t = cur_oc->t, T_SHIFT)) {
                struct var_t *w = tstack_getpush();
                qlex();
                eval5(w);
                qop_shift(v, w, t);
                tstack_pop(NULL);
        }
}

/*
 * Process relational operators
 * The expression `v' will have its data type changed to `int'
 */
static void
eval3(struct var_t *v)
{
        int t;

        eval4(v);
        while (istokflag(t = cur_oc->t, T_COMPARISON)) {
                struct var_t *w = tstack_getpush();
                qlex();
                eval4(w);
                qop_cmp(v, w, t);
                tstack_pop(NULL);
        }
}

/* Process binary operators */
static void
eval2(struct var_t *v)
{
        int t;

        eval3(v);
        while (istokflag(t = cur_oc->t, T_BINARY)) {
                struct var_t *w = tstack_getpush();
                qlex();
                eval3(w);

                switch (t) {
                case OC_AND:
                        qop_bit_and(v, w);
                        break;
                case OC_OR:
                        qop_bit_or(v, w);
                        break;
                default:
                case OC_XOR:
                        qop_xor(v, w);
                        break;
                }
                tstack_pop(NULL);
        }
}

/* Process logical AND, OR, left to right */
static void
eval1(struct var_t *v)
{
        int t;

        eval2(v);
        while (istokflag(t = cur_oc->t, T_LOGICAL)) {
                struct var_t *w = tstack_getpush();
                int res;
                qlex();
                eval2(w);

                if (t == OC_OROR)
                        res = !qop_cmpz(v) || !qop_cmpz(w);
                else
                        res = !qop_cmpz(v) && !qop_cmpz(w);
                var_reset(v);
                qop_assign_int(v, res);
                tstack_pop(NULL);
        }
}

/* Assign op. */
static void
eval0(struct var_t *v)
{
        switch (cur_oc->t) {
        case OC_MUL:
                syntax("Pointers not yet supported");
        case OC_PLUSPLUS:
                syntax("Pre-increment not yet supported");
        case OC_MINUSMINUS:
                syntax("Pre-decrement not yet supported");
        default:
                break;
        }

        eval1(v);
}

/**
 * eval - Evaluate an expression
 * @v:  An variable to store result, or NULL to throw away the result.
 *      If not NULL, it must be either empty or a type that may receive
 *      the result, or user-error will be assumed and  a syntax error
 *      will be thrown.
 */
void
eval(struct var_t *v)
{
        struct var_t *w;

        RECURSION_INCR();

        qlex();
        w = tstack_getpush();
        eval0(w);
        if (v)
                qop_mov(v, w);
        tstack_pop(NULL);
        q_unlex();

        RECURSION_DECR();
}

/**
 * eval_index - Evaluate an array index
 * @ii: pointer to struct to store results
 *
 * Before function call PC is at first token after opening bracket.
 * After function call PC is at first token after closing bracket.
 */
void
eval_index(struct index_info_t *ii)
{
        struct var_t *idx = tstack_getpush();
        ii->s = NULL;
        ii->i = 0;
        eval(idx);
        qlex();
        expect(OC_RBRACK);
        switch (ii->magic = idx->magic) {
        case QSTRING_MAGIC:
                /* eliteral because this should evaluate to something
                 * already be known
                 */
                ii->s = eliteral(string_get_cstring(idx));
                ii->i = 0;
                break;
        case QINT_MAGIC:
                ii->s = NULL;
                /* because idx stores long long, but ii.i is int */
                if (idx->i < INT_MIN || idx->i > INT_MAX)
                        syntax("Array index out of range");
                ii->i = idx->i;
                break;
        default:
                syntax("Invalid type for array index");
        }
        tstack_pop(NULL);
}

void
moduleinit_eval(void)
{
        int t;
        for (t = 0; t < QD_NCODES; t++) {
                if (t == QD_OROR || t == QD_ANDAND)
                        tokflag_tbl[t] |= T_LOGICAL;
                if (t == QD_AND || t == QD_OR || t == QD_XOR)
                        tokflag_tbl[t] |= T_BINARY;
                if (t == QD_EQEQ || t == QD_LEQ || t == QD_GEQ
                    || t == QD_NEQ || t == QD_LT || t == QD_GT) {
                        tokflag_tbl[t] |= T_COMPARISON;
                }
                if (t == QD_LSHIFT || t == QD_RSHIFT)
                        tokflag_tbl[t] |= T_SHIFT;
                if (t == QD_PLUS || t == QD_MINUS)
                        tokflag_tbl[t] |= T_ADDSUBTRACT;
                if (t == QD_TILDE || t == QD_MINUS
                    || t == QD_EXCLAIM || t == QD_PLUS) {
                        tokflag_tbl[t] |= T_UNARY;
                }
                if (t == QD_MUL || t == QD_DIV || t == QD_MOD)
                        tokflag_tbl[t] |= T_MULDIVMOD;
                if (t == QD_LPAR || t == QD_LBRACK || t == QD_PER)
                        tokflag_tbl[t] |= T_INDIRECT;
        }
}

