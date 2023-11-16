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

#define EVAL_STACK_SIZE 8192
static struct qvar_t *eval_stack = NULL;
static struct qvar_t *eval_sp = NULL;
static struct qvar_t *eval_stack_top = NULL;

static struct qvar_t *
eval_push(void)
{
        struct qvar_t *ret;
        if (!eval_sp) {
                eval_stack = emalloc(EVAL_STACK_SIZE);
                eval_sp = eval_stack;
                eval_stack_top = eval_stack + EVAL_STACK_SIZE;
        } else if (eval_sp >= eval_stack_top) {
                qsyntax("Eval stack overrun");
        }
        ret = eval_sp;
        ++eval_sp;
        qvar_init(ret);
        return ret;
}

static void
eval_pop(struct qvar_t *v)
{
        /* XXX: arg is a sanity check, not actually needed */
        bug_on(v != (eval_sp - 1));
        bug_on(eval_sp <= eval_stack);

        qvar_reset(v);
        --eval_sp;
}

static bool
islogical(int t)
{
        return t == TO_DTOK(QD_OROR) || t == TO_DTOK(QD_ANDAND);
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
        return t == TO_DTOK(QD_LSHIFT) || t == TO_DTOK(QD_RSHIFT);
}

static bool
isadd(int t)
{
        return t == TO_DTOK(QD_PLUS) || t == TO_DTOK(QD_MINUS);
}

