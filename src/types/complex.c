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
        double im, re;
        int sign;
        complex double c = cv->c;
        bug_on(!isvar_complex(self));

        re = creal(c);
        im = cimag(c);
        if (im < 0) {
                im = -im;
                sign = '-';
        } else {
                sign = '+';
        }

        buffer_init(&b);
        buffer_printf(&b, "(%.17g%c%.17gj)", re, sign, im);
        return stringvar_from_buffer(&b);
}

static int
complex_cmp(Object *a, Object *b)
{
        complex double ca, cb;
        COMPLEX(a, ca);
        COMPLEX(b, cb);
        /*
         * "a < b" makes no sense with complex numbers, so sorting
         * won't be meaningul here.  However we still need this for
         * matching purposes.  Return 0 if they match, 1 otherwise,
         * regardless which is "bigger".
         */
        return !(creal(ca) == creal(cb) && cimag(ca) == cimag(cb));
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

static enum result_t
assert_arg_real(Object *x, const char *what)
{
        if (!isvar_real(x)) {
                err_notreal(typestr(x));
                return RES_ERROR;
        }
        return RES_OK;
}

static Object *
complex_create(Frame *fr)
{
        Object *args, *kwargs, *realarg, *imagarg, *ret;
        size_t argc;
        int nkw;
        double real, imag;

        args = vm_get_arg(fr, 0);
        kwargs = vm_get_arg(fr, 1);
        bug_on(!args || !isvar_array(args));
        bug_on(!kwargs || !isvar_dict(kwargs));

        /* guilty until proven innocent */
        ret = ErrorVar;

        dict_unpack(kwargs,
                STRCONST_ID(real),      &realarg,       NullVar,
                STRCONST_ID(imag),      &imagarg,       NullVar,
                NULL);

        nkw = 0;
        if (realarg != NullVar)
                nkw++;
        if (imagarg != NullVar)
                nkw++;

        argc = seqvar_size(args);
        if (argc + nkw > 2) {
                err_setstr(ArgumentError,
                           "Expected at most 2 args but got %d",
                           (int)(argc + nkw));
                goto out;
        }

        switch (argc) {
        default:
        case 0:
                break;
        case 1:
                if (realarg != NullVar) {
                        err_doublearg("real");
                        goto out;
                }
                realarg = array_getitem(args, 0);
                break;
        case 2:
                realarg = array_getitem(args, 0);
                imagarg = array_getitem(args, 1);
                break;
        }

        if (realarg == NullVar) {
                VAR_DECR_REF(realarg);
                realarg = VAR_NEW_REF(gbl.fzero);
        }

        if (isvar_complex(realarg)) {
                if (imagarg != NullVar) {
                        err_doublearg("imag");
                        goto out;
                }
                VAR_DECR_REF(imagarg);
                /* ref already produced */
                return realarg;
        }

        if (imagarg == NullVar) {
                VAR_DECR_REF(imagarg);
                imagarg = VAR_NEW_REF(gbl.fzero);
        }
        if (assert_arg_real(realarg, "real") == RES_ERROR)
                goto out;
        if (assert_arg_real(imagarg, "imag") == RES_ERROR)
                goto out;

        real = realvar_tod(realarg);
        imag = realvar_tod(imagarg);
        ret = complexvar_new(real, imag);
out:
        VAR_DECR_REF(realarg);
        VAR_DECR_REF(imagarg);
        return ret;
}

static const struct type_prop_t complex_prop_getsets[] = {
        { .name = "real", .getprop = complex_getreal, .setprop = NULL },
        { .name = "imag", .getprop = complex_getimag, .setprop = NULL },
        { .name = NULL },
};

static const struct type_inittbl_t complex_methods[] = {
        V_INITTBL("conjugate", do_complex_conjugate, 0, 0, -1, -1),
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
        .flags  = OBF_NUMBER,
        .name   = "complex",
        .opm    = &complex_primitives,
        .cbm    = complex_methods,
        .size   = sizeof(struct complexvar_t),
        .str    = complex_str,
        .cmp    = complex_cmp,
        .cmpz   = complex_cmpz,
        .prop_getsets = complex_prop_getsets,
        .create = complex_create,
};

Object *
complexvar_new(double real, double imag)
{
        Object *ret = var_new(&ComplexType);
        struct complexvar_t *cv = V2C(ret);
        cv->c = (complex double)real + (complex double)imag * I;
        return ret;
}

