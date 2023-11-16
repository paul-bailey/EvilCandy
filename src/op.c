/* q-op.c - primitive operations on variables */
#include "egq.h"
#include <math.h>
#include <string.h>

static void
epermit(const char *op)
{
        qsyntax("%s operation not permitted for this type", op);
}

static void
emismatch(const char *op)
{
        qsyntax("cannot perform %s operation on mismatched types", op);
}

/**
 * assign a = a * b
 */
void
qop_mul(struct qvar_t *a, struct qvar_t *b)
{
        if (a->magic == QINT_MAGIC) {
                if (b->magic == QINT_MAGIC)
                        a->i *= b->i;
                else if (b->magic == QFLOAT_MAGIC)
                        a->i *= (long long)(b->f);
                else
                        goto er;
        } else if (a->magic == QFLOAT_MAGIC) {
                if (b->magic == QINT_MAGIC)
                        a->f *= (double)(b->i);
                else if (b->magic == QFLOAT_MAGIC)
                        a->f *= b->f;
                else
                        goto er;
        } else {
                goto er;
        }
        return;
er:
        epermit("'*'");
}

/**
 * assign a = a / b
 */
void
qop_div(struct qvar_t *a, struct qvar_t *b)
{
        if (a->magic == QINT_MAGIC) {
                if (b->magic == QINT_MAGIC)
                        a->i /= b->i;
                else if (b->magic == QFLOAT_MAGIC)
                        a->i /= (long long)b->f;
                else
                        goto er;
        } else if (a->magic == QFLOAT_MAGIC) {
                if (b->magic == QINT_MAGIC)
                        a->f /= (double)b->i;
                else if (b->magic == QFLOAT_MAGIC)
                        a->f /= b->f;
                else
                        goto er;
        } else {
                goto er;
        }
        return;
er:
        epermit("'/'");
}

/**
 * assign a = a % b
 */
void
qop_mod(struct qvar_t *a, struct qvar_t *b)
{
        if (a->magic == QINT_MAGIC) {
                if (b->magic == QINT_MAGIC)
                        a->i %= b->i;
                else if (b->magic == QFLOAT_MAGIC)
                        a->i %= (long long)b->f;
                else
                        goto er;
        } else if (a->magic == QFLOAT_MAGIC) {
                double bf;
                if (b->magic == QINT_MAGIC)
                        bf = (double)b->i;
                else if (b->magic == QFLOAT_MAGIC)
                        bf = b->f;
                else
                        goto er;
                a->f = fmod(a->f, bf);
        } else {
                goto er;
        }
        return;
er:
        epermit("'%'");
}

/**
 * assign a = a + b
 */
void
qop_add(struct qvar_t *a, struct qvar_t *b)
{
        /* TODO: If objects, maybe have @a inherit or append @b */
        if (a->magic == QSTRING_MAGIC) {
                /* '+' means 'concatenate' for strings */
                if (b->magic != QSTRING_MAGIC)
                        emismatch("'+'");
                token_puts(&a->s, b->s.s);
        } else if (a->magic == QINT_MAGIC) {
                if (b->magic == QINT_MAGIC)
                        a->i += b->i;
                else if (b->magic == QFLOAT_MAGIC)
                        a->i += (long long)b->f;
                else
                        goto er;
        } else if (a->magic == QFLOAT_MAGIC) {
                if (b->magic == QINT_MAGIC)
                        a->f += (double)b->i;
                else if (b->magic == QFLOAT_MAGIC)
                        a->f += b->f;
                else
                        goto er;
        } else {
                goto er;
        }
        return;

er:
        epermit("'+'");
}

/**
 * assign a = a - b
 */
void
qop_sub(struct qvar_t *a, struct qvar_t *b)
{
        if (a->magic == QINT_MAGIC) {
                if (b->magic == QINT_MAGIC)
                        a->i -= b->i;
                else if (b->magic == QFLOAT_MAGIC)
                        a->i -= (long long)b->f;
                else
                        goto er;
        } else if (a->magic == QFLOAT_MAGIC) {
                if (b->magic == QINT_MAGIC)
                        a->f -= (double)b->i;
                else if (b->magic == QFLOAT_MAGIC)
                        a->f -= b->f;
                else
                        goto er;
        } else {
                goto er;
        }
        return;
er:
        epermit("'-'");
}

/*
 * can't just be a-b, because if they're floats, a non-zero result
 * might cast to 0
 */
#define CMP(a_, b_) (a_ == b_ ? 0 : (a_ < b_ ? -1 : 1))

