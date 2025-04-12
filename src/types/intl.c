/* Internal types: TYPE_STRPTR, TYPE_XPTR */
#include "var.h"
#include <string.h>

static struct var_t *
strptr_cp(struct var_t *v)
{
        /*
         * We aren't copying this data type unless it's to be
         * used by user code, therefore make a StringType var.
         */
        return stringvar_new(v->strptr);
}

static int
strptr_cmp(struct var_t *to, struct var_t *from)
{
        char *s2, *s1 = to->strptr;
        if (isvar_string(from))
                s2 = string_get_cstring(from);
        else if (isvar_strptr(from))
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
        v->v_type = &StrptrType;
        v->strptr = cstr;
        return v;
}

struct var_t *
xptrvar_new(struct executable_t *x)
{
        struct var_t *v = var_new();
        v->v_type = &XptrType;
        v->xptr = x;
        return v;
}

static const struct operator_methods_t strptr_primitives = {
        .cmp = strptr_cmp,
        .cp  = strptr_cp,
};

static const struct operator_methods_t no_primitives = { 0 };

struct type_t StrptrType = {
        .name   = "[internal-use string]",
        .opm    = &strptr_primitives,
        .cbm    = NULL,
};

struct type_t XptrType = {
        .name   = "[internal-use executable]",
        .opm    = &no_primitives,
        .cbm    = NULL,
};

