#include "types_priv.h"

static bool
empty_cmpz(struct var_t *v)
{
        return true;
}

static int
empty_cmp(struct var_t *a, struct var_t *b)
{
        return isvar_empty(b) ? 0 : -1;
}

static struct var_t *
empty_bit_not(struct var_t *v)
{
        return intvar_new(-1LL);
}

static struct var_t *
empty_cp(struct var_t *v)
{
        bug_on(v != NullVar);
        VAR_INCR_REF(v);
        return v;
}

static const struct operator_methods_t empty_primitives = {
        .cp             = empty_cp,
        .cmp            = empty_cmp,
        .cmpz           = empty_cmpz,
        .bit_not        = empty_bit_not,
};

struct type_t EmptyType = {
        .name   = "empty",
        .opm    = &empty_primitives,
        .cbm    = NULL,
};

