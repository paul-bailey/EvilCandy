/* q-eval.c - Recursive descent parser for scripter */
#include "egq.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

/* && or || */
static bool
islogical(int t)
{
        return t == OC_OROR || t == OC_ANDAND;
}

/* & | ^ */
static bool
isbinary(int t)
{
        return t == OC_AND || t == OC_OR || t == OC_XOR;
}

static bool
iscmp(int t)
{
        return t == OC_EQEQ || t == OC_LEQ || t == OC_GEQ
                || t == OC_NEQ || t == OC_LT || t == OC_GT;
}

static bool
isshift(int t)
{
        return t == OC_LSHIFT || t == OC_RSHIFT;
}

static bool
isadd(int t)
{
        return t == OC_PLUS || t == OC_MINUS;
}

static bool
ismuldivmod(int t)
{
        return t == OC_MUL || t == OC_DIV || t == OC_MOD;
}

static void eval0(struct var_t *v);

/*
 * Helper to eval_atomic
 * got something like "v = function (a, b, c) { ..."
 */
static void
eval_atomic_function(struct var_t *v)
{
        int brace;

        bug_on(v->magic != QEMPTY_MAGIC);
        v->magic = QFUNCTION_MAGIC;
        qlex();
        expect(OC_LPAR);

        /*
         * PC is now at start of function call.
         * scan to end of function, first checking that
         * argument header is sane.
         */
        qop_mov(v, &q_.pc);
        /*
         * Set owner to "this", since we're declaring it.
         * Even if we're parsing an element of an object,
         * which could be a return value from a function,
         * we want our namespace to be in the current function
         * when returning to this.
         */
        v->fn.owner = q_.fp;
        do {
                qlex();
                if (cur_oc->t == OC_RPAR)
                        break; /* no args */
                expect('u');
                qlex();
        } while (cur_oc->t == OC_COMMA);
        expect(OC_RPAR);
        qlex();
        expect(OC_LBRACE);

        brace = 1;
        while (brace && cur_oc->t != EOF) {
                qlex();
                if (cur_oc->t == OC_LBRACE)
                        brace++;
                else if (cur_oc->t == OC_RBRACE)
                        brace--;
        }
        if (cur_oc->t == EOF)
                syntax("Unbalanced brace");
}

static void
eval_atomic_symbol(struct var_t *v)
{
        char *name = cur_oc->s;
        struct var_t *w = symbol_seek(name);
        if (!w)
                syntax("Symbol %s not found", name);
        qop_mov(v, w);
}

static void
eval_atomic_object(struct var_t *v)
{
        if (v->magic != QEMPTY_MAGIC)
                syntax("Cannot assign object to existing variable");
        object_from_empty(v);
        do {
                struct var_t *child;
                qlex();
                expect('u');
                child = var_new();
                child->name = literal(cur_oc->s);
                qlex();
                if (cur_oc->t != OC_COMMA) {
                        /* not declaring empty child */
                        expect(OC_COLON);
                        eval(child);
                }
                object_add_child(v, child);
                qlex();
        } while (cur_oc->t == OC_COMMA);
        expect(OC_RBRACE);
}

static void
eval_atomic_assign(struct var_t *v, struct opcode_t *oc)
{
        switch (oc->t) {
        case 'i':
                qop_assign_int(v, oc->i);
                break;
        case 'f':
                qop_assign_float(v, oc->f);
                break;
        case 'q':
                qop_assign_cstring(v, oc->s);
                break;
        default:
                bug();
        }
}

/* magic is one of "ifq" */
static void
eval_atomic_literal(struct var_t *v)
{
        struct opcode_t *oc = cur_oc;
        qlex();
        if (cur_oc->t == OC_PER) {
                struct var_t *method;
                struct var_t *w = tstack_getpush();
                eval_atomic_assign(w, oc);
                qlex();
                expect('u');
                method = ebuiltin_method(w, cur_oc->s);
                call_function(method, v, w);
                tstack_pop(NULL);
        } else {
                q_unlex();
                eval_atomic_assign(v, oc);
        }
}

static void
eval_atomic_array(struct var_t *v)
{
        bug_on(v->magic != QEMPTY_MAGIC);
        array_from_empty(v);
        qlex();
        if (cur_oc->t == OC_RBRACK) /* empty array */
                return;
        q_unlex();
        do {
                struct var_t *child = var_new();
                qlex();
                eval(child);
                qlex();
                array_add_child(v, child);
        } while(cur_oc->t == OC_COMMA);
        expect(OC_RBRACK);
}

/* find value of number, string, function, or object */
static void
eval_atomic(struct var_t *v)
{
        switch (cur_oc->t) {
        case 'u':
                eval_atomic_symbol(v);
                break;
        case 'i':
        case 'f':
        case 'q':
                eval_atomic_literal(v);
                break;
        case OC_FUNC:
                eval_atomic_function(v);
                break;
        case OC_LBRACK:
                eval_atomic_array(v);
                break;
        case OC_LBRACE:
                eval_atomic_object(v);
                break;
        case OC_THIS:
                qop_mov(v, get_this());
                break;
        default:
                /* TODO: OC_THIS */
                syntax("Cannot evaluate atomic expression '%s'", cur_oc->s);
        }

        qlex();
}

