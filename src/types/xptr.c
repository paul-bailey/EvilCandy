#include <evilcandy/debug.h>
#include <evilcandy/hash.h>
#include <evilcandy/ewrappers.h>
#include <evilcandy/types/string.h>
#include <evilcandy/types/tuple.h>
#include <internal/locations.h>
#include <internal/types/sequential_types.h>
#include <internal/types/xptr.h>

#define V2XP(v_)        ((struct xptrvar_t *)(v_))

static void
xptr_reset(Object *v)
{
        struct xptrvar_t *ex = V2XP(v);
        if (ex->instr)
                efree(ex->instr);
        if (ex->locations)
                efree(ex->locations);
        if (ex->rodata)
                VAR_DECR_REF(ex->rodata);
        if (ex->file_name)
                efree(ex->file_name);
        if (ex->names)
                VAR_DECR_REF(ex->names);
        if (ex->funcname)
                VAR_DECR_REF(ex->funcname);
}

static Object *
xptr_str(Object *v)
{
        char buf[64];
        evc_sprintf(buf, sizeof(buf), "<code-block at %p>", (void *)v);
        return stringvar_new(buf);
}

/* Instances should be treated as globally unique */
static hash_t
xptr_hash(Object *v)
{
        return calc_ptr_hash(v);
}

struct type_t XptrType = {
        .flags  = 0,
        .name   = "[executable]",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct xptrvar_t),
        .str    = xptr_str,
        .cmp    = NULL,
        .cmpeq  = NULL,
        .cmpz   = NULL,
        .reset  = xptr_reset,
        .hash   = xptr_hash,
};

/**
 * xptrvar_new - Get a new XptrType var
 * @file_name: Name of source file that defines this code
 * @file_line: Starting line in file of this code block if it's a
 *             function definition, or 1 if it's the start of a
 *             script.
 */
Object *
xptrvar_new(const struct xptr_cfg_t *cfg)
{
        Object *v = var_new(&XptrType);
        struct xptrvar_t *x = V2XP(v);

        if (cfg->rodata && seqvar_size(cfg->rodata) > 0) {
                x->rodata = tuplevar_from_stack(array_get_data(cfg->rodata),
                                                seqvar_size(cfg->rodata),
                                                false);
        } else {
                x->rodata = NULL;
        }
        if (cfg->names) {
                x->names = tuplevar_from_stack(array_get_data(cfg->names),
                                               seqvar_size(cfg->names),
                                               false);
        } else {
                x->names = NULL;
        }
        bug_on(!cfg->instr);
        bug_on(!cfg->n_instr);
        x->instr        = cfg->instr;
        x->n_instr      = cfg->n_instr;
        x->file_name    = estrdup(cfg->file_name);
        x->file_line    = cfg->file_line;
        x->n_locals     = cfg->n_locals;
        x->funcname     = cfg->funcname;
        x->locations    = cfg->locations;
        x->locations_size = cfg->locations_size;
        if (x->funcname)
                VAR_INCR_REF(x->funcname);
        return v;
}


