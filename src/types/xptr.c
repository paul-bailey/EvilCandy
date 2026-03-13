#include <evilcandy.h>
#include <xptr.h>

#define V2XP(v_)        ((struct xptrvar_t *)(v_))

static void
xptr_reset(Object *v)
{
        struct xptrvar_t *ex = V2XP(v);
        if (ex->instr)
                efree(ex->instr);
        if (ex->rodata)
                VAR_DECR_REF(ex->rodata);
        if (ex->file_name)
                efree(ex->file_name);
}

static int
xptr_cmp(Object *a, Object *b)
{
        bug_on(!isvar_xptr(a));
        bug_on(!isvar_xptr(b));

        /* there should be no other way */
        return a == b ? 0 : (a > b ? 1 : -1);
}

static Object *
xptr_str(Object *v)
{
        char buf[64];
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf) - 1, "<code-block at %p>", v);
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
        .cmp    = xptr_cmp,
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

        x->rodata = tuplevar_from_stack(array_get_data(cfg->rodata),
                                        seqvar_size(cfg->rodata), false);
        x->names = tuplevar_from_stack(array_get_data(cfg->names),
                                        seqvar_size(cfg->names), false);
        x->instr        = cfg->instr;
        x->n_instr      = cfg->n_instr;
        x->file_name    = estrdup(cfg->file_name);
        x->file_line    = cfg->file_line;
        x->n_locals     = cfg->n_locals;
        return v;
}


