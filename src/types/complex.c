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
        struct complexvar_t *cv = V2C(self);
        struct buffer_t b;
        complex double c = cv->c;
        bug_on(!isvar_complex(self));

        buffer_init(&b);
        /* XXX: types/float.c and this should call the same format func */
        buffer_printf(&b, "%.17g + %.17gj", creal(c), cimag(c));
        return stringvar_from_buffer(&b);
}

static int
complex_cmp(Object *a, Object *b)
{
        complex double ca, cb;
        COMPLEX(a, ca);
        COMPLEX(b, cb);
        return creal(ca) == creal(cb) && cimag(ca) == cimag(cb);
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

static const struct type_prop_t complex_prop_getsets[] = {
        { .name = "real", .getprop = complex_getreal, .setprop = NULL },
        { .name = "imag", .getprop = complex_getimag, .setprop = NULL },
        { .name = NULL },
};

static const struct type_inittbl_t complex_methods[] = {
        TBLEND,
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
        .name   = "complex",
        .opm    = &complex_primitives,
        .cbm    = complex_methods,
        .size   = sizeof(struct complexvar_t),
        .str    = complex_str,
        .cmp    = complex_cmp,
        .cmpz   = complex_cmpz,
        .prop_getsets = complex_prop_getsets,
};

Object *
complexvar_new(double real, double imag)
{
        Object *ret = var_new(&ComplexType);
        struct complexvar_t *cv = V2C(ret);
        cv->c = (complex double)real + (complex double)imag * I;
        return ret;
}

