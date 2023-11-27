#include "var.h"

static inline long long
var2int(struct var_t *v, const char *op)
{
        if (!isnumvar(v))
                syntax("Invalid or mismatched types for operator");
        return v->magic == QINT_MAGIC ? v->i : (long long)v->f;
}

static void
int_mul(struct var_t *a, struct var_t *b)
{
        a->i *= var2int(b, "*");
}

static void
int_div(struct var_t *a, struct var_t *b)
{
        long long i = var2int(b, "/");
        if (i == 0LL)
                a->i = 0;
        else
                a->i /= i;
}

static void
int_mod(struct var_t *a, struct var_t *b)
{
        long long i = var2int(b, "%");
        if (i == 0LL)
                a->i = 0;
        else
                a->i /= i;
}

static void
int_add(struct var_t *a, struct var_t *b)
{
        a->i += var2int(b, "+");
}

static void
int_sub(struct var_t *a, struct var_t *b)
{
        a->i -= var2int(b, "-");
}

static int
int_cmp(struct var_t *a, struct var_t *b)
{
        long long i = var2int(b, "cmp");
        return OP_CMP(a->i, i);
}

static void
int_lshift(struct var_t *a, struct var_t *b)
{
        long long shift = var2int(b, "<<");
        if (shift >= 64)
                a->i = 0LL;
        else if (shift > 0)
                a->i <<= shift;
}

static void
int_rshift(struct var_t *a, struct var_t *b)
{
        /*
         * XXX REVISIT: Policy decision, is this logical shift,
         * or arithmetic shift?
         */
        long long shift = var2int(b, ">>");
        unsigned long long i = a->i;
        if (shift >= 64)
                i = 0LL;
        else if (shift > 0)
                i >>= shift;
        a->i = i;
}

static void
int_bit_and(struct var_t *a, struct var_t *b)
{
        a->i &= var2int(b, "&");
}

static void
int_bit_or(struct var_t *a, struct var_t *b)
{
        a->i |= var2int(b, "|");
}

static void
int_xor(struct var_t *a, struct var_t *b)
{
        a->i ^= var2int(b, "^");
}

static bool
int_cmpz(struct var_t *a)
{
        return a->i == 0LL;
}

static void
int_incr(struct var_t *a)
{
        a->i++;
}

static void
int_decr(struct var_t *a)
{
        a->i--;
}

static void
int_bit_not(struct var_t *a)
{
        a->i = ~(a->i);
}

static void
int_negate(struct var_t *a)
{
        a->i = -a->i;
}

static void
int_mov(struct var_t *a, struct var_t *b)
{
        a->i = var2int(b, "mov");
}

static void
int_tostr(struct var_t *ret)
{
        char buf[64];
        ssize_t len;
        struct var_t *self = get_this();
        bug_on(self->magic != QINT_MAGIC);

        len = snprintf(buf, sizeof(buf), "%lld", self->i);
        bug_on(len >= sizeof(buf));
        (void)len; /* in case NDEBUG */

        qop_assign_cstring(ret, buf);
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
        .cmp            = int_cmp,
        .lshift         = int_lshift,
        .rshift         = int_rshift,
        .bit_and        = int_bit_and,
        .bit_or         = int_bit_or,
        .xor            = int_xor,
        .cmpz           = int_cmpz,
        .incr           = int_incr,
        .decr           = int_decr,
        .bit_not        = int_bit_not,
        .negate         = int_negate,
        .mov            = int_mov,
};


void
typedefinit_integer(void)
{
        var_config_type(QINT_MAGIC, "integer", &int_primitives, int_methods);
}
