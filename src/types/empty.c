#include "var.h"

static bool
empty_cmpz(struct var_t *v)
{
        return true;
}

static void
empty_bit_not(struct var_t *v)
{
        integer_init(v, -1LL);
}

static const struct operator_methods_t empty_primitives = {
        .cmpz           = empty_cmpz,
        .bit_not        = empty_bit_not,
};

void
typedefinit_empty(void)
{
        var_config_type(QEMPTY_MAGIC, "empty", &empty_primitives, NULL);
}