/* Process parenthesized expression */
static void
eval10(struct var_t *v)
{
        int t = cur_oc->t;
        switch (t) {
        case OC_LPAR:
                t = OC_RPAR;
                break;
        case OC_LBRACK:
                t = OC_RBRACK;
                break;
        default:
                /* Not parenthesized, carry on */
                eval_atomic(v);
                return;
        }

        qlex();
        eval0(v);
        expect(t);
        qlex();
}

/*
 * Helper to eval8.
 * We got [, expect something that evaluates to number,
 * then exepct ]
 */
static int
eval_index(void)
{
        struct var_t *v = tstack_getpush();
        long long i;
        eval(v);
        if (v->magic != QINT_MAGIC)
                syntax("Array index must evaluate to integer");
        i = v->i;
        if (i > INT_MAX || i < INT_MIN)
                syntax("Array index %lld unacceptably ginormous", i);
        tstack_pop(NULL);
        qlex();
        expect(OC_RBRACK);
        return (int)i;
}

static void
eval9(struct var_t *v)
{
        eval10(v);
        if (cur_oc->t == OC_LPAR &&
            (v->magic == QFUNCTION_MAGIC || v->magic == QINTL_MAGIC)) {
                /* it's a function call */
                struct var_t *fn;
                q_unlex();

                fn = tstack_getpush();
                qop_mov(fn, v);
                var_reset(v);
                call_function(fn, v, NULL);
                tstack_pop(NULL);
                qlex();
        }
}

/* check if we're descending a parent.child.granchild...
 * or parent['child']... or array[n] line.
 */
static void
eval8(struct var_t *v)
{
        int t;
        eval9(v);
        while ((t = cur_oc->t) == OC_PER || t == OC_LBRACK) {
                struct var_t *child = NULL;
                if (t == OC_PER) {
                        qlex();
                        expect('u');
                        if (v->magic != QOBJECT_MAGIC)
                                child = ebuiltin_method(v, cur_oc->s);
                        else
                                child = eobject_child(v, cur_oc->s);
                } else { /* t == OC_LBRACK */
                        switch (v->magic) {
                        case QOBJECT_MAGIC:
                                qlex();
                                expect('q');
                                child = eobject_child(v, cur_oc->s);
                                qlex();
                                expect(OC_RBRACK);
                                break;
                        case QARRAY_MAGIC:
                                child = earray_child(v, eval_index());
                                break;
                        case QSTRING_MAGIC:
                            {
                                int c = etoken_substr(&v->s, eval_index());
                                token_reset(&v->s);
                                token_putc(&v->s, c);
                                /*
                                 * special case: Substring is currently
                                 * not a saved struct var_t, so we cannot
                                 * get child. Skip the qop_mov... part.
                                 */
                                eval9(v);
                                continue;
                            }
                        default:
                                syntax("associate array syntax invalid for type %s",
                                       typestr(v->magic));
                        }
                }
                var_reset(v);
                qop_mov(v, child);
                eval9(v);
        }
}

/* process unary operators, left to right */
static void
eval7(struct var_t *v)
{
        /*
         * TODO: Support this.
         * things like ~ or ! before number,
         * '-' before number too, for negative value.
         */
        eval8(v);
}

/* multiply, divide, modulo, left to right */
static void
eval6(struct var_t *v)
{
        int t;

        eval7(v);
        while (ismuldivmod(t = cur_oc->t)) {
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
        while (isadd(t = cur_oc->t)) {
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
        while (isshift(t = cur_oc->t)) {
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
        while (iscmp(t = cur_oc->t)) {
                struct var_t *w = tstack_getpush();
                qlex();
                eval4(w);
                /*
                 * FIXME: Need sanity check.
                 * qop_cmp clobbers v.
                 * v SHOULD be an unassigned temporary
                 * variable, so this SHOULD be find.
                 */
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
        while (isbinary(t = cur_oc->t)) {
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
        while (islogical(t = cur_oc->t)) {
                struct var_t *w = tstack_getpush();
                qlex();
                eval2(w);

                if (t == OC_OROR)
                        qop_land(v, w);
                else
                        qop_lor(v, w);
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
 * @v: An empty, unattached variable to store result.
 */
void
eval(struct var_t *v)
{
        /* We probably have a 64kB stack irl, but let's be paranoid */
        enum { RECURSION_SAFETY = 256 };
        static int recursion = 0;

        if (recursion >= RECURSION_SAFETY)
                syntax("Excess expression recursion");
        ++recursion;

        qlex();
        eval0(v);
        q_unlex();

        --recursion;
}

void
eval_safe(struct var_t *v)
{
        struct var_t *w = tstack_getpush();
        eval(w);
        qop_mov(v, w);
        tstack_pop(NULL);
}

