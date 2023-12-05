/* Internal types: TYPE_VARPTR, TYPE_STRPTR, TYPE_XPTR */
#include "var.h"

static void
strptr_mov(struct var_t *to, struct var_t *from)
{
        string_init(to, from->strptr);
}

static const struct operator_methods_t strptr_primitives = {
        .mov = strptr_mov,
};

static const struct operator_methods_t no_primitives = { 0 };

static const struct type_inittbl_t no_methods[] = {
        TBLEND,
};

void
typedefinit_intl(void)
{
        var_config_type(TYPE_STRPTR, "[internal-use string]",
                        &strptr_primitives, no_methods);
        var_config_type(TYPE_VARPTR, "[internal-use stack]",
                        &no_primitives, no_methods);
        var_config_type(TYPE_XPTR, "[internal-use executable]",
                        &no_primitives, no_methods);
}

