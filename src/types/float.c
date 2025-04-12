#include "types_priv.h"
#include <math.h>

static inline double
var2float(struct var_t *v)
{
        return isvar_int(v) ? (double)v->i : v->f;
}

struct var_t *
floatvar_new(double v)
{
        struct var_t *ret = var_new();
        ret->f = v;
        ret->v_type = &FloatType;
        return ret;
}

static struct var_t *
float_cp(struct var_t *f)
{
        return floatvar_new(f->f);
}

static struct var_t *
float_mul(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("*");
                return NULL;
        }
        return floatvar_new(a->f * var2float(b));
}

static struct var_t *
float_div(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("/");
                return NULL;
        }
        double f = var2float(b);
        /* XXX: Should have some way of logging error to user */
        if (fpclassify(f) != FP_NORMAL)
                return floatvar_new(0.);
        else
                return floatvar_new(a->f /= f);
}

static struct var_t *
float_add(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("+");
                return NULL;
        }
        return floatvar_new(a->f + var2float(b));
}

static struct var_t *
float_sub(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("-");
                return NULL;
        }
        return floatvar_new(a->f - var2float(b));
}

static int
float_cmp(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b))
                return -1;
        double f = var2float(b);
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
        return floatvar_new(-(a->f));
}

static struct var_t *
float_tostr(struct vmframe_t *fr)
{
        char buf[64];
        ssize_t len;
        struct var_t *self = get_this(fr);
        bug_on(!isvar_float(self));

        len = snprintf(buf, sizeof(buf), "%.8g", self->f);
        /* this should be impossible */
        bug_on(len >= sizeof(buf));
        (void)len; /* in case NDEBUG */

        return stringvar_new(buf);
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
        .cp             = float_cp,
};

struct type_t FloatType = {
        .name   = "float",
        .opm    = &float_primitives,
        .cbm    = float_methods,
};

