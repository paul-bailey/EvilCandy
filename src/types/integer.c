#include <evilcandy.h>

#define V2I(v)  ((struct intvar_t *)v)

static inline long long
var2int(struct var_t *v)
{
        return isvar_int(v) ? V2I(v)->i : floatvar_tod(v);
}

static struct var_t *
int_mul(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("*");
                return NULL;
        }
        return intvar_new(V2I(a)->i * var2int(b));
}

static struct var_t *
int_div(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("/");
                return NULL;
        }
        long long i = var2int(b);
        if (i == 0LL) /* No! */
                return intvar_new(0LL);
        else
                return intvar_new(V2I(a)->i / i);
}

static struct var_t *
int_mod(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("%");
                return NULL;
        }
        long long i = var2int(b);
        if (i == 0LL)
                return intvar_new(0LL);
        else
                return intvar_new(V2I(a)->i % i);
}

static struct var_t *
int_add(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("+");
                return NULL;
        }
        return intvar_new(V2I(a)->i + var2int(b));
}

static struct var_t *
int_sub(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("-");
                return NULL;
        }
        return intvar_new(V2I(a)->i - var2int(b));
}

static int
int_cmp(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b))
                return -1;
        long long i = var2int(b);
        return OP_CMP(V2I(a)->i, i);
}

static struct var_t *
int_lshift(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("<<");
                return NULL;
        }
        long long shift = var2int(b);
        if (shift >= 64 || shift <= 0)
                return intvar_new(0LL);
        else
                return intvar_new(V2I(a)->i << shift);
}

static struct var_t *
int_rshift(struct var_t *a, struct var_t *b)
{
        /*
         * XXX REVISIT: Policy decision, is this logical shift,
         * or arithmetic shift?
         */
        if (!isnumvar(b)) {
                err_mismatch(">>");
                return NULL;
        }
        long long shift = var2int(b);
        unsigned long long i = V2I(a)->i;
        if (shift >= 64 || shift <= 0)
                return intvar_new(0LL);
        else
                return intvar_new(i >> shift);
}

static struct var_t *
int_bit_and(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("&");
                return NULL;
        }
        return intvar_new(V2I(a)->i & var2int(b));
}

static struct var_t *
int_bit_or(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("|");
                return NULL;
        }
        return intvar_new(V2I(a)->i | var2int(b));
}

static struct var_t *
int_xor(struct var_t *a, struct var_t *b)
{
        if (!isnumvar(b)) {
                err_mismatch("^");
                return NULL;
        }
        return intvar_new(V2I(a)->i ^ var2int(b));
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

