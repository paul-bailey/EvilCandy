/* q-eval.c - Recursive descent parser for scripter */
#include "egq.h"
#include <string.h>
#include <stdlib.h>

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

static void eval0(struct qvar_t *v);

/*
 * Helper to eval_atomic
 * got something like "v = function (a, b, c) { ..."
 */
static void
eval_atomic_function(struct qvar_t *v)
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
                qsyntax("Unbalanced brace");
}

static void
eval_atomic_symbol(struct qvar_t *v)
{
        char *name = cur_oc->s;
        struct qvar_t *w = symbol_seek(name);
        if (!w)
                qsyntax("Symbol %s not found", name);
        symbol_walk(v, w, false);
}

static void
eval_atomic_object(struct qvar_t *v)
{
        if (v->magic != QEMPTY_MAGIC)
                qsyntax("Cannot assign object to existing variable");
        qobject_from_empty(v);
        do {
                struct qvar_t *child;
                qlex();
                expect('u');
                child = qvar_new();
                child->name = q_literal(cur_oc->s);
                qlex();
                if (cur_oc->t != OC_COMMA) {
                        /* not declaring empty child */
                        expect(OC_COLON);
                        q_eval(child);
                }
                qobject_add_child(v, child);
                qlex();
        } while (cur_oc->t == OC_COMMA);
        expect(OC_RBRACE);
}

static void
eval_atomic_assign(struct qvar_t *v, struct opcode_t *oc)
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
eval_atomic_literal(struct qvar_t *v)
{
        struct opcode_t *oc = cur_oc;
        qlex();
        if (cur_oc->t == OC_PER) {
                struct qvar_t *method;
                struct qvar_t *w = qstack_getpush();
                eval_atomic_assign(w, oc);
                qlex();
                expect('u');
                method = ebuiltin_method(w, cur_oc->s);
                call_function(method, v, w);
                qstack_pop(NULL);
        } else {
                q_unlex();
                eval_atomic_assign(v, oc);
        }
}

static void
eval_atomic_array(struct qvar_t *v)
{
        bug_on(v->magic != QEMPTY_MAGIC);
        qarray_from_empty(v);
        qlex();
        if (cur_oc->t == OC_RBRACK) /* empty array */
                return;
        q_unlex();
        do {
                struct qvar_t *child = qvar_new();
                qlex();
                q_eval(child);
                qlex();
                qarray_add_child(v, child);
        } while(cur_oc->t == OC_COMMA);
        expect(OC_RBRACK);
}

/* find value of number, string, function, or object */
static void
eval_atomic(struct qvar_t *v)
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
        case OC_RPAR:
        case OC_RBRACK:
                /* XXX: Why is this ok? */
                break;
        case OC_LBRACE:
                eval_atomic_object(v);
                break;
        case OC_THIS:
                symbol_walk(v, get_this(), false);
                break;
        default:
                /* TODO: OC_THIS */
                qsyntax("Cannot evaluate atomic expression '%s'", cur_oc->s);
        }

        qlex();
}

/* Process parenthesized expression */
static void
eval8(struct qvar_t *v)
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

/* process unary operators, left to right */
static void
eval7(struct qvar_t *v)
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
eval6(struct qvar_t *v)
{
        int t;

        eval7(v);
        while (ismuldivmod(t = cur_oc->t)) {
                struct qvar_t *w = qstack_getpush();
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
                qstack_pop(NULL);
        }
}

/* add or subtract two terms, left to right */
static void
eval5(struct qvar_t *v)
{
        int t;

        eval6(v);
        while (isadd(t = cur_oc->t)) {
                struct qvar_t *w = qstack_getpush();
                qlex();
                eval6(w);
                if (t == OC_PLUS)
                        qop_add(v, w);
                else  /* minus */
                        qop_sub(v, w);
                qstack_pop(NULL);
        }
}

/* process shift operation, left to right */
static void
eval4(struct qvar_t *v)
{
        int t;

        eval5(v);
        while (isshift(t = cur_oc->t)) {
                struct qvar_t *w = qstack_getpush();
                qlex();
                eval5(w);
                qop_shift(v, w, t);
                qstack_pop(NULL);
        }
}

/*
 * Process relational operators
 * The expression `v' will have its data type changed to `int'
 */
static void
eval3(struct qvar_t *v)
{
        int t;

        eval4(v);
        while (iscmp(t = cur_oc->t)) {
                struct qvar_t *w = qstack_getpush();
                qlex();
                eval4(w);
                /*
                 * FIXME: Need sanity check.
                 * qop_cmp clobbers v.
                 * v SHOULD be an unassigned temporary
                 * variable, so this SHOULD be find.
                 */
                qop_cmp(v, w, t);
                qstack_pop(NULL);
        }
}

/* Process binary operators */
static void
eval2(struct qvar_t *v)
{
        int t;

        eval3(v);
        while (isbinary(t = cur_oc->t)) {
                struct qvar_t *w = qstack_getpush();
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
                qstack_pop(NULL);
        }
}

/* Process logical AND, OR, left to right */
static void
eval1(struct qvar_t *v)
{
        int t;

        eval2(v);
        while (islogical(t = cur_oc->t)) {
                struct qvar_t *w = qstack_getpush();
                qlex();
                eval2(w);

                if (t == OC_OROR)
                        qop_land(v, w);
                else
                        qop_lor(v, w);
                qstack_pop(NULL);
        }
}

/* Assign op. */
static void
eval0(struct qvar_t *v)
{
        switch (cur_oc->t) {
        case OC_MUL:
                qsyntax("Pointers not yet supported");
        case OC_PLUSPLUS:
                qsyntax("Pre-increment not yet supported");
        case OC_MINUSMINUS:
                qsyntax("Pre-decrement not yet supported");
        default:
                break;
        }

        eval1(v);
}

/**
 * q_eval - Evaluate an expression
 * @v: An empty, unattached variable to store result.
 */
void
q_eval(struct qvar_t *v)
{
        /* We probably have a 64kB stack irl, but let's be paranoid */
        enum { RECURSION_SAFETY = 256 };
        static int recursion = 0;

        if (recursion >= RECURSION_SAFETY)
                qsyntax("Excess expression recursion");
        ++recursion;

        qlex();
        eval0(v);
        q_unlex();

        --recursion;
}

void
q_eval_safe(struct qvar_t *v)
{
        struct qvar_t *w = qstack_getpush();
        q_eval(w);
        qop_mov(v, w);
        qstack_pop(NULL);
}

