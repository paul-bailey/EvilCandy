#include "var.h"

static bool
empty_cmpz(struct var_t *v)
{
        return true;
}

static struct var_t *
empty_bit_not(struct var_t *v)
{
        struct var_t *new = var_new();
        integer_init(new, -1LL);
        return new;
}

static const struct operator_methods_t empty_primitives = {
        .cmpz           = empty_cmpz,
        .bit_not        = empty_bit_not,
};

void
typedefinit_empty(void)
{
        var_config_type(TYPE_EMPTY, "empty", &empty_primitives, NULL);
}

