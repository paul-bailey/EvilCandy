/*
 * complex.c - for complex numbers
 *
 * FIXME: Complex numbers in C are a mess, especially if you are using Gnu C.
 * Also, I recall from my egfractal project that standard C management of
 * complex numbers is somehow immensely slow compared to just writing the
 * functions myself.  Don't know why that is, since standard libraries tend
 * to be *faster* than anything I could write myself.  But complex numbers are
 * just special I guess.
 *
 * tl;dr... Replace code below so that it doesn't rely on <complex.h>
 */
#include <evilcandy.h>
#include <math.h>
#include <complex.h>

struct complexvar_t {
        Object base;
        complex double c;
};

#define V2C(v)  ((struct complexvar_t *)v)

#define COMPLEX(v_, c_) do {                    \
        if (isvar_complex(v_))                  \
                c_ = V2C(v_)->c;                \
        else if (isvar_float(v_))               \
                c_ = floatvar_tod(v_);          \
        else if (isvar_int(v_))                 \
                c_ = (double)intvar_toll(v_);   \
        else {                                  \
                c_ = 0.0 + 0.0*I;               \
                bug();                          \
        }                                       \
} while (0)

static inline Object *
ccomplexvar_new(complex double c)
{
        return complexvar_new(creal(c), cimag(c));
}

static Object *
complex_pow(Object *a, Object *b)
{
        complex double ca, cb;
        COMPLEX(a, ca);
        COMPLEX(b, cb);
        return ccomplexvar_new(cpow(ca, cb));
}

static Object *
complex_mul(Object *a, Object *b)
{
        complex double ca, cb;
        COMPLEX(a, ca);
        COMPLEX(b, cb);
        return ccomplexvar_new(ca * cb);
}

static Object *
complex_div(Object *a, Object *b)
{
        complex double ca, cb;
        COMPLEX(a, ca);
        COMPLEX(b, cb);
        if (creal(cb) == 0.0 && cimag(cb) == 0.0) {
                err_setstr(NumberError, "Divide by zero");
                return NULL;
        }
        return ccomplexvar_new(ca / cb);
}

static Object *
complex_add(Object *a, Object *b)
{
        complex double ca, cb;
        COMPLEX(a, ca);
        COMPLEX(b, cb);
        return ccomplexvar_new(ca + cb);
}

static Object *
complex_sub(Object *a, Object *b)
{
        complex double ca, cb;
        COMPLEX(a, ca);
        COMPLEX(b, cb);
        return ccomplexvar_new(ca - cb);
}

static Object *
complex_negate(Object *self)
{
        complex double c;
        COMPLEX(self, c);
        return ccomplexvar_new(c * -1.0);
}

static Object *
complex_abs(Object *self)
{
        complex double c;
        COMPLEX(self, c);
        return floatvar_new(cabs(c));
}

static Object *
complex_str(Object *self)
{
        struct complexvar_t *cv;
        double im, re;
        int sign;
        complex double c;

        bug_on(!isvar_complex(self));

        cv = V2C(self);
        c = cv->c;
        re = creal(c);
        im = cimag(c);
        if (im < 0) {
                im = -im;
                sign = '-';
        } else {
                sign = '+';
        }

        return stringvar_from_format("(%.17g%c%.17gj)", re, sign, im);
}

static enum result_t
complex_cmp(Object *a, Object *b, int *result)
{
        complex double ca, cb;
        COMPLEX(a, ca);
        COMPLEX(b, cb);

        if (ca == cb) {
                *result = 0;
                return RES_OK;
        }

        err_setstr(TypeError,
                   "comparison between complex numbers not permitted");
        return RES_ERROR;
}

static bool
complex_cmpeq(Object *a, Object *b)
{
        complex double ca, cb;
        COMPLEX(a, ca);
        COMPLEX(b, cb);

        return ca == cb;
}

static bool
complex_cmpz(Object *x)
{
        /* XXX: more appropriate to use hypot? */
        struct complexvar_t *cv = V2C(x);
        return creal(cv->c) == 0.0 && cimag(cv->c) == 0.0;
}

static Object *
complex_getreal(Object *self)
{
        struct complexvar_t *cv = V2C(self);
        bug_on(!isvar_complex(self));
        return floatvar_new(creal(cv->c));
}

static Object *
complex_getimag(Object *self)
{
        struct complexvar_t *cv = V2C(self);
        bug_on(!isvar_complex(self));
        return floatvar_new(cimag(cv->c));
}

static Object *
do_complex_conjugate(Frame *fr)
{
        Object *self = vm_get_this(fr);
        bug_on(!self || !isvar_complex(self));
        complex double c = V2C(self)->c;
        return complexvar_new(creal(c), -cimag(c));
}

static hash_t
calc_complex_hash(Object *self)
{
        complex double c;
        bug_on(!isvar_complex(self));
        c = V2C(self)->c;

        if (cimag(c) == 0.0)
                return double_hash(creal(c));
        return calc_object_hash_generic(self);
}

static Object *
complex_create(Frame *fr)
{
        double real, imag, real_a, imag_a;

        real = NAN;
        imag = NAN;
        real_a = NAN;
        imag_a = NAN;

        if (vm_getargs(fr, "[|ff!]{|ff}:complex",
                       &real_a, &imag_a, STRCONST_ID(real), &real,
                       STRCONST_ID(imag), &imag) == RES_ERROR) {
                /* Permit single-arg complex number */
                Object *arr = vm_get_arg(fr, 0);
                bug_on(!arr || !isvar_array(arr));
                if ((seqvar_size(arr)) == 1) {
                        Object *c = array_borrowitem(arr, 0);
                        if (isvar_complex(c)) {
                                err_clear();
                                return VAR_NEW_REF(c);
                        }
                }
                return ErrorVar;
        }

        if (isnan(real)) {
                real = isnan(real_a) ? 0.0 : real_a;
        } else if (!isnan(real_a)) {
                err_doublearg("real");
                return ErrorVar;
        }

        if (isnan(imag)) {
                imag = isnan(imag_a) ? 0.0 : imag_a;
        } else if (!isnan(imag_a)) {
                err_doublearg("imag");
                return ErrorVar;
        }

        return complexvar_new(real, imag);
}

static const struct type_prop_t complex_prop_getsets[] = {
        { .name = "real", .getprop = complex_getreal, .setprop = NULL },
        { .name = "imag", .getprop = complex_getimag, .setprop = NULL },
        { .name = NULL },
};

static const struct type_method_t complex_methods[] = {
        {"conjugate", do_complex_conjugate},
        {NULL, NULL},
};

static const struct operator_methods_t complex_primitives = {
        .pow    = complex_pow,
        .mul    = complex_mul,
        .div    = complex_div,
        .add    = complex_add,
        .sub    = complex_sub,
        .negate = complex_negate,
        .abs    = complex_abs,
};

struct type_t ComplexType = {
        .flags  = OBF_NUMBER,
        .name   = "complex",
        .opm    = &complex_primitives,
        .cbm    = complex_methods,
        .size   = sizeof(struct complexvar_t),
        .str    = complex_str,
        .cmp    = complex_cmp,
        .cmpz   = complex_cmpz,
        .cmpeq  = complex_cmpeq,
        .prop_getsets = complex_prop_getsets,
        .create = complex_create,
        .hash   = calc_complex_hash,
};

Object *
complexvar_new(double real, double imag)
{
        Object *ret = var_new(&ComplexType);
        struct complexvar_t *cv = V2C(ret);
        cv->c = (complex double)real + (complex double)imag * I;
        return ret;
}

