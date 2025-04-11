/*
 * op.c - built-in methods for operators like + and -
 * FIXME: Half of this should be in var.c and half should be in vm.c
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
        bool cond = qop_cmpz(v, &status);
        if (status)
                return NULL;
        return intvar_new((int)cond);
}

/**
 * qop_cp - Copy @v to a new variable
 * @v: variable to copy.  Its reference counter will not be consumed.
 *
 * Return: A duplicate of @v. This will have its own reference counter.
 *
 * Quirks:
 * - If @v is TYPE_STRPTR, the return value will be TYPE_STRING
 * - Dictionaries, lists, and functions are BY REFERENCE; the copies
 *   will be of pointers to the same resource.  Floats, integers, and
 *   strings are BY VALUE; the copies will contain duplicates of the
 *   originals' data.
 */
struct var_t *
qop_cp(struct var_t *v)
{
        const struct operator_methods_t *p = primitives_of(v);
        bug_on(!p);
        bug_on(!p->cp);
        return p->cp(v);
}


