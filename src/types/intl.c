/* Internal types: TYPE_VARPTR, TYPE_STRPTR, TYPE_XPTR */
#include "var.h"
#include <string.h>

static void
strptr_mov(struct var_t *to, struct var_t *from)
{
        string_init(to, from->strptr);
}

static int
strptr_cmp(struct var_t *to, struct var_t *from)
{
        char *s2, *s1 = to->strptr;
        if (from->magic == TYPE_STRING)
                s2 = string_get_cstring(from);
        else if (from->magic == TYPE_STRPTR)
                s2 = from->strptr;
        else
                return 1;

        if (!s1 || !s2)
                return s1 != s2;
        if (s1 == s2)
                return 0;

        return !!strcmp(s1, s2);
}

static const struct operator_methods_t strptr_primitives = {
        .mov = strptr_mov,
        .cmp = strptr_cmp,
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
        var_config_type(TYPE_XPTR, "[internal-use executable]",
                        &no_primitives, no_methods);
}