static bool
ismuldivmod(int t)
{
        return t == TO_DTOK(QD_MUL) ||
               t == TO_DTOK(QD_DIV) || t == TO_DTOK(QD_MOD);
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

        v->magic = QFUNCTION_MAGIC;
        qlex();
        if (q_.pc.px.oc->t != TO_DTOK(QD_LPAR))
                qerr_expected("(");

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
                if (q_.pc.px.oc->t != 'u')
                        qerr_expected("identifier");
                qlex();
        } while (q_.pc.px.oc->t == TO_DTOK(QD_COMMA));
        if (q_.pc.px.oc->t != TO_DTOK(QD_RPAR))
                qerr_expected(")");
        qlex();
        if (q_.pc.px.oc->t != TO_DTOK(QD_LBRACE))
                qerr_expected("{");

        brace = 1;
        while (brace && q_.pc.px.oc->t != EOF) {
                qlex();
                if (q_.pc.px.oc->t == TO_DTOK(QD_LBRACE))
                        brace++;
                else if (q_.pc.px.oc->t == TO_DTOK(QD_RBRACE))
                        brace--;
        }
        if (q_.pc.px.oc->t == EOF)
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
        char *name = q_.pc.px.oc->s;
        struct qvar_t *w = qsymbol_lookup(name);
        if (!w)
                qsyntax("symbol %s not found", name);
        switch (w->magic) {
        case QINTL_MAGIC:
        case QFUNCTION_MAGIC:
           {
                int t = qlex(); /* need to peek */
                q_unlex();
                if (t == TO_DTOK(QD_LPAR)) {
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

/* find value of number, string, function, or object */
static void
eval_atomic(struct qvar_t *v)
{
        /* TODO: Check type of @v before clobbering it! */
        switch (q_.pc.px.oc->t) {
        case 'u':
                eval_atomic_symbol(v);
                break;
        case 'i':
                qop_assign_int(v, q_.pc.px.oc->i);
                break;
        case 'f':
                qop_assign_float(v, q_.pc.px.oc->f);
                break;
        case 'q':
                qop_assign_cstring(v, q_.pc.px.oc->s);
                break;
        case TO_KTOK(KW_FUNC):
                eval_atomic_function(v);
                break;
        case TO_DTOK(QD_RPAR):
        case TO_DTOK(QD_RBRACK):
                /* XXX: Why is this ok? */
                break;
        case TO_DTOK(QD_LBRACE):
                /* TODO: Evaluate object */
                qsyntax("Evaluate object not supported yet");
                break;
        default:
                qsyntax("Cannot evaluate atomic expression toktype=%c/%d",
                        tok_type(q_.pc.px.oc->t), tok_delim(q_.pc.px.oc->t));
        }
        qlex();
}

/* Process parenthesized expression */
static void
eval8(struct qvar_t *v)
{
        int t = q_.pc.px.oc->t;
        switch (t) {
        case TO_DTOK(QD_LPAR):
                t = TO_DTOK(QD_RPAR);
                break;
        case TO_DTOK(QD_LBRACK):
                t = TO_DTOK(QD_RBRACK);
                break;
        default:
                /* Not parenthesized, carry on */
                eval_atomic(v);
                return;
        }

        qlex();
        eval0(v);
        if (q_.pc.px.oc->t != t) {
                if (t == TO_DTOK(QD_LPAR))
                        qerr_expected(")");
                else
                        qerr_expected("]");
        }
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
        while (ismuldivmod(t = q_.pc.px.oc->t)) {
                struct qvar_t *w = eval_push();
                qlex();
                eval7(w);
                switch (t) {
                case TO_DTOK(QD_MUL):
                        qop_mul(v, w);
                        break;
                case TO_DTOK(QD_DIV):
                        qop_div(v, w);
                        break;
                default:
                case TO_DTOK(QD_MOD):
                        qop_mod(v, w);
                        break;
                }
                eval_pop(w);
        }
}

/* add or subtract two terms, left to right */
static void
eval5(struct qvar_t *v)
{
        int t;

        eval6(v);
        while (isadd(t = q_.pc.px.oc->t)) {
                struct qvar_t *w = eval_push();
                qlex();
                eval6(w);
                if (t == TO_DTOK(QD_PLUS))
                        qop_add(v, w);
                else  /* minus */
                        qop_sub(v, w);
                eval_pop(w);
        }
}

/* process shift operation, left to right */
static void
eval4(struct qvar_t *v)
{
        int t;

        eval5(v);
        while (isshift(t = q_.pc.px.oc->t)) {
                struct qvar_t *w = eval_push();
                qlex();
                eval5(w);
                qop_shift(v, w, t);
                eval_pop(w);
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
        while (iscmp(t = q_.pc.px.oc->t)) {
                struct qvar_t *w = eval_push();
                qlex();
                eval4(w);
                /*
                 * sanity check.  qop_cmp clobbers v, which is fine if
                 * script was written correctly.
                 */
                bug_on(!(v >= eval_stack && v < eval_sp));
                qop_cmp(v, w, t);
                eval_pop(w);
        }
}

/* Process binary operators */
static void
eval2(struct qvar_t *v)
{
        int t;

        eval3(v);
        while (isbinary(t = q_.pc.px.oc->t)) {
                struct qvar_t *w = eval_push();
                qlex();
                eval3(w);

                switch (t) {
                case TO_DTOK(QD_AND):
                        qop_bit_and(v, w);
                        break;
                case TO_DTOK(QD_OR):
                        qop_bit_or(v, w);
                        break;
                default:
                case TO_DTOK(QD_XOR):
                        qop_xor(v, w);
                        break;
                }
                eval_pop(w);
        }
}

/* Process logical AND, OR, left to right */
static void
eval1(struct qvar_t *v)
{
        int t;

        eval2(v);
        while (islogical(t = q_.pc.px.oc->t)) {
                struct qvar_t *w = eval_push();
                qlex();
                eval2(w);

                if (t == TO_DTOK(QD_OROR))
                        qop_land(v, w);
                else
                        qop_lor(v, w);
                eval_pop(w);
        }
}

/* Assign op. */
static void
eval0(struct qvar_t *v)
{
        switch (q_.pc.px.oc->t) {
        case TO_DTOK(QD_MUL):
                qsyntax("Pointers not yet supported");
        case TO_DTOK(QD_PLUSPLUS):
                qsyntax("Pre-increment not yet supported");
        case TO_DTOK(QD_MINUSMINUS):
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

