#include <evilcandy.h>
#include <math.h>

#define V2F(v)  ((struct floatvar_t *)(v))

/*
 * 'bug' because code in var.c ought to have trapped
 * non-number before calling us.
 */
#define DOUBLE(v, d) do {                       \
        if (isvar_float(v))                     \
                d = floatvar_tod(v);            \
        else if (isvar_int(v))                  \
                d = (double)intvar_toll(v);     \
        else {                                  \
                d = 0.0; /* happy, compiler? */ \
                bug();                          \
        }                                       \
} while (0)

Object *
floatvar_new(double v)
{
        Object *ret = var_new(&FloatType);
        V2F(ret)->f = v;
        return ret;
}

static Object *
float_pow(Object *a, Object *b)
{
        double fa, fb;
        DOUBLE(a, fa);
        DOUBLE(b, fb);

        /*
         * Try to preven any 'nan' answers.
         *
         * Cody & Waite, _Software Manual for the Elementary Functions_
         * was shockingly unhelpful here.
         */
        if (fa < 0.0 && fb != floor(fb)) {
                bug_on(!ComplexType.opm || !ComplexType.opm->pow);
                return ComplexType.opm->pow(a, b);
        }

        if (fa == 0.0) {
                if (fb < 0.0) {
                        err_setstr(NumberError,
                                "0 ** Negative number would divide by zero");
                        return NULL;
                }
        }

        return floatvar_new(pow(fa, fb));
}

static Object *
float_mul(Object *a, Object *b)
{
        double fa, fb;
        DOUBLE(a, fa);
        DOUBLE(b, fb);
        return floatvar_new(fa * fb);
}

static Object *
float_div(Object *a, Object *b)
{
        double fa, fb;
        DOUBLE(a, fa);
        DOUBLE(b, fb);

        if (fb == 0.0) {
                err_setstr(NumberError, "Divide by zero");
                return NULL;
        }

        return floatvar_new(fa / fb);
}

static Object *
float_mod(Object *a, Object *b)
{
        double fa, fb;
        DOUBLE(a, fa);
        DOUBLE(b, fb);

        if (fb == 0.0) {
                err_setstr(NumberError, "Modulo by zero");
                return NULL;
        }

        return floatvar_new(fmod(fa, fb));
}

static Object *
float_add(Object *a, Object *b)
{
        double fa, fb;
        DOUBLE(a, fa);
        DOUBLE(b, fb);
        return floatvar_new(fa + fb);
}

static Object *
float_sub(Object *a, Object *b)
{
        double fa, fb;
        DOUBLE(a, fa);
        DOUBLE(b, fb);
        return floatvar_new(fa - fb);
}

static int
float_cmp(Object *a, Object *b)
{
        double fa, fb;
        DOUBLE(a, fa);
        DOUBLE(b, fb);
        /*
         * FIXME: double has a 52-bit mantissa (53 bits of precision),
         * so when comparing a double with any integer <= (1ull << 53),
         * we could get a false match.
         */
        return OP_CMP(fa, fb);
}

static bool
float_cmpz(Object *a)
{
        return fpclassify(V2F(a)->f) == FP_ZERO;
}

static Object *
float_negate(Object *a)
{
        return floatvar_new(-(V2F(a)->f));
}

static Object *
float_abs(Object *a)
{
        return floatvar_new(fabs(V2F(a)->f));
}

static Object *
float_str(Object *a)
{
        char buf[25 + 17];
        double d = V2F(a)->f;
        int i, len;

        memset(buf, 0, sizeof(buf));

        /* Handle some picky stuff first */
        if (isnan(d)) {
                strcpy(buf, "nan");
                goto done;
        } else if (isinf(d)) {
                double dab = fabs(d);
                if (dab == d)
                        strcpy(buf, "inf");
                else
                        strcpy(buf, "-inf");
                goto done;
        }

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
         * The '#' **should** take care of it for me, but I don't trust
         * that.
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

done:
        return stringvar_new(buf);
}

static Object *
float_conjugate(Frame *fr)
{
        Object *self = vm_get_this(fr);
        bug_on(!self || !isvar_float(self));
        return VAR_NEW_REF(self);
}

static Object *
float_create(Frame *fr)
{
        size_t argc;
        Object *args, *arg;

        args = vm_get_arg(fr, 0);
        bug_on(!isvar_array(args));
        argc = seqvar_size(args);
        if (argc > 1) {
                err_setstr(ArgumentError,
                           "Expected at most 1 arg, got %lu", seqvar_size(args));
                return ErrorVar;
        } else if (argc == 0) {
                return VAR_NEW_REF(gbl.fzero);
        }
        arg = array_getitem(args, 0);
        bug_on(!arg);
        VAR_DECR_REF(arg); /* just borrowing */

        if (isvar_float(arg)) {
                return VAR_NEW_REF(arg);
        } else if (isvar_int(arg)) {
                double d = (double)intvar_toll(arg);
                return floatvar_new(d);
        } else if (isvar_string(arg)) {
                size_t pos;
                double d;
                pos = string_slide(arg, NULL, 0);
                if (string_tod(arg, &pos, &d) == RES_ERROR)
                        goto badstring;
                if (string_slide(arg, NULL, pos) != seqvar_size(arg))
                        goto badstring;
                return floatvar_new(d);
        } else {
                err_setstr(TypeError,
                           "Expected real number or string but got '%s'",
                           typestr(arg));
                return ErrorVar;
        }

badstring:
        bug_on(!isvar_string(arg));
        err_setstr(ValueError, "Could not parse as float: '%s'",
                   string_cstring(arg));
        return ErrorVar;
}

static const struct type_inittbl_t float_methods[] = {
        V_INITTBL("conjugate", float_conjugate, 0, 0, -1, -1),
        TBLEND,
};

static const struct operator_methods_t float_primitives = {
        .pow            = float_pow,
        .mul            = float_mul,
        .div            = float_div,
        .mod            = float_mod,
        .add            = float_add,
        .sub            = float_sub,
        .negate         = float_negate,
        .abs            = float_abs,
};

struct type_t FloatType = {
        .flags  = OBF_NUMBER | OBF_REAL,
        .name   = "float",
        .opm    = &float_primitives,
        .cbm    = float_methods,
        .size   = sizeof(struct floatvar_t),
        .str    = float_str,
        .cmp    = float_cmp,
        .cmpz   = float_cmpz,
        .create = float_create,
};

