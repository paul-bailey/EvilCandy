#include "var.h"
#include <math.h>

static inline double
var2float_(struct var_t *v)
{
        return v->magic == TYPE_INT ? (double)v->i : v->f;
}

static inline double
var2float(struct var_t *v, const char *op)
{
        if (!isnumvar(v))
                syntax("Invalid/mismatched type for '%s' operator", op);
        return var2float_(v);
}

static struct var_t *
float_new(double v)
{
        struct var_t *ret = var_new();
        float_init(ret, v);
        return ret;
}

static struct var_t *
float_mul(struct var_t *a, struct var_t *b)
{
        return float_new(a->f * var2float(b, "*"));
}

static struct var_t *
float_div(struct var_t *a, struct var_t *b)
{
        double f = var2float(b, "/");
        /* XXX: Should have some way of logging error to user */
        if (fpclassify(f) != FP_NORMAL)
                return float_new(0.);
        else
                return float_new(a->f /= f);
}

static struct var_t *
float_add(struct var_t *a, struct var_t *b)
{
        return float_new(a->f + var2float(b, "+"));
}

static struct var_t *
float_sub(struct var_t *a, struct var_t *b)
{
        return float_new(a->f - var2float(b, "-"));
}

static int
float_cmp(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b))
                return -1;
        double f = var2float_(b);
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

static struct var_t *
float_negate(struct var_t *a)
{
        return float_new(-(a->f));
}

static void
float_mov(struct var_t *to, struct var_t *from)
{
        float_init(to, from->f);
}

static int
float_mov_strict(struct var_t *to, struct var_t *from)
{
        if (!isnumvar(from))
                return -1;
        to->f = var2float_(from);
        return 0;
}

static int
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
        return 0;
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
        .mov_strict     = float_mov_strict,
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
