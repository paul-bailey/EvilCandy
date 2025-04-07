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
econst(void)
{
        err_setstr(RuntimeError, "You may not assign a declared const");
}

static inline const struct operator_methods_t *
primitives_of(struct var_t *v)
{
        bug_on(v->magic >= NTYPES);
        return TYPEDEFS[v->magic].opm;
}

/**
 * assign a = a * b
 */
struct var_t *
qop_mul(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->mul) {
                err_permit("*", a);
                return NULL;
        }
        return p->mul(a, b);
}

/**
 * assign a = a / b
 */
struct var_t *
qop_div(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->div) {
                err_permit("/", a);
        }
        return p->div(a, b);
}

/**
 * assign a = a % b
 */
struct var_t *
qop_mod(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->mod) {
                err_permit("%", a);
                return NULL;
        }
        return p->mod(a, b);
}

/**
 * assign a = a + b
 */
struct var_t *
qop_add(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->add) {
                err_permit("+", a);
                return NULL;
        }
        return p->add(a, b);
}

/**
 * assign a = a - b
 */
struct var_t *
qop_sub(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->sub) {
                err_permit("-", a);
                return NULL;
        }
        return p->sub(a, b);
}

/**
 * qop_cmp - compare @a to @b and store result in @a
 * @op: An delimiter token indicating a comparison, e.g. OC_LT
 *
 * WARNING!! this re-casts @a, deleting what it had before.
 */
struct var_t *
qop_cmp(struct var_t *a, struct var_t *b, int op)
{
        int ret, cmp;
        struct var_t *v;
        const struct operator_methods_t *p;

        p = primitives_of(a);
        if (!p->cmp) {
                err_permit("cmp", a);
                return NULL;
        }
        cmp = p->cmp(a, b);

        /* TODO: Move this part below into a call wrapper in
         * vm.c, so the instruction-to-token translation doesn't
         * have to take place.
         */
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
        v = var_new();
        integer_init(v, ret);
        return v;
}

/**
 * qop_shift - left-shift value of @a by amount specified in @b
 *              and store result in @a
 * @op: Must be either OC_LSFHIT or OC_RSHIFT
 */
struct var_t *
qop_shift(struct var_t *a, struct var_t *b, int op)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (op == OC_LSHIFT) {
                if (!p->lshift) {
                        err_permit("<<", a);
                        return NULL;
                }
                return p->lshift(a, b);
        } else {
                bug_on(op != OC_RSHIFT);
                if (!p->rshift) {
                        err_permit(">>", a);
                        return NULL;
                }
                return p->rshift(a, b);
        }
}

/* set a = a & b */
struct var_t *
qop_bit_and(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->bit_and) {
                err_permit("&", a);
                return NULL;
        }
        return p->bit_and(a, b);
}

/* set a = a | b */
struct var_t *
qop_bit_or(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->bit_or) {
                err_permit("|", a);
                return NULL;
        }
        return p->bit_or(a, b);
}

/* set a = a ^ b */
struct var_t *
qop_xor(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p->xor) {
                err_permit("^", a);
                return NULL;
        }
        return p->xor(a, b);
}

/**
 * qop_cmpz - Compare @v to zero, NULL, or something like it
 * @status:  To be set to RES_ERROR if cmpz not permitted,
 *           RES_OK otherwise.  This may not be NULL.
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
qop_cmpz(struct var_t *v, enum result_t *status)
{
        const struct operator_methods_t *p = primitives_of(v);
        if (!p->cmpz) {
                err_permit("cmpz", v);
                *status = RES_ERROR;
                return true;
        }
        *status = RES_OK;
        return p->cmpz(v);
}

/* v++ */
enum result_t
qop_incr(struct var_t *v)
{
        const struct operator_methods_t *p = primitives_of(v);
        if (!p->incr) {
                err_permit("++", v);
                return RES_ERROR;
        }
        if (isconst(v)) {
                econst();
                return RES_ERROR;
        }
        p->incr(v);
        return RES_OK;
}

/* v-- */
enum result_t
qop_decr(struct var_t *v)
{
        const struct operator_methods_t *p = primitives_of(v);
        if (!p->decr) {
                err_permit("--", v);
                return RES_ERROR;
        }
        if (isconst(v)) {
                econst();
                return RES_ERROR;
        }
        p->decr(v);
        return RES_OK;
}

/* ~v */
struct var_t *
qop_bit_not(struct var_t *v)
{
        const struct operator_methods_t *p = primitives_of(v);
        if (!p->bit_not) {
                err_permit("~", v);
                return NULL;
        }
        return p->bit_not(v);
}

/* -v */
struct var_t *
qop_negate(struct var_t *v)
{
        const struct operator_methods_t *p = primitives_of(v);
        if (!p->negate) {
                err_permit("-", v);
                return NULL;
        }
        return p->negate(v);
}

struct var_t *
qop_lnot(struct var_t *v)
{
        int status;
        struct var_t *ret;
        bool cond = qop_cmpz(v, &status);
        if (status)
                return NULL;
        ret = var_new();
        integer_init(ret, (int)cond);
        return ret;
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
        /*
         * TODO: Need to get rid of MOV altogether.  It's a relic
         * of when the stack was an array of struct var_t's rather
         * than an array of pointers, and I didn't have a good GC
         * implementation.  That got confusing with
         *      "a <= a OPERATOR b"
         * when, with our new way of struct var_t allocation, it
         * should have been
         *      "new <= a OPERATOR b"
         * all along.
         */
        const struct operator_methods_t *p;
        if (from == to)
                return to;

        bug_on(!from || !to);
        bug_on((unsigned)from->magic >= NTYPES);
        bug_on((unsigned)to->magic >= NTYPES);

        if (to->magic == TYPE_EMPTY) {
                if (from->magic == TYPE_EMPTY)
                        return to;
                p = primitives_of(from);
                bug_on(!p->mov);
                p->mov(to, from);
        } else {
                bug_on(from->magic == TYPE_EMPTY);
                if (isconst(to)) {
                        econst();
                        return NULL;
                }
                p = primitives_of(to);
                if (p->mov_strict) {
                        if (p->mov_strict(to, from) == 0)
                                return to;
                }
                err_setstr(RuntimeError,
                           "ASSIGN not permited from %s => %s",
                           typestr(from), typestr(to));
                return NULL;
        }
        return to;
}

