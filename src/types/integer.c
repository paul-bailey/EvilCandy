#include <evilcandy.h>

#define V2I(v)  ((struct intvar_t *)v)

#define BUGCHECK_TYPES(A, B) \
                bug_on(!isvar_int(a) || !isvar_int(b))

/*
 * FIXME: EvilCandy integers are documented as being 64 bits,
 * but there's no guarantee that 'long long' is.
 */
#define LL_SQUARE_LIMIT  (1ull << 32)

static Object *
intvar_new__(long long x)
{
        return x ? intvar_new(x) : VAR_NEW_REF(gbl.zero);
}

/*
 * Algorithm taken straight from Wikipedia, "Exponentiation by squaring".
 * I C-ified and int-ified it and added some boundary checks.  I *assume*
 * this is faster than math.h's pow() function since I don't have to fuss
 * with floating point, but 99% of the time float.c's version--which does
 * use math.h--will get called anyway.
 */
long long
ipow(long long x, long long y)
{
        long long a, sign;

        if (y <= 0LL) {
                if (y == 0LL)
                        return 1LL;
                return 0LL;
        }

        if (x == 0LL || x == 1LL)
                return x;

        sign = 1LL;
        if (x < 0LL) {
                if (!!(y & 1LL))
                        sign = -1;
                x = -x;
        }

        /*
         * TODO: some testing...
         * The smallest x and biggest y possible (before an overflow
         * detection would stop this algorithm) is 2**63.  A brute force
         * algorithm, just "y--" and "x *= x0", would take at most 63
         * iterations, then.  The following algorithm takes at most 7
         * iterations.  However, this algorithm has much more going on
         * than brute force, and the normal scenario has a y set to
         * something much smaler than 63.  So I'm not 100% convinced
         * this is faster in most cases.
         */
        a = 1LL;
        while (y > 1LL) {
                if (!!(y & 1LL)) {
                         a = x * a;
                         y--;
                }
                y >>= 1LL;
                if (x >= LL_SQUARE_LIMIT)
                        goto err;
                x *= x;
        }

        if ((x * a) < x)
                goto err;

        return (x * a) * sign;

err:
        err_setstr(NumberError, "boundary error for ** operator");
        return 0;
}

static Object *
int_pow(Object *a, Object *b)
{
        long long la, lb, res;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);

        err_clear();
        res = ipow(la, lb);
        if (err_occurred())
                return NULL;
        return intvar_new__(res);
}

static Object *
int_mul(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new__(la * lb);
}

static Object *
int_div(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        if (lb == 0LL) {
                err_setstr(NumberError, "Divide by zero");
                return NULL;
        }
        return intvar_new__(la / lb);
}

static Object *
int_mod(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        if (lb == 0LL) {
                err_setstr(NumberError, "Modulo zero");
                return NULL;
        }
        return intvar_new__(la % lb);
}

static Object *
int_add(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new__(la + lb);
}

static Object *
int_sub(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new__(la - lb);
}

/*
 * cmp is not an .opm operation, so @b may be non-integer.
 * var_compare made sure not to call us for non-real numbers,
 * so assume @b is either float or int.
 */
static int
int_cmp(Object *a, Object *b)
{
        bug_on(!isvar_int(a) || !isvar_real(b));

        long long la = intvar_toll(a);
        if (isvar_int(b)) {
                long long lb = intvar_toll(b);
                return OP_CMP(la, lb);
        } else {
                /*
                 * b is float.  Don't convert b into int, or else
                 * we'd return zero for things like a=1 and b=1.1
                 */
                double fa = (double)la;
                double fb = floatvar_tod(b);
                return OP_CMP(fa, fb);
        }
}

static Object *
int_lshift(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new__(la << lb);
}

static Object *
int_rshift(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new__(la >> lb);
}

static Object *
int_bit_and(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new__(la & lb);
}

static Object *
int_bit_or(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new__(la | lb);
}

static Object *
int_xor(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new__(la ^ lb);
}

static bool
int_cmpz(Object *a)
{
        return V2I(a)->i == 0LL;
}

static Object *
int_bit_not(Object *a)
{
        return intvar_new__(~(V2I(a)->i));
}

static Object *
int_negate(Object *a)
{
        return intvar_new__(-(V2I(a)->i));
}

static Object *
int_abs(Object *a)
{
        long long v = intvar_toll(a);
        if (v < 0)
                v = -v;
        return intvar_new__(v);
}

static Object *
int_str(Object *v)
{
        char buf[64];
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "%lld", V2I(v)->i);
        return stringvar_new(buf);
}

