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

BINARY_OP_BASIC_FUNC(mul, "*");
BINARY_OP_BASIC_FUNC(pow, "**");
BINARY_OP_BASIC_FUNC(div, "/");
BINARY_OP_BASIC_FUNC(mod, "%");

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

BINARY_OP_BASIC_FUNC(sub, "-");
BINARY_OP_BASIC_FUNC(bit_and, "&");
BINARY_OP_BASIC_FUNC(bit_or, "|");
BINARY_OP_BASIC_FUNC(xor, "^");

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


