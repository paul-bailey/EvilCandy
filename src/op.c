/*
 * op.c - built-in methods for operators like + and -
 * FIXME: Half of this should be in var.c and half should be in vm.c
 */
#include <evilcandy.h>

static inline const struct operator_methods_t *
primitives_of(struct var_t *v)
{
        return v->v_type->opm;
}

/**
 * assign a = a * b
 */
struct var_t *
qop_mul(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p || !p->mul) {
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
        if (!p || !p->div) {
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
        if (!p || !p->mod) {
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
        if (isnumvar(a)) {
                const struct operator_methods_t *op = a->v_type->opm;
                if (!op->add)
                        goto cant;
                return op->add(a, b);
        } else if (a->v_type->sqm) {
                const struct seq_methods_t *sq = a->v_type->sqm;
                if (!sq->cat || a->v_type != b->v_type)
                        goto cant;
                return sq->cat(a, b);
        }
        /* else, not '+'-ible */

cant:
        err_permit("+", a);
        return NULL;
}

/**
 * assign a = a - b
 */
struct var_t *
qop_sub(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p || !p->sub) {
                err_permit("-", a);
                return NULL;
        }
        return p->sub(a, b);
}

/* set a = a & b */
struct var_t *
qop_bit_and(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *p = primitives_of(a);
        if (!p || !p->bit_and) {
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
        if (!p || !p->bit_or) {
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
        if (!p || !p->xor) {
                err_permit("^", a);
                return NULL;
        }
        return p->xor(a, b);
}

/* ~v */
struct var_t *
qop_bit_not(struct var_t *v)
{
        const struct operator_methods_t *p = primitives_of(v);
        if (!p || !p->bit_not) {
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
        if (!p || !p->negate) {
                err_permit("-", v);
                return NULL;
        }
        return p->negate(v);
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
        if (!v->v_type->cmpz) {
                err_permit("cmpz", v);
                *status = RES_ERROR;
                return true;
        }
        *status = RES_OK;
        return v->v_type->cmpz(v);
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
        bug_on(!v->v_type->cp);
        return v->v_type->cp(v);
}


