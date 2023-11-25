/* op.c - built-in methods for operators like + and - */
#include "egq.h"
#include <math.h>
#include <string.h>

static void
type_err(struct var_t *v, int magic)
{
        syntax("You may not change variable %s from type %s to type %s",
                nameof(v), typestr(v->magic), typestr(magic));
}

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
        a->magic = QINT_MAGIC;
        a->i = ret;
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
        qop_assign_int(v, (int)cond);
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
void
qop_mov(struct var_t *to, struct var_t *from)
{
        const struct operator_methods_t *p;
        if (from == to)
                return;

        bug_on(!from || !to);
        bug_on(from->magic == QEMPTY_MAGIC);
        if (to->magic == QEMPTY_MAGIC) {
                bug_on(isconst(to));
                p = primitives_of(from);
                bug_on(!p->mov);
                p->mov(to, from);
                to->magic = from->magic;
        } else {
                p = primitives_of(to);
                bug_on(!p->mov);
                p->mov(to, from);
        }
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

/* helper to qop_assign_cstring and qop_assign_char */
static void
qop_string_maybeinit(struct var_t *v)
{
        if (v->magic == QEMPTY_MAGIC) {
                string_init(v);
        } else if (v->magic != QSTRING_MAGIC) {
                type_err(v, QSTRING_MAGIC);
        }
}

/**
 * Convert @v to string if it's empty type, and assign the C string @s to
 * it
 */
void
qop_assign_cstring(struct var_t *v, const char *s)
{
        qop_string_maybeinit(v);
        string_assign_cstring(v, s);
}

/**
 * Convert @v to string if it's empty type, and assign the C string to be
 * a single character length, character @c
 */
void
qop_assign_char(struct var_t *v, int c)
{
        qop_string_maybeinit(v);
        string_clear(v);
        string_putc(v, c);
}

/**
 * Convert @v to int if it's empty type, and assign @i to it (cast to a
 * double in the case that @v is float type)
 */
void
qop_assign_int(struct var_t *v, long long i)
{
        if (v->magic == QEMPTY_MAGIC)
                v->magic = QINT_MAGIC;

        if (v->magic == QINT_MAGIC)
                v->i = i;
        else if (v->magic == QFLOAT_MAGIC)
                v->f = (long long)i;
        else
                type_err(v, QINT_MAGIC);
}

/**
 * Convert @v to float if it's empty type, and assign @f to it (cast to
 * a long long in the case that @v is an integer
 */
void
qop_assign_float(struct var_t *v, double f)
{
        if (v->magic == QEMPTY_MAGIC)
                v->magic = QFLOAT_MAGIC;

        if (v->magic == QINT_MAGIC)
                v->i = (long long)f;
        else if (v->magic == QFLOAT_MAGIC)
                v->f = f;
        else
                type_err(v, QFLOAT_MAGIC);
}