static Object *
int_bit_length(Frame *fr)
{
        int count;
        unsigned long long ival;
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &IntType) == RES_ERROR)
                return ErrorVar;

        ival = intvar_toll(self);

        count = 0;
        while (ival) {
                count++;
                ival >>= 1;
        }
        return intvar_new__(count);
}

static Object *
int_bit_count(Frame *fr)
{
        int count;
        unsigned long long ival;
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &IntType) == RES_ERROR)
                return ErrorVar;

        ival = intvar_toll(self);
        count = bit_count64(ival);
        return intvar_new__(count);
}

static Object *
int_conjugate(Frame *fr)
{
        Object *self = vm_get_this(fr);
        bug_on(!self || !isvar_int(self));
        return VAR_NEW_REF(self);
}

static Object *
int_create(Frame *fr)
{
        size_t argc;
        Object *arg, *v, *b;

        arg = vm_get_arg(fr, 0);
        bug_on(!arg || !isvar_array(arg));
        argc = seqvar_size(arg);
        switch (argc) {
        case 0:
                return VAR_NEW_REF(gbl.zero);
        case 1:
                v = array_borrowitem(arg, 0);
                b = NULL;
                break;
        case 2:
                v = array_borrowitem(arg, 0);
                b = array_borrowitem(arg, 1);
                if (isvar_real(v)) {
                        err_setstr(TypeError,
                                "base argument invalid when converting type %s",
                                typestr(v));
                        return ErrorVar;
                }
                break;
        default:
                err_maxargs(argc, 2);
                return ErrorVar;
        }

        if (isvar_complex(v)) {
                err_setstr(TypeError,
                           "%s type invalid for int().  Did you mean abs()?",
                           typestr(v));
                return ErrorVar;
        } else if (isvar_int(v)) {
                return VAR_NEW_REF(v);
        } else if (isvar_float(v)) {
                return intvar_new((long long)floatvar_tod(v));
        } else if (isvar_string(v)) {
                int base;
                size_t pos;
                long long ival;

                if (b) {
                        if (!isvar_int(b)) {
                                err_setstr(TypeError,
                                        "base argument must be an integer");
                                return ErrorVar;
                        }
                        base = intvar_toi(b);
                        if (base < 2 || base > 36 || err_occurred()) {
                                err_clear();
                                err_setstr(ValueError,
                                           "Base argument %lld out of range",
                                           intvar_toll(b));
                                return ErrorVar;
                        }
                } else {
                        base = 0;
                }
                pos = string_slide(v, NULL, 0);
                if (string_toll(v, base, &pos, &ival) == RES_ERROR)
                        goto bad;
                if (string_slide(v, NULL, pos) != seqvar_size(v))
                        goto bad;
                return intvar_new(ival);

bad:
                err_setstr(ValueError,
                          "Cannot convert string '%s' base %d to int",
                          string_cstring(v), base);
                return ErrorVar;
        }

        err_setstr(TypeError,
                "Invalid type '%s' for int()", typestr(v));
        return ErrorVar;
}

Object *
intvar_new(long long initval)
{
        Object *ret = var_new(&IntType);
        V2I(ret)->i = initval;
        return ret;
}

/**
 * intvar_toi - Like intvar_toll, but set an exception if result
 *              does not fit in an integer
 * @v: A variable already confirmed to be an IntType var.
 * Return: Integer value of @v, or undefined if out of integer range.
 *      If err_occurred() is false, then return value is valid.
 */
int
intvar_toi(Object *v)
{
        long long lli = intvar_toll(v);
        if (lli < INT_MIN || lli > INT_MAX)
                err_setstr(ValueError, "Integer overflow");
        return (int)lli;
}

static const struct type_inittbl_t int_methods[] = {
        V_INITTBL("bit_length", int_bit_length, 0, 0, -1, -1),
        V_INITTBL("bit_count",  int_bit_count,  0, 0, -1, -1),
        V_INITTBL("conjugate",  int_conjugate,  0, 0, -1, -1),
        TBLEND,
};

static const struct operator_methods_t int_primitives = {
        .pow            = int_pow,
        .mul            = int_mul,
        .div            = int_div,
        .mod            = int_mod,
        .add            = int_add,
        .sub            = int_sub,
        .lshift         = int_lshift,
        .rshift         = int_rshift,
        .bit_and        = int_bit_and,
        .bit_or         = int_bit_or,
        .xor            = int_xor,
        .bit_not        = int_bit_not,
        .negate         = int_negate,
        .abs            = int_abs,
};

struct type_t IntType = {
        .flags  = OBF_NUMBER | OBF_REAL,
        .name   = "integer",
        .opm    = &int_primitives,
        .cbm    = int_methods,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct intvar_t),
        .str    = int_str,
        .cmpz   = int_cmpz,
        .cmp    = int_cmp,
        .create = int_create,
};

