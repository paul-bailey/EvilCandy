/*
 * builtin/math.c - Implementation of the __gbl__.Math built-in object
 */
#include <evilcandy.h>
#include <math.h>

static int
get_floatarg(Frame *fr, int argno, double *d)
{
        Object *x = vm_get_arg(fr, argno);
        bug_on(!x);
        if (isvar_float(x)) {
                *d = floatvar_tod(x);
        } else if (isvar_int(x)) {
                *d = (double)intvar_toll(x);
        } else {
                err_setstr(TypeError,
                           "Expected: integer or float but got %s",
                           typestr(x));
                return RES_ERROR;
        }
        return RES_OK;
}

static Object *
math_1arg(Frame *fr, double (*cb)(double))
{
        double x;
        if (get_floatarg(fr, 0, &x) == RES_ERROR)
                return ErrorVar;
        return floatvar_new(cb(x));
}

static Object *
math_2arg(Frame *fr, double (*cb)(double, double))
{
        double x, y;
        if (get_floatarg(fr, 0, &x) == RES_ERROR)
                return ErrorVar;
        if (get_floatarg(fr, 1, &y) == RES_ERROR)
                return ErrorVar;
        return floatvar_new(cb(x, y));
}

/*
 * This covers most of the functions in this library.
 */
#define MATHMETHOD_(func_, libfunc_, narg_) \
static Object * \
do_##func_(Frame *fr) \
{ \
        return math_##narg_##arg(fr, libfunc_); \
}

#define MATHMETHOD(func_, narg_) MATHMETHOD_(func_, func_, narg_)

MATHMETHOD(acos,  1)
MATHMETHOD(asin,  1)
MATHMETHOD(atan,  1)
MATHMETHOD(atan2, 2)
MATHMETHOD(acosh, 1)
MATHMETHOD(asinh, 1)
MATHMETHOD(atanh, 1)
MATHMETHOD(ceil,  1)
MATHMETHOD(cos,   1)
MATHMETHOD(cosh,  1)
MATHMETHOD(floor, 1)
MATHMETHOD(hypot, 2)
MATHMETHOD(pow,   2)
MATHMETHOD(sin,   1)
MATHMETHOD(sinh,  1)
MATHMETHOD(sqrt,  1)
MATHMETHOD(tan,   1)
MATHMETHOD(tanh,  1)


#define MATHTBL(func_, narg_) \
        V_INITTBL(#func_, do_##func_, narg_, narg_, -1, -1)

static const struct type_inittbl_t math_inittbl[] = {
        MATHTBL(acos,   1),
        MATHTBL(asin,   1),
        MATHTBL(atan,   1),
        MATHTBL(atan2,  2),
        MATHTBL(acosh,  1),
        MATHTBL(asinh,  1),
        MATHTBL(atanh,  1),
        MATHTBL(ceil,   1),
        MATHTBL(cos,    1),
        MATHTBL(cosh,   1),
        MATHTBL(floor,  1),
        MATHTBL(hypot,  2),
        MATHTBL(pow,    2),
        MATHTBL(sin,    1),
        MATHTBL(sinh,   1),
        MATHTBL(sqrt,   1),
        MATHTBL(tan,    1),
        MATHTBL(tanh,   1),
        /*
         * TODO: Support at least some of the following standard-C
         *       functions:
         *
         * isfinite
         * isinf
         * isnan
         * isnormal
         * signbit
         * copysign
         * nextafter
         * nearbyint
         * rint
         * round
         * trunc
         * fmod
         * remainder
         * remquo
         * fdim
         * fmax
         * fmin
         * fma
         * fabs
         * cbrt
         * exp
         * exp2
         * exp10
         * expm1
         * log
         * log2
         * log10
         * log1p
         * logb
         * ilogb
         * modf
         * frexp
         * ldexp
         * scalbn
         * tgamma
         * lgamma
         * j0
         * j1
         * jn
         * y0
         * y1
         * yn
         * erf
         * erfc
         */

        TBLEND,
};

static Object *
create_math_instance(Frame *fr)
{
        /*
         * TODO: Not just the methods, but also values like inf, nan, etc.,
         * which have no way of being expressed in the language.
         */
        return dictvar_from_methods(NULL, math_inittbl);
}

void
moduleinit_math(void)
{
        Object *k = stringvar_new("_math");
        Object *o = var_from_format("<xmM>",
                                    create_math_instance, 0, 0);
        dict_setitem(GlobalObject, k, o);
        VAR_DECR_REF(k);
        VAR_DECR_REF(o);
}


