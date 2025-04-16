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
empty_cp(struct var_t *v)
{
        bug_on(v != NullVar);
        VAR_INCR_REF(v);
        return v;
}

struct var_t *
emptyvar_new(void)
{
        return var_new(&EmptyType);
}

/* says 'null' -- no use creating this more than once */
static struct var_t *emptystr = NULL;

struct var_t *
empty_str(struct var_t *v)
{
        if (emptystr == NULL)
                emptystr = stringvar_new("null");

        VAR_INCR_REF(emptystr);
        return emptystr;
}

struct type_t EmptyType = {
        .name   = "empty",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct var_t),
        .str    = empty_str,
        .cp     = empty_cp,
        .cmp    = empty_cmp,
        .cmpz   = empty_cmpz,
};

