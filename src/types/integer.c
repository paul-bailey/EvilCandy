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
        err_setstr(RuntimeError, "boundary error for ** operator");
        return 0;
}

static struct var_t *
int_pow(struct var_t *a, struct var_t *b)
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

static struct var_t *
int_mul(struct var_t *a, struct var_t *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la * lb);
}

static struct var_t *
int_div(struct var_t *a, struct var_t *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        if (lb == 0LL) {
                err_setstr(RuntimeError, "Divide by zero");
                return NULL;
        }
        return intvar_new(la / lb);
}

static struct var_t *
int_mod(struct var_t *a, struct var_t *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        if (lb == 0LL) {
                err_setstr(RuntimeError, "Modulo zero");
                return NULL;
        }
        return intvar_new(la % lb);
}

static struct var_t *
int_add(struct var_t *a, struct var_t *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la + lb);
}

static struct var_t *
int_sub(struct var_t *a, struct var_t *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la - lb);
}

static int
int_cmp(struct var_t *a, struct var_t *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return OP_CMP(la, lb);
}

static struct var_t *
int_lshift(struct var_t *a, struct var_t *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la << lb);
}

static struct var_t *
int_rshift(struct var_t *a, struct var_t *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la >> lb);
}

static struct var_t *
int_bit_and(struct var_t *a, struct var_t *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la & lb);
}

static struct var_t *
int_bit_or(struct var_t *a, struct var_t *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la | lb);
}

static struct var_t *
int_xor(struct var_t *a, struct var_t *b)
{
        long long la, lb;
        BUGCHECK_TYPES(a, b);
        la = intvar_toll(a);
        lb = intvar_toll(b);
        return intvar_new(la ^ lb);
}

static bool
int_cmpz(struct var_t *a)
{
        return V2I(a)->i == 0LL;
}

static struct var_t *
int_bit_not(struct var_t *a)
{
        return intvar_new(~(V2I(a)->i));
}

static struct var_t *
int_negate(struct var_t *a)
{
        return intvar_new(-(V2I(a)->i));
}

static struct var_t *
int_str(struct var_t *v)
{
        char buf[64];
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "%lld", V2I(v)->i);
        return stringvar_new(buf);
}

static struct var_t *
int_tostr(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        bug_on(!isvar_int(self));
        return int_str(self);
}

struct var_t *
intvar_new(long long initval)
{
        struct var_t *ret = var_new(&IntType);
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

