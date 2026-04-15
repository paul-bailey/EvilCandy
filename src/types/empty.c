#include <evilcandy.h>
#include <evilcandy/global.h>
#include <evilcandy/hash.h>
#include <internal/type_registry.h>

static bool
empty_cmpz(Object *v)
{
        return true;
}

Object *
emptyvar_new(void)
{
        return var_new(&EmptyType);
}

static Object *
empty_str(Object *v)
{
        if (STRCONST_ID(null))
                return VAR_NEW_REF(STRCONST_ID(null));
        return stringvar_from_ascii("null");
}

static hash_t
empty_hash(Object *v)
{
        return calc_ptr_hash(v);
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
        .cmp    = NULL,
        .cmpeq  = NULL,
        .cmpz   = empty_cmpz,
        /* there should only be one instance of this */
        .hash   = empty_hash,
};

