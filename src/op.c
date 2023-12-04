/*
 * op.c - built-in methods for operators like + and -
 *
 * FIXME: Most of these should be in var.c and be called
 * "var_"-something.
 */
#include <evilcandy.h>
#include <typedefs.h>
#include <math.h>
#include <string.h>
/*
 * TODO: this for OC_* in qop_cmp, but should use IARG instead
 * (likely a faster switch statement because they're
 * sequential).
 */
#include "token.h"

static void
epermit(const char *op)
{
        syntax("%s operation not permitted for this type", op);
}

static inline const struct operator_methods_t *
primitives_of(struct var_t *v)
{
        bug_on(v->magic >= Q_NMAGIC);
        return TYPEDEFS[v->magic].opm;
}

/**
 * assign a = a * b
 */
void
qop_mul(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->mul)
                epermit("*");
        p->mul(a, b);
}

/**
 * assign a = a / b
 */
void
qop_div(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->div)
                epermit("/");
        p->div(a, b);
}

/**
 * assign a = a % b
 */
void
qop_mod(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->mod)
                epermit("%");
        p->mod(a, b);
}

/**
 * assign a = a + b
 */
void
qop_add(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->add)
                epermit("+");
        p->add(a, b);
}

/**
 * assign a = a - b
 */
void
qop_sub(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->sub)
                epermit("+");
        p->sub(a, b);
}

/**
 * qop_cmp - compare @a to @b and store result in @a
 * @op: An delimiter token indicating a comparison, e.g. OC_LT
 *
 * WARNING!! this re-casts @a, deleting what it had before.
 */
void
qop_cmp(struct var_t *a, struct var_t *b, int op)
{
        int ret, cmp;
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->cmp)
                epermit("cmp");
        cmp = p->cmp(a, b);
        switch (op) {
        case OC_EQEQ:
                ret = cmp == 0;
                break;
        case OC_LEQ:
                ret = cmp <= 0;
                break;
        case OC_GEQ:
                ret = cmp >= 0;
                break;
        case OC_NEQ:
                ret = cmp != 0;
                break;
        case OC_LT:
                ret = cmp < 0;
                break;
        case OC_GT:
                ret = cmp > 0;
                break;
        default:
                ret = 0;
                bug();
        }

        /* clobber @a and store ret in it */
        var_reset(a);
        integer_init(a, ret);
}

/**
 * qop_shift - left-shift value of @a by amount specified in @b
 *              and store result in @a
 * @op: Must be either OC_LSFHIT or OC_RSHIFT
 */
void
qop_shift(struct var_t *a, struct var_t *b, int op)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (op == OC_LSHIFT) {
                if (!p->lshift)
                        epermit("<<");
                p->lshift(a, b);
        } else {
                bug_on(op != OC_RSHIFT);
                if (!p->rshift)
                        epermit(">>");
                p->rshift(a, b);
        }
}

/* set a = a & b */
void
qop_bit_and(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->bit_and)
                epermit("&");
        p->bit_and(a, b);
}

/* set a = a | b */
void
qop_bit_or(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->bit_or)
                epermit("|");
        p->bit_or(a, b);
}

/* set a = a ^ b */
void
qop_xor(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->xor)
                epermit("^");
        p->xor(a, b);
}

/**
 * qop_cmpz - Compare @v to zero, NULL, or something like it
 *
 * Return: if @v is...
 *      empty:          true always
 *      integer:        true if zero
 *      float:          true if 0.0 exactly
 *      string:         true if null or even if "", false otherwise
 *      object:         false always, even if empty
 *      anything else:  false or error
 */
bool
qop_cmpz(struct var_t *v)
{
        const struct operator_methods_t *p = primitives_of(v);
        if (!p->cmpz)
                epermit("cmpz");
        return p->cmpz(v);
}

/* v++ */
void
qop_incr(struct var_t *v)
{
        const struct operator_methods_t *p = primitives_of(v);
        if (!p->incr)
                epermit("++");
        p->incr(v);
}

/* v-- */
void
qop_decr(struct var_t *v)
{
        const struct operator_methods_t *p = primitives_of(v);
        if (!p->decr)
                epermit("--");
        p->decr(v);
}

/* ~v */
void
qop_bit_not(struct var_t *v)
{
        const struct operator_methods_t *p = primitives_of(v);
        if (!p->bit_not)
                epermit("~");
        p->bit_not(v);
}

/* -v */
void
qop_negate(struct var_t *v)
{
        const struct operator_methods_t *p = primitives_of(v);
        if (!p->negate)
                epermit("-");
        p->negate(v);
}

/* !v WARNING! this clobbers v's type */
void
qop_lnot(struct var_t *v)
{
        bool cond = qop_cmpz(v);
        var_reset(v);
        integer_init(v, (int)cond);
}

/**
 * qop_mov - Assign @to with the contents of @from
 * @to: var getting assigned
 * @from: var reference for assignation.  This will not be modified.
 *
 * If @to isn't empty and its type is different from @from,
 * a syntax error will be thrown.
 *
 * If @to and @from are objects or arrays, they will end up both
 * containing the handle to the same object.
 */
struct var_t *
qop_mov(struct var_t *to, struct var_t *from)
{
        const struct operator_methods_t *p;
        if (from == to)
                return to;

        bug_on(!from || !to);
        if (to->magic == QEMPTY_MAGIC) {
                if (from->magic == QEMPTY_MAGIC) {
                        return to;
                } else if (from->magic == Q_STRPTR_MAGIC) {
                        string_init(to, from->strptr);
                        return to;
                }
                bug_on(isconst(to));
                p = primitives_of(from);
                bug_on(!p->mov);
                p->mov(to, from);
                /*
                 * XXX: don't set this here, the callbacks should call
                 * their own (type)_init function.  The idea is, we may
                 * wish to add more fields than a single magic number.
                 */
                to->magic = from->magic;
        } else {
                bug_on(from->magic == QEMPTY_MAGIC);
                p = primitives_of(to);
                bug_on(!p->mov);
                p->mov(to, from);
        }
        return to;
}

/**
 * like qop_mov, but if @to is an incompatible type,
 * it will be reset and clobbered.
 */
void
qop_clobber(struct var_t *to, struct var_t *from)
{
        var_reset(to);
        qop_mov(to, from);
}


