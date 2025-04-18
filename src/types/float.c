#include <evilcandy.h>
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
float_str(struct var_t *a)
{
        char buf[25 + 17];
        double d = V2F(a)->f;
        int i, len;

        memset(buf, 0, sizeof(buf));
        len = snprintf(buf, sizeof(buf)-1, "%.17g", d);
        bug_on(len >= sizeof(buf));

        /*
         * We want '%g' instead of '%f' because for very large or very
         * small numbers, true precision will be lost in the expression.
         * We want '%g' instead of '%e' because guaranteeing scientific
         * notation is harder to read intuitively for small, say two- or
         * thee-digit numbers.
         *
         * The only pitfall of '%g' is that if nothing is beyond the
         * decimal, the result could be interpreted back as an integer.
         * So check if that's the case and add '.0' to the end if needed.
         */
        for (i = 0; i < len; i++) {
                if (buf[i] == '.' || buf[i] == 'e' || buf[i] == 'E')
                        break;
        }

        if (i == len) {
                bug_on(i >= sizeof(buf) - 2);
                buf[i++] = '.';
                buf[i++] = '0';
                buf[i++] = '\0';
                bug_on(i > sizeof(buf));
        }

        return stringvar_new(buf);
}

static struct var_t *
float_tostr(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        bug_on(!isvar_float(self));
        return float_str(self);
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
        .str    = float_str,
        .cmp    = float_cmp,
        .cmpz   = float_cmpz,
};

