#include <evilcandy.h>
#include <xptr.h>

#define V2XP(v_)        ((struct xptrvar_t *)(v_))


static void
xptr_reset(struct var_t *v)
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
        if (ex->label)
                efree(ex->label);
}

static int
xptr_cmp(struct var_t *a, struct var_t *b)
{
        bug_on(!isvar_xptr(a));
        bug_on(!isvar_xptr(b));

        /* there should be no other way */
        return a == b ? 0 : (a > b ? 1 : -1);
}

static struct var_t *
xptr_cp(struct var_t *x)
{
        /* should be unreachable */
        bug();
        return NULL;
}

static struct var_t *
xptr_str(struct var_t *v)
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
        .cp     = xptr_cp,
        .reset  = xptr_reset,
};

/*
 * These API functions should only be used by assemble.c and
 * serializer.c, which build up the XptrType code block.
 */

/**
 * xptr_next_label - Grow label array and get index of next label
 *
 * Return value should be regarded as an opaque identifier.  Use
 * it for the argument to xptr_set_label
 */
int
xptr_next_label(struct var_t *v)
{
        struct xptrvar_t *x = V2XP(v);
        bug_on(!isvar_xptr(v));

        x->n_label++;
        x->label = erealloc(x->label, x->n_label * sizeof(x->label[0]));
        return x->n_label - 1;
}

/**
 * xptr_set_label - Set a label
 *
 * The assumption here is:
 *      1. @jmp is a return value from xptr_next_label
 *      2. You are inserting this BEFORE a call to xptr_add_instr
 *         for an instruction you want to jump to.
 */
void
xptr_set_label(struct var_t *v, int jmp)
{
        struct xptrvar_t *x = V2XP(v);
        bug_on(!isvar_xptr(v));
        bug_on(!x->label || x->n_label <= jmp);
        x->label[jmp] = x->n_instr;
}

/**
 * xptr_add_xptr - Seek or add rodata
 * @x: Parent XptrType var
 * @new_data: Child XptrType var to add.  This must be either a datum
 *      that can be expressed in a single token--strings, integers,
 *      etc.--or other XptrType vars.
 *
 * If @new_data already exists in @x's .rodata, consume @new_data's
 * reference and return the .rodata array index where it was found.
 * If it does not exist, store it and return the new array index.
 * Since @new_data is assumed to have just been created, this function
 * does not produce a reference.
 */
int
xptr_add_rodata(struct var_t *v, struct var_t *new_data)
{
        int i;
        struct xptrvar_t *x = V2XP(v);

        bug_on(!isvar_xptr(v));
        for (i = 0; i < x->n_rodata; i++) {
                if (var_compare(new_data, x->rodata[i]) == 0) {
                        VAR_DECR_REF(new_data);
                        break;
                }
        }

        if (i == x->n_rodata) {
                /*
                 * It looks cumbersome to grow rodata array by just one
                 * each time, but we're only doing this during assembly.
                 * In usual cases, there is probably less than a dozen
                 * of these per function.
                 */
                x->rodata = erealloc(x->rodata,
                                     (x->n_rodata + 1) * sizeof(struct var_t *));
                x->rodata[x->n_rodata] = new_data;
                x->n_rodata++;
        }

        return i;
}

/**
 * xptr_add_instr - Append an instruction to the instruction array.
 */
void
xptr_add_instr(struct var_t *v, instruction_t ii)
{
        struct xptrvar_t *x = V2XP(v);

        bug_on(!isvar_xptr(v));

        /*
         * TODO: Add a n_instr_alloc var, there are enough of these that
         * we don't really want to realloc the array by just one each time
         */
        x->instr = erealloc(x->instr,
                        (x->n_instr + 1) * sizeof(x->instr[0]));

        x->instr[x->n_instr] = ii;
        x->n_instr++;
}

/**
 * xptrvar_new - Get a new XptrType var
 * @file_name: Name of source file that defines this code
 * @file_line: Starting line in file of this code block if it's a
 *             function definition, or 1 if it's the start of a
 *             script.
 */
struct var_t *
xptrvar_new(const char *file_name, int file_line)
{
        struct var_t *v = var_new(&XptrType);
        struct xptrvar_t *x = V2XP(v);
        x->instr        = NULL;
        x->rodata       = NULL;
        x->n_instr      = 0;
        x->n_rodata     = 0;
        x->label        = NULL;
        x->n_label      = 0;
        x->file_name    = file_name;
        x->file_line    = file_line;
        x->uuid         = uuidstr();
        return v;
}


