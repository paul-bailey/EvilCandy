/*
 * op.c - built-in methods for operators like + and -
 * FIXME: Half of this should be in var.c and half should be in vm.c
 */
#include <evilcandy.h>
#include <internal/op.h>
#include <internal/types/number_types.h>

/*
 * Return proper number methods
 * For numbers, precedence is (high to low) complex, float, integer.
 * For non-matching non-numbers, return NULL; functions below will
 * decide whether this is an error or not.
 */
static const struct operator_methods_t *
get_binop_method(Object *a, Object *b)
{
        struct type_t *at = a->v_type;
        struct type_t *bt = b->v_type;

        if (at == bt)
                return at->opm;

        if (!isvar_number(a) || !isvar_number(b))
                return NULL;

        bug_on(!at->opm || !bt->opm);

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
                return ErrorVar;                        \
        }                                               \
        bug_on(!opm->Field);                            \
        return opm->Field(a, b);                        \
}

BINARY_OP_BASIC_FUNC(pow, "**")
BINARY_OP_BASIC_FUNC(div, "/")
BINARY_OP_BASIC_FUNC(sub, "-")
BINARY_OP_BASIC_FUNC(bit_and, "&")
BINARY_OP_BASIC_FUNC(xor, "^")
BINARY_OP_BASIC_FUNC(lshift, "<<")
BINARY_OP_BASIC_FUNC(rshift, ">>")
BINARY_OP_BASIC_FUNC(bit_or, "|")
BINARY_OP_BASIC_FUNC(add, "+")

Object *
qop_mod(Object *a, Object *b)
{
        const struct operator_methods_t *opm;

        if ((opm = get_binop_method(a, b)) != NULL) {
                if (opm->mod)
                        return opm->mod(a, b);
        }

        if (isvar_string(a)) {
                bug_on(!a->v_type->opm || !a->v_type->opm->mod);
                return a->v_type->opm->mod(a, b);
        }
        /* else, not '%'-ible */

        err_permit2("%", a, b);
        return ErrorVar;
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
                if (opm->mul)
                        return opm->mul(a, b);
        }

        /*
         * FIXME: Types which support below should have their own
         * 'mul' operator callbacks, which may be able to improve
         * the speed of these operations.
         */
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
        } else {
                goto cant;
        }

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
        return ErrorVar;
}
#undef MAY_CAT

/* ~v */
Object *
qop_bit_not(Object *v)
{
        const struct operator_methods_t *p = v->v_type->opm;
        if (!p || !p->bit_not) {
                err_permit("~", v);
                return ErrorVar;
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
                return ErrorVar;
        }
        return p->negate(v);
}


