/*
 * disassemble.c - Code that handles the -d or -D option, disassemble
 *                 bytecode back into text a user could read.
 */
#include <token.h>
#include <evilcandy.h>
#include <xptr.h>

#define IARG(x)   [IARG_##x]  = #x
#define IARGP(x)  [IARG_PTR_##x]  = #x

static const char *INSTR_NAMES[N_INSTR] = {
#include "disassemble_gen.c.h"
};

static const char *ATTR_NAMES[] = {
        "ATTR_CONST",
        "ATTR_STACK",
};

static const char *PTR_NAMES[] = {
        IARGP(AP),
        IARGP(FP),
        IARGP(CP),
        IARGP(SEEK),
        IARGP(GBL),
        IARGP(THIS)
};

static const char *FUNCARG_NAMES[] = {
        "NO_PARENT",
        "WITH_PARENT"
};

static const char *CMP_NAMES[] = {
        IARG(EQ),
        IARG(LEQ),
        IARG(GEQ),
        IARG(NEQ),
        IARG(LT),
        IARG(GT)
};

/* note, i evaluated twice */
#define SAFE_NAME(arr, i) \
        (i >= ARRAY_SIZE(arr##_NAMES) ? undefstr : arr##_NAMES[i])

static const char *undefstr = "<!undefined>";

static int
line_to_label(unsigned int line, struct xptrvar_t *ex)
{
        int i;
        for (i = 0; i < ex->n_label; i++) {
                if (ex->label[i] == line)
                        return i;
        }
        return -1;
}

static void
spaces(FILE *fp, int n)
{
        while (n-- > 0)
                putc(' ', fp);
}

#define ADD_DEFINES(arr) do { \
        int i; \
        for (i = 0; i < ARRAY_SIZE(arr); i++) \
                fprintf(fp, ".define %-24s%d\n", arr[i], i); \
} while (0)

static void
print_rodata_str(FILE *fp, struct xptrvar_t *ex, unsigned int i)
{
        struct var_t *v;

        if (i >= ex->n_rodata) {
                /*
                 * XXX: Bug necessarily? Could be from a malformed
                 * byte-code file
                 */
                DBUG("idx=%d >= n_rodata=%d", (int)i, ex->n_rodata);
                bug();
        }
        v = ex->rodata[i];

        if (isvar_int(v))
                fprintf(fp, "0x%016llx", intvar_toll(v));
        else if (isvar_float(v))
                fprintf(fp, "%.8le", floatvar_tod(v));
        else if (isvar_string(v))
                print_escapestr(fp, string_get_cstring(v), '"');
        else if (isvar_xptr(v))
                fprintf(fp, "<%s>", ((struct xptrvar_t *)(v))->uuid);
        else
                fprintf(fp, "%s", undefstr);
}

static void
dump_rodata(FILE *fp, struct xptrvar_t *ex)
{
        int i;
        for (i = 0; i < ex->n_rodata; i++) {
                fprintf(fp, ".rodata ");
                print_rodata_str(fp, ex, i);
                putc('\n', fp);
        }
}

static void
disassemble_start(FILE *fp, const char *sourcefile_name)
{
        fprintf(fp, "# Disassembly for file %s\n\n", sourcefile_name);
        fprintf(fp, "# enuerations for GETATTR/SETATTR arg1\n");
        ADD_DEFINES(ATTR_NAMES);
        putc('\n', fp);
        fprintf(fp, "# enumerations for CALL_FUNC arg1\n");
        ADD_DEFINES(FUNCARG_NAMES);
        putc('\n', fp);
        fprintf(fp, "# enumerations for CMP arg1\n");
        ADD_DEFINES(CMP_NAMES);
        putc('\n', fp);
        fprintf(fp, "# enumerations for LOAD/ASSIGN_xxx arg1\n");
        ADD_DEFINES(PTR_NAMES);
        putc('\n', fp);
        putc('\n', fp);
}

static void
disinstr(FILE *fp, struct xptrvar_t *ex, unsigned int i)
{
        int label = line_to_label(i, ex);
        size_t len = 0;

        instruction_t *ii = &ex->instr[i];
        if (label >= 0) {
                putc('\n', fp);
                fprintf(fp, "%d:\n", label);
        }

        fprintf(fp, "%8s%-16s", "", SAFE_NAME(INSTR, ii->code));
        switch (ii->code) {
        case INSTR_GETATTR:
        case INSTR_SETATTR:
                len = fprintf(fp, "%s, %hd",
                              SAFE_NAME(ATTR, ii->arg1), ii->arg2);
                if (len < 16)
                        spaces(fp, 16 - len);
                if (ii->arg1 != IARG_ATTR_STACK) {
                        fprintf(fp, "# ");
                        print_rodata_str(fp, ex, ii->arg2);
                }
                putc('\n', fp);
                break;

        case INSTR_ASSIGN:
        case INSTR_LOAD:
                fprintf(fp, "%s, %hd\n",
                        SAFE_NAME(PTR, ii->arg1), ii->arg2);
                break;

        case INSTR_CALL_FUNC:
                fprintf(fp, "%s, %hd\n",
                        SAFE_NAME(FUNCARG, ii->arg1), ii->arg2);
                break;

        case INSTR_CMP:
                fprintf(fp, "%s, %hd\n",
                        SAFE_NAME(CMP, ii->arg1), ii->arg2);
                break;

        case INSTR_B:
        case INSTR_B_IF:
                len = fprintf(fp, "%d, %hd", ii->arg1, ii->arg2);
                if (len < 16)
                        spaces(fp, 16 - len);
                fprintf(fp, "# label %d\n",
                        line_to_label(i + ii->arg2 + 1, ex));
                break;

        case INSTR_SYMTAB:
                len = fprintf(fp, "%d, %hd", ii->arg1, ii->arg2);
                if (len < 16)
                        spaces(fp, 16 - len);
                fprintf(fp, "# ");
                print_rodata_str(fp, ex, ii->arg2);
                putc('\n', fp);
                break;

        default:
                fprintf(fp, "%d, %hd\n", ii->arg1, ii->arg2);
        }
}

static void
disassemble_recursive(FILE *fp, struct xptrvar_t *ex, int verbose)
{
        int i;
        fprintf(fp, ".start <%s>\n", ex->uuid);
        if (verbose) {
                fprintf(fp, "# in file \"%s\"\n", ex->file_name);
                fprintf(fp, "# starting at line %d\n", ex->file_line);
        }

        for (i = 0; i < ex->n_instr; i++) {
                disinstr(fp, ex, i);
        }

        putc('\n', fp);
        dump_rodata(fp, ex);
        fprintf(fp, ".end\n\n\n");

        for (i = 0; i < ex->n_rodata; i++) {
                struct var_t *v = ex->rodata[i];
                if (isvar_xptr(v)) {
                        disassemble_recursive(fp,
                                        (struct xptrvar_t *)v, verbose);
                }
        }
}

void
disassemble(FILE *fp, struct var_t *ex, const char *sourcefile_name)
{
        bug_on(!isvar_xptr(ex));
        disassemble_start(fp, sourcefile_name);
        disassemble_recursive(fp, (struct xptrvar_t *)ex, true);
}

/**
 * like disassemble, but without the verbose defines up top.
 * Used for debugging in interactive TTY mode.
 */
void
disassemble_lite(FILE *fp, struct var_t *ex)
{
        bug_on(!isvar_xptr(ex));
        disassemble_recursive(fp, (struct xptrvar_t *)ex, false);
}

