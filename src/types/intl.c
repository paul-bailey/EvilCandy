/* Internal types: TYPE_STRPTR, TYPE_XPTR */
#include <evilcandy.h>

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

static struct var_t *
xptrvar_str(struct var_t *x)
{
        char buf[64];
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf) - 1, "<code-block at '%s'>",
                 V2XP(x)->xptr->uuid);
        return stringvar_new(buf);
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
        efree(((struct uuidptrvar_t *)v)->uuid);
}

static struct var_t *
uuidptr_str(struct var_t *v)
{
        char buf[64];
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf) - 1, "<uuid '%s'>",
                 (((struct uuidptrvar_t *)v)->uuid));
        return stringvar_new(buf);
}

struct type_t XptrType = {
        .name   = "[internal-use executable]",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct xptrvar_t),
        .str    = xptrvar_str,
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
        .str    = uuidptr_str,
        .cmp    = NULL,
        .cmpz   = NULL,
        .cp     = NULL,
        .reset = uuidptr_reset,
};
