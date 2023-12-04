#include "var.h"
#include <math.h>

static inline double
var2float(struct var_t *v, const char *op)
{
        if (!isnumvar(v))
                syntax("Invalid/mismatched type for operation");
        return v->magic == TYPE_INT ? (double)v->i : v->f;
}

static void
float_mul(struct var_t *a, struct var_t *b)
{
        a->f *= var2float(b, "*");
}

static void
float_div(struct var_t *a, struct var_t *b)
{
        double f = var2float(b, "/");
        /* XXX: Should have some way of logging error to user */
        if (fpclassify(f) != FP_NORMAL)
                a->f = 0.;
        else
                a->f /= f;
}

static void
float_add(struct var_t *a, struct var_t *b)
{
        a->f += var2float(b, "+");
}

static void
float_sub(struct var_t *a, struct var_t *b)
{
        a->f -= var2float(b, "-");
}

static int
float_cmp(struct var_t *a, struct var_t *b)
{
        double f = var2float(b, "cmp");
        return OP_CMP(a->f, f);
}

static bool
float_cmpz(struct var_t *a)
{
        return fpclassify(a->f) == FP_ZERO;
}

static void
float_incr(struct var_t *a)
{
        a->f += 1.0;
}

static void
float_decr(struct var_t *a)
{
        a->f -= 1.0;
}

static void
float_negate(struct var_t *a)
{
        a->f = -(a->f);
}

static void
float_mov(struct var_t *to, struct var_t *from)
{
        to->f = var2float(from, "mov");
}

static void
float_tostr(struct var_t *ret)
{
        char buf[64];
        ssize_t len;
        struct var_t *self = get_this();
        bug_on(self->magic != TYPE_FLOAT);

        len = snprintf(buf, sizeof(buf), "%.8g", self->f);
        /* this should be impossible */
        bug_on(len >= sizeof(buf));
        (void)len; /* in case NDEBUG */

        string_init(ret, buf);
}

static const struct type_inittbl_t float_methods[] = {
        V_INITTBL("tostr", float_tostr, 0, 0),
        TBLEND,
};

static const struct operator_methods_t float_primitives = {
        .mul            = float_mul,
        .div            = float_div,
        .add            = float_add,
        .sub            = float_sub,
        .cmp            = float_cmp,
        .cmpz           = float_cmpz,
        .incr           = float_incr,
        .decr           = float_decr,
        .negate         = float_negate,
        .mov            = float_mov,
};

struct var_t *
float_init(struct var_t *v, double f)
{
        bug_on(v->magic != TYPE_EMPTY);
        v->f = f;
        v->magic = TYPE_FLOAT;
        return v;
}

void
typedefinit_float(void)
{
        var_config_type(TYPE_FLOAT, "float",
                        &float_primitives, float_methods);
}
