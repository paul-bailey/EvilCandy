/* Internal types: TYPE_STRPTR, TYPE_XPTR */
#include "types_priv.h"
#include <string.h>
#include <stdlib.h>

struct uuidptrvar_t {
        struct var_t base;
        char *uuid;
};

/* struct (uuid/x)ptrvar_t forward-defined in typedefs.h */

#define V2XP(v) ((struct xptrvar_t *)(v))

struct var_t *
xptrvar_new(struct executable_t *x)
{
        struct var_t *v = var_new(&XptrType);
        V2XP(v)->xptr = x;
        return v;
}

char *
uuidptr_get_cstring(struct var_t *v)
{
        bug_on(v->v_type != &UuidptrType);
        return ((struct uuidptrvar_t *)v)->uuid;
}

struct var_t *
uuidptrvar_new(char *uuid)
{
        struct var_t *v = var_new(&UuidptrType);
        ((struct uuidptrvar_t *)v)->uuid = uuid;
        return v;
}

static void
uuidptr_reset(struct var_t *v)
{
        /*
         * low-level alert -- we happen to know that @uuid arg to
         * uuidptrvar_new is a malloc'd value which caller will not throw
         * away.  Copy pointer directly and call free in our destructor.
         */
        free(((struct uuidptrvar_t *)v)->uuid);
}

struct type_t XptrType = {
        .name   = "[internal-use executable]",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct xptrvar_t),
        .cmp    = NULL,
        .cmpz   = NULL,
        .cp     = NULL,
        .reset  = NULL,
};

struct type_t UuidptrType = {
        .name   = "[internal-use UUID]",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct uuidptrvar_t),
        .cmp    = NULL,
        .cmpz   = NULL,
        .cp     = NULL,
        .reset = uuidptr_reset,
};
