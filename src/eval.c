/* q-eval.c - Recursive descent parser for scripter */
#include "egq.h"
#include <string.h>
#include <stdlib.h>

static unsigned char optbl[QD_NCODES];
static bool module_is_init = false;

enum {
        F_LOG = 0x01,
        F_BIN = 0x02,
        F_CMP = 0x04,
        F_SFT = 0x08,
};

static void
init_module(void)
{
        memset(optbl, 0, sizeof(optbl));
        optbl[QD_AND] = F_BIN;
        optbl[QD_OR]  = F_BIN;
        optbl[QD_XOR] = F_BIN;

        optbl[QD_EQEQ] = F_CMP;
        optbl[QD_LEQ]  = F_CMP;
        optbl[QD_GEQ]  = F_CMP;
        optbl[QD_NEQ]  = F_CMP;
        optbl[QD_LT]   = F_CMP;
        optbl[QD_GT]   = F_CMP;

        optbl[QD_OROR]   = F_LOG;
        optbl[QD_ANDAND] = F_LOG;

        optbl[QD_LSHIFT] = F_SFT;
        optbl[QD_RSHIFT] = F_SFT;
        module_is_init = true;
}

static bool
islogical(int t)
{
        return t == OC_OROR || t == OC_ANDAND;
}

static bool
isbinary(int t)
{
        return tok_type(t) == 'd' && !!(optbl[tok_delim(t)] & F_BIN);
}

static bool
iscmp(int t)
{
        return tok_type(t) == 'd' && !!(optbl[tok_delim(t)] & F_CMP);
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
        return t == OC_MUL ||
               t == OC_DIV || t == OC_MOD;
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

/*
 * helper to eval_atomic
 * look up symbol,
 *      If function followed by '(', call it.
 *      If function NOT followed by '(',
 *              assign function pointer to v.
 *      If variable, assign its value to v.
 */
static void
eval_atomic_symbol(struct qvar_t *v)
{
        char *name = cur_oc->s;
        struct qvar_t *w = qsymbol_lookup(name, 0);
        if (!w)
                qsyntax("symbol %s not found", name);
        switch (w->magic) {
        case QINTL_MAGIC:
        case QFUNCTION_MAGIC:
           {
                int t = qlex(); /* need to peek */
                q_unlex();
                if (t == OC_LPAR) {
                        /* It's a function call */
                        qcall_function(w, v);
                        return;
                }
                /* else, it's a variable assignment, fall through */
           }
        default:
                break;
        }
        qop_mov(v, w);
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

/* find value of number, string, function, or object */
static void
eval_atomic(struct qvar_t *v)
{
        /* TODO: Check type of @v before clobbering it! */
        switch (cur_oc->t) {
        case 'u':
                eval_atomic_symbol(v);
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
                eval_atomic_function(v);
                break;
        case OC_RPAR:
        case OC_RBRACK:
                /* XXX: Why is this ok? */
                break;
        case OC_LBRACE:
                eval_atomic_object(v);
                break;
        default:
                /* TODO: OC_THIS */
                qsyntax("Cannot evaluate atomic expression toktype=%c/%d",
                        tok_type(cur_oc->t), tok_delim(cur_oc->t));
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
                 * qop_cmp clobbers v, which is fine if
                 * script was written correctly.
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
        /* We probably have a 64kB stack, but let's be paranoid */
        enum { RECURSION_SAFETY = 256 };
        static int recursion = 0;

        if (recursion >= RECURSION_SAFETY)
                qsyntax("Excess expression recursion");
        ++recursion;

        if (!module_is_init)
                init_module();

        qlex();
        eval0(v);
        q_unlex();

        --recursion;
}

