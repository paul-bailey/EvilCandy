/* Internal types: TYPE_VARPTR, TYPE_STRPTR, TYPE_XPTR */
#include "var.h"
#include <string.h>

static struct var_t *
strptr_cp(struct var_t *v)
{
        /*
         * We aren't copying this data type unless it's to be
         * used by user code, therefore make a TYPE_STRING var.
         */
        return stringvar_new(v->strptr);
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

struct var_t *
strptrvar_new(char *cstr)
{
        struct var_t *v = var_new();
        v->magic = TYPE_STRPTR;
        v->strptr = cstr;
        return v;
}

static const struct operator_methods_t strptr_primitives = {
        .cmp = strptr_cmp,
        .cp  = strptr_cp,
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