/* return <0 if a < b, >0 if a > b */
static int
cmp_helper(struct qvar_t *a, struct qvar_t *b)
{
        if (a->magic != b->magic) {
                if (a->magic == QINT_MAGIC) {
                        long long bi;
                        if (b->magic == QINT_MAGIC)
                                bi = b->i;
                        else if (b->magic == QFLOAT_MAGIC)
                                bi = (long long)b->f;
                        else
                                goto er;
                        return CMP(a->i, bi);
                } else if (a->magic == QFLOAT_MAGIC) {
                        double bf;
                        if (b->magic == QINT_MAGIC)
                                bf = (double)b->i;
                        else if (b->magic == QFLOAT_MAGIC)
                                bf = b->f;
                        else
                                goto er;
                        return CMP(a->f, bf);
                } else {
                        goto er;
                }
        }

        switch (a->magic) {
        case QFUNCTION_MAGIC:
                /* '==' means 'they point to the same execution code */
                if (!CMP(a->fn.mk.ns, b->fn.mk.ns))
                        return CMP(a->fn.mk.oc, b->fn.mk.oc);
                return false;
        case QFLOAT_MAGIC:
                return CMP(a->f, b->f);
        case QINT_MAGIC:
                return CMP(a->i, b->i);
        case QSTRING_MAGIC:
                /* '==' means 'they have the same text' */
                if (!a->s.s)
                        return b->s.s ? -1 : 1;
                else if (!b->s.s)
                        return 1;
                return strcmp(a->s.s, b->s.s);
        case QEMPTY_MAGIC:
                /* empty vars always equal each other */
                return 0;
        case QOBJECT_MAGIC:
                /*
                 * Different object vars are equal
                 * if they have the same handle
                 */
                return CMP(a->o.h, b->o.h);
        default:
                break;
        }

er:
        epermit("comparison");
        return 0;
}

/**
 * qop_cmp - compare @a to @b and store result in @a
 * @op: An delimiter token indicating a comparison, e.g. OC_LT
 *
 * WARNING!! this re-casts @a, deleting what it had before.
 */
void
qop_cmp(struct qvar_t *a, struct qvar_t *b, int op)
{
        int ret, cmp = cmp_helper(a, b);
        switch (op) {
        case OC_EQEQ:
                ret = cmp == 0;
        case OC_LEQ:
                ret = cmp <= 0;
        case OC_GEQ:
                ret = cmp >= 0;
        case OC_NEQ:
                ret = cmp != 0;
        case OC_LT:
                ret = cmp < 0;
        case OC_GT:
                ret = cmp > 0;
        default:
                ret = 0;
                bug();
        }

        /* clobber @a and store ret in it */
        qvar_reset(a);
        a->magic = QINT_MAGIC;
        a->i = ret;
}

/**
 * qop_shift - left-shift value of @a by amount specified in @b
 *              and store result in @a
 * @op: Must be either OC_LSFHIT or OC_RSHIFT
 */
void
qop_shift(struct qvar_t *a, struct qvar_t *b, int op)
{
        long long amt;

        bug_on(op != OC_LSHIFT && op != OC_RSHIFT);
        if (a->magic != QINT_MAGIC || b->magic != QINT_MAGIC)
                epermit(op == OC_LSHIFT ? "<<" : ">>");

        amt = b->i;

        if (amt >= 64) {
                a->i = 0;
        } else if (amt <= 0) {
                if (amt < 0)
                        qsyntax("Cannot shift by negative amount");
                /* else, don't change i */
                return;
        } else if (op == OC_LSHIFT) {
                a->i <<= (int)amt;
        } else {
                a->i >>= (int)amt;
        }
}

/* set a = a & b */
void
qop_bit_and(struct qvar_t *a, struct qvar_t *b)
{
        if (a->magic != QINT_MAGIC || b->magic != QINT_MAGIC)
                epermit("&");
        a->i &= b->i;
}

/* set a = a | b */
void
qop_bit_or(struct qvar_t *a, struct qvar_t *b)
{
        if (a->magic != QINT_MAGIC || b->magic != QINT_MAGIC)
                epermit("|");
        a->i |= b->i;
}

/* set a = a ^ b */
void
qop_xor(struct qvar_t *a, struct qvar_t *b)
{
        if (a->magic != QINT_MAGIC || b->magic != QINT_MAGIC)
                epermit("^");
        a->i ^= b->i;
}

/* set a = a && b */
void
qop_land(struct qvar_t *a, struct qvar_t *b)
{
        if (a->magic != QINT_MAGIC || b->magic != QINT_MAGIC)
                epermit("&&");
        a->i = !!a->i && !!b->i;
}

