#include <evilcandy.h>

static bool
empty_cmpz(Object *v)
{
        return true;
}

static int
empty_cmp(Object *a, Object *b)
{
        return isvar_empty(b) ? 0 : -1;
}

Object *
emptyvar_new(void)
{
        return var_new(&EmptyType);
}

Object *
empty_str(Object *v)
{
        if (STRCONST_ID(null))
                return VAR_NEW_REF(STRCONST_ID(null));
        return stringvar_from_ascii("null");
}

struct type_t EmptyType = {
        .flags  = 0,
        .name   = "empty",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(Object),
        .str    = empty_str,
        .cmp    = empty_cmp,
        .cmpz   = empty_cmpz,
};

