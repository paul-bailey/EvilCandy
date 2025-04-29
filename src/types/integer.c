#include <evilcandy.h>

#define V2I(v)  ((struct intvar_t *)v)

#define BUGCHECK_TYPES(A, B) \
                bug_on(!isvar_int(a) || !isvar_int(b))

/*
 * FIXME: EvilCandy integers are documented as being 64 bits,
 * but there's no guarantee that 'long long' is.
 */
#define LL_SQUARE_LIMIT  (1ull << 32)

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
        return intvar_new(res);
}

static Object *
int_mul(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la * lb);
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
        return intvar_new(la / lb);
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
        return intvar_new(la % lb);
}

static Object *
int_add(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la + lb);
}

static Object *
int_sub(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la - lb);
}

static int
int_cmp(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return OP_CMP(la, lb);
}

static Object *
int_lshift(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la << lb);
}

static Object *
int_rshift(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la >> lb);
}

static Object *
int_bit_and(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la & lb);
}

static Object *
int_bit_or(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la | lb);
}

static Object *
int_xor(Object *a, Object *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la ^ lb);
}

static bool
int_cmpz(Object *a)
{
        return V2I(a)->i == 0LL;
}

static Object *
int_bit_not(Object *a)
{
        return intvar_new(~(V2I(a)->i));
}

static Object *
int_negate(Object *a)
{
        return intvar_new(-(V2I(a)->i));
}

static Object *
int_abs(Object *a)
{
        long long v = intvar_toll(a);
        if (v < 0)
                v = -v;
        return intvar_new(v);
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
int_tostr(Frame *fr)
{
        Object *self = get_this(fr);
        if (arg_type_check(self, &IntType) == RES_ERROR)
                return ErrorVar;
        return int_str(self);
}

Object *
intvar_new(long long initval)
{
        Object *ret = var_new(&IntType);
        V2I(ret)->i = initval;
        return ret;
}

static const struct type_inittbl_t int_methods[] = {
        V_INITTBL("tostr", int_tostr, 0, 0),
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
        .name   = "integer",
        .opm    = &int_primitives,
        .cbm    = int_methods,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct intvar_t),
        .str    = int_str,
        .cmpz   = int_cmpz,
        .cmp    = int_cmp,
};

