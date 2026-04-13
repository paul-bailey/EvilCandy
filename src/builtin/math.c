/*
 * builtin/math.c - Implementation of the __gbl__.Math built-in object
 */
#include <evilcandy.h>
#include <internal/types/number_types.h>
#include <internal/init.h>
#include <math.h>

static double
get_floatarg(Object *x)
{
        if (isvar_float(x)) {
                return floatvar_tod(x);
        } else {
                bug_on(!isvar_int(x));
                return (double)intvar_toll(x);
        }
}

static Object *
math_1arg(Frame *fr, double (*cb)(double))
{
        double x;
        Object *xo;
        if (vm_getargs(fr, "[<if>!]{!}", &xo) == RES_ERROR)
                return ErrorVar;
        x = get_floatarg(xo);
        return floatvar_new(cb(x));
}

static Object *
math_2arg(Frame *fr, double (*cb)(double, double))
{
        double x, y;
        Object *xo, *yo;
        if (vm_getargs(fr, "[<if><if>!]{!}", &xo, &yo) == RES_ERROR)
                return ErrorVar;
        x = get_floatarg(xo);
        y = get_floatarg(yo);
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


#define MATHTBL(func_) \
        {#func_, do_##func_}

static const struct type_method_t math_inittbl[] = {
        MATHTBL(acos),
        MATHTBL(asin),
        MATHTBL(atan),
        MATHTBL(atan2),
        MATHTBL(acosh),
        MATHTBL(asinh),
        MATHTBL(atanh),
        MATHTBL(ceil),
        MATHTBL(cos),
        MATHTBL(cosh),
        MATHTBL(floor),
        MATHTBL(hypot),
        MATHTBL(pow),
        MATHTBL(sin),
        MATHTBL(sinh),
        MATHTBL(sqrt),
        MATHTBL(tan),
        MATHTBL(tanh),
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

        {NULL, NULL},
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
        Object *k = stringvar_from_ascii("_math");
        Object *o = var_from_format("<xmM>",
                                    create_math_instance, 0, 0);
        dict_setitem(GlobalObject, k, o);
        VAR_DECR_REF(k);
        VAR_DECR_REF(o);
}


