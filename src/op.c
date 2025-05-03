/*
 * op.c - built-in methods for operators like + and -
 * FIXME: Half of this should be in var.c and half should be in vm.c
 */
#include <evilcandy.h>

/*
 * Return proper methods if @a and @b are both numerical types.
 * return NULL otherwise.
 * Precedence is (high to low) complex, float, integer.
 */
static const struct operator_methods_t *
get_binop_method(Object *a, Object *b)
{
        struct type_t *at = a->v_type;
        struct type_t *bt = b->v_type;

        if (at->opm == NULL || bt->opm == NULL)
                return NULL;

        if (at == &ComplexType)
                return at->opm;
        if (bt == &ComplexType)
                return bt->opm;
        if (bt == &FloatType)
                return bt->opm;
        /* else, a is float or both are integers */
        return at->opm;
}

/*
 * For operations where left and right values absolutely must be a number.
 * (Some of these operators, however, have non-numerical meaning.)
 */
#define BINARY_OP_BASIC_FUNC(Field, What)               \
Object *                                                \
qop_##Field (Object *a, Object *b)                      \
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

BINARY_OP_BASIC_FUNC(pow, "**")
BINARY_OP_BASIC_FUNC(div, "/")
BINARY_OP_BASIC_FUNC(mod, "%")
BINARY_OP_BASIC_FUNC(sub, "-")
BINARY_OP_BASIC_FUNC(bit_and, "&")
BINARY_OP_BASIC_FUNC(xor, "^")
BINARY_OP_BASIC_FUNC(lshift, "<<")
BINARY_OP_BASIC_FUNC(rshift, ">>")


Object *
qop_bit_or(Object *a, Object *b)
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

Object *
qop_add(Object *a, Object *b)
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
Object *
qop_mul(Object *a, Object *b)
{
        const struct operator_methods_t *opm;
        Object *ret;
        long long i;
        binary_operator_t adder;

        if ((opm = get_binop_method(a, b)) != NULL) {
                /* fast path, number multiplication */
                bug_on(!opm->mul);
                return opm->mul(a, b);
        }

        if (MAY_CAT(a)) {
                Object *tmp;
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
                Object *tmp = adder(b, ret);
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
Object *
qop_bit_not(Object *v)
{
        const struct operator_methods_t *p = v->v_type->opm;
        if (!p || !p->bit_not) {
                err_permit("~", v);
                return NULL;
        }
        return p->bit_not(v);
}

/* -v */
Object *
qop_negate(Object *v)
{
        const struct operator_methods_t *p = v->v_type->opm;
        if (!p || !p->negate) {
                err_permit("-", v);
                return NULL;
        }
        return p->negate(v);
}


