#include "types_priv.h"
#include <math.h>

#define V2F(v)  ((struct floatvar_t *)(v))

static inline double
var2float(struct var_t *v)
{
        return isvar_int(v) ? intvar_toll(v) : V2F(v)->f;
}

struct var_t *
floatvar_new(double v)
{
        struct var_t *ret = var_new(&FloatType);
        V2F(ret)->f = v;
        return ret;
}

static struct var_t *
float_cp(struct var_t *f)
{
        return floatvar_new(V2F(f)->f);
}

static struct var_t *
float_mul(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("*");
                return NULL;
        }
        return floatvar_new(V2F(a)->f * var2float(b));
}

static struct var_t *
float_div(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("/");
                return NULL;
        }
        double f = var2float(b);
        /* don't accidentally divide by zero */
        if (fpclassify(f) != FP_NORMAL)
                return floatvar_new(0.);
        else
                return floatvar_new(V2F(a)->f / f);
}

static struct var_t *
float_add(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("+");
                return NULL;
        }
        return floatvar_new(V2F(a)->f + var2float(b));
}

static struct var_t *
float_sub(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("-");
                return NULL;
        }
        return floatvar_new(V2F(a)->f - var2float(b));
}

static int
float_cmp(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b))
                return -1;
        double f = var2float(b);
        return OP_CMP(V2F(a)->f, f);
}

static bool
float_cmpz(struct var_t *a)
{
        return fpclassify(V2F(a)->f) == FP_ZERO;
}

static struct var_t *
float_negate(struct var_t *a)
{
        return floatvar_new(-(V2F(a)->f));
}

static struct var_t *
float_tostr(struct vmframe_t *fr)
{
        char buf[64];
        ssize_t len;
        struct var_t *self = get_this(fr);
        bug_on(!isvar_float(self));

        len = snprintf(buf, sizeof(buf), "%.8g", V2F(self)->f);
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
        .negate         = float_negate,
};

struct type_t FloatType = {
        .name   = "float",
        .opm    = &float_primitives,
        .cbm    = float_methods,
        .size   = sizeof(struct floatvar_t),
        .cp     = float_cp,
        .cmp    = float_cmp,
        .cmpz   = float_cmpz,
};

