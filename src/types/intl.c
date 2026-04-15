/* Internal types: UuidType, IdType */
#include <evilcandy.h>
#include <evilcandy/ewrappers.h>
#include <internal/type_registry.h>

/*
 * TODO; Delete this object class.
 * It isn't used anywhere.
 */
struct uuidptrvar_t {
        Object base;
        char *uuid;
};

struct idvar_t {
        Object base;
        long long id;
};

/* struct (uuid/x)ptrvar_t forward-defined in typedefs.h */

char *
uuidptr_get_cstring(Object *v)
{
        bug_on(v->v_type != &UuidptrType);
        return ((struct uuidptrvar_t *)v)->uuid;
}

long long
idvar_toll(Object *v)
{
        bug_on(v->v_type != &IdType);
        return ((struct idvar_t *)v)->id;
}

Object *
uuidptrvar_new(char *uuid)
{
        Object *v = var_new(&UuidptrType);
        ((struct uuidptrvar_t *)v)->uuid = uuid;
        return v;
}

Object *
idvar_new(long long id)
{
        Object *v = var_new(&IdType);
        ((struct idvar_t *)v)->id = id;
        return v;
}

static void
uuidptr_reset(Object *v)
{
        /*
         * low-level alert -- we happen to know that @uuid arg to
         * uuidptrvar_new is a malloc'd value which caller will not throw
         * away.  Copy pointer directly and call free in our destructor.
         */
        efree(((struct uuidptrvar_t *)v)->uuid);
}

static Object *
uuidptr_str(Object *v)
{
        char buf[64];
        evc_sprintf(buf, sizeof(buf), "<uuid '%s'>",
                    (((struct uuidptrvar_t *)v)->uuid));
        return stringvar_new(buf);
}

static Object *
id_str(Object *v)
{
        char buf[32];
        evc_sprintf(buf, sizeof(buf), "<id %llx>",
                 (((struct idvar_t *)v)->id));
        return stringvar_new(buf);
}

struct type_t IdType = {
        .flags  = 0,
        .name   = "[internal-use ID]",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct idvar_t),
        .str    = id_str,
        .cmp    = NULL,
        .cmpz   = NULL,
        .cmpeq  = NULL, /*< XXX REVISIT: id's match, is that '=='? */
        .reset  = NULL,
        .hash   = NULL,
};

struct type_t UuidptrType = {
        .flags  = 0,
        .name   = "[internal-use UUID]",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct uuidptrvar_t),
        .str    = uuidptr_str,
        .cmp    = NULL,
        .cmpz   = NULL,
        .cmpeq  = NULL,
        .reset = uuidptr_reset,
        .hash   = NULL,
};