/* set a = a || b */
void
qop_lor(struct qvar_t *a, struct qvar_t *b)
{
        if (a->magic != QINT_MAGIC || b->magic != QINT_MAGIC)
                epermit("^");
        a->i = !!a->i || !!b->i;
}

/**
 * qop_cmpz - Compare @v to zero, NULL, or something like it
 *
 * Return: if @v is...
 *      empty:          true always
 *      integer:        true if zero
 *      float:          true if 0.0 exactly
 *      string:         true if null, false even if ""
 *      object:         false always, even if empty
 *      anything else:  false always
 */
bool
qop_cmpz(struct qvar_t *v)
{
        switch (v->magic) {
        case QFLOAT_MAGIC:
                return v->f == 0.0;
        case QINT_MAGIC:
                return v->i == 0LL;
        case QSTRING_MAGIC:
                return v->s.s == NULL;
        case QEMPTY_MAGIC:
                return true;
        default:
                return false;
        }
}

static void
type_err(struct qvar_t *v, int magic)
{
        qsyntax("You may not change variable %s from type %s to type %s",
                q_nameof(v), q_typestr(v->magic), q_typestr(magic));
}

/**
 * qop_mov - Assign @to with the contents of @from
 * @to: var getting assigned
 * @from: var reference for assignation.  This will not be modified.
 *
 * If @to isn't empty and its type is different from @from,
 * a syntax error will be thrown.
 *
 * If @to and @from are objects, they will both contain the handle to
 * the same object.  Use qvar_copy() for @to to be distinct from @from
 * FIXME: This will cause some jurisdictional problems if multiple
 * objects have handles to the same child object.
 */
void
qop_mov(struct qvar_t *to, struct qvar_t *from)
{
        /* Don't laugh. there are reasons this could happen. */
        if (from == to)
                return;

        if (to->magic == QEMPTY_MAGIC || to->magic == from->magic) {
                switch (from->magic) {
                case QOBJECT_MAGIC:
                        to->o.owner = NULL;
                        to->o.h = from->o.h;
                        to->o.h->nref++;
                        break;
                case QSTRING_MAGIC:
                        if (to->magic == QEMPTY_MAGIC)
                                token_init(&to->s);
                        else
                                token_reset(&to->s);
                        token_puts(&to->s, from->s.s);
                        break;
                case QFUNCTION_MAGIC:
                        to->fn.mk.oc = from->fn.mk.oc;
                        to->fn.mk.ns = from->fn.mk.ns;
                        /*
                         * Even if @to and @from are not the same
                         * instance, a function's owner is immutable
                         */
                        to->fn.owner = from->fn.owner;
                        break;
                case QINTL_MAGIC:
                        to->fni = from->fni;
                        break;
                case QPTRX_MAGIC:
                        to->px.ns = from->px.ns;
                        to->px.oc = from->px.oc;
                        break;
                case QINT_MAGIC:
                        to->i = from->i;
                        break;
                case QFLOAT_MAGIC:
                        to->f = from->f;
                        break;
                }
                to->magic = from->magic;
        } else if (to->magic == QPTRX_MAGIC
                   && from->magic == QFUNCTION_MAGIC) {
                to->px.ns = from->fn.mk.ns;
                to->px.oc = from->fn.mk.oc;
        } else if (to->magic == QFUNCTION_MAGIC
                   && from->magic == QPTRX_MAGIC) {
                to->fn.mk.ns = from->px.ns;
                to->fn.mk.oc = from->px.oc;
        } else if (to->magic == QINT_MAGIC) {
                if (from->magic == QINT_MAGIC)
                        to->i = from->i;
                else if (from->magic == QFLOAT_MAGIC)
                        to->i = (long long)from->f;
                else
                        goto er;
        } else if (to->magic == QFLOAT_MAGIC) {
                if (from->magic == QINT_MAGIC)
                        to->f = (double)from->i;
                else if (from->magic == QFLOAT_MAGIC)
                        to->f = from->f;
                else
                        goto er;
        } else {
                goto er;
        }
        return;
er:
        type_err(to, from->magic);
}

void
qop_assign_cstring(struct qvar_t *v, const char *s)
{
        if (v->magic == QEMPTY_MAGIC) {
                v->magic = QSTRING_MAGIC;
                token_init(&v->s);
        } else if (v->magic == QSTRING_MAGIC) {
                token_reset(&v->s);
        } else {
                type_err(v, QSTRING_MAGIC);
        }
        token_puts(&v->s, s);
}

void
qop_assign_int(struct qvar_t *v, long long i)
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

void
qop_assign_float(struct qvar_t *v, double f)
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



