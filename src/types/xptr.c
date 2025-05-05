#include <evilcandy.h>
#include <xptr.h>

#define V2XP(v_)        ((struct xptrvar_t *)(v_))

static void
xptr_reset(Object *v)
{
        struct xptrvar_t *ex = V2XP(v);
        if (ex->instr)
                efree(ex->instr);
        if (ex->rodata) {
                int i;
                for (i = 0; i < ex->n_rodata; i++)
                        VAR_DECR_REF(ex->rodata[i]);
                efree(ex->rodata);
        }
        if (ex->file_name)
                efree(ex->file_name);
        if (ex->label)
                efree(ex->label);
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
        snprintf(buf, sizeof(buf) - 1, "<code-block at '%s'>",
                 V2XP(v)->uuid);
        return stringvar_new(buf);
}

struct type_t XptrType = {
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

        x->instr        = cfg->instr;
        x->n_rodata     = cfg->n_rodata;
        x->n_instr      = cfg->n_instr;
        x->rodata       = cfg->rodata;
        x->label        = cfg->label;
        x->n_label      = cfg->n_label;
        x->file_name    = estrdup(cfg->file_name);
        x->file_line    = cfg->file_line;
        x->uuid         = uuidstr();
        return v;
}


