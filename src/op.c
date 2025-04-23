/*
 * op.c - built-in methods for operators like + and -
 * FIXME: Half of this should be in var.c and half should be in vm.c
 */
#include <evilcandy.h>

/*
 * Return true if @a and @b are both numerical types.
 * If @a is an integer and @b is a float, swap them, so that the floating
 * point operations are used whenever one of two numbers are floats.
 */
static const struct operator_methods_t *
get_binop_method(struct var_t *a, struct var_t *b)
{
        if (isvar_float(a)) {
                if (isvar_int(b) || isvar_float(b))
                        return a->v_type->opm;
        } else if (isvar_int(a)) {
                if (isvar_float(b))
                        return b->v_type->opm;
                if (isvar_int(b))
                        return a->v_type->opm;
        }
        return NULL;
}

/*
 * used for operations where left and right values absolutely
 * must be an integer or a float.
 */
#define BINARY_OP_BASIC_FUNC(Field, What)               \
struct var_t *                                          \
qop_##Field (struct var_t *a, struct var_t *b)          \
{                                                       \
        const struct operator_methods_t *opm;           \
        if ((opm = get_binop_method(a, b)) == NULL      \
            || opm->Field == NULL) {                    \
                err_permit2(What, a, b);                \
                return NULL;                            \
        }                                               \
        bug_on(!opm->Field);                            \
        return opm->Field(a, b);                        \
}

BINARY_OP_BASIC_FUNC(pow, "**");
BINARY_OP_BASIC_FUNC(div, "/");
BINARY_OP_BASIC_FUNC(mod, "%");
BINARY_OP_BASIC_FUNC(sub, "-");
BINARY_OP_BASIC_FUNC(bit_and, "&");
BINARY_OP_BASIC_FUNC(xor, "^");
BINARY_OP_BASIC_FUNC(lshift, "<<");
BINARY_OP_BASIC_FUNC(rshift, ">>");


struct var_t *
qop_bit_or(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *opm;
        const struct map_methods_t *mpm;
        if ((opm = get_binop_method(a, b)) != NULL) {
                if (!opm->bit_or)
                        goto err;
                return opm->bit_or(a, b);
        }

        if (((mpm = a->v_type->mpm) != NULL)
            && mpm->mpunion && a->v_type == b->v_type) {
                return mpm->mpunion(a, b);
        }

err:
        err_permit2("|", a, b);
        return NULL;
}

struct var_t *
qop_add(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *opm;

        if ((opm = get_binop_method(a, b)) != NULL) {
                bug_on(!opm->add);
                return opm->add(a, b);
        }

        if (a->v_type->sqm) {
                const struct seq_methods_t *sq = a->v_type->sqm;
                if (!sq->cat || a->v_type != b->v_type)
                        goto cant;
                return sq->cat(a, b);
        }
        /* else, not '+'-ible */

cant:
        err_permit2("+", a, b);
        return NULL;
}

#define MAY_CAT(v_)     ((v_)->v_type->sqm && (v_)->v_type->sqm->cat)
struct var_t *
qop_mul(struct var_t *a, struct var_t *b)
{
        const struct operator_methods_t *opm;
        struct var_t *ret;
        long long i;
        binary_operator_t adder;

        if ((opm = get_binop_method(a, b)) != NULL) {
                /* fast path, number multiplication */
                bug_on(!opm->mul);
                return opm->mul(a, b);
        }

        if (MAY_CAT(a)) {
                struct var_t *tmp;
                if (!isvar_int(b))
                        goto cant;
                tmp = a;
                a = b;
                b = tmp;
        } else if (MAY_CAT(b)) {
                if (!isvar_int(a))
                        goto cant;
        }

        /*
         * XXX: should we sanity check huge multipliers, or let
         * user wait for an OOM crash?
         */
        adder = b->v_type->sqm->cat;
        i = intvar_toll(a);
        if (i <= 0)
                return adder(b, NULL);

        VAR_INCR_REF(b);
        ret = b;
        while (i > 1) {
                struct var_t *tmp = adder(b, ret);
                VAR_DECR_REF(ret);
                ret = tmp;
                i--;
        }
        return ret;

cant:
        err_permit2("*", a, b);
        return NULL;
}
#undef MAY_CAT

/* ~v */
struct var_t *
qop_bit_not(struct var_t *v)
{
        const struct operator_methods_t *p = v->v_type->opm;
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
        const struct operator_methods_t *p = v->v_type->opm;
        if (!p || !p->negate) {
                err_permit("-", v);
                return NULL;
        }
        return p->negate(v);
}


