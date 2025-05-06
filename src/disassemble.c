/*
 * disassemble.c - Code that handles the -d or -D option, disassemble
 *                 bytecode back into text a user could read.
 */
#include <token.h>
#include <evilcandy.h>
#include <xptr.h>

#define IARG(x)   [IARG_##x]  = #x
#define IARGP(x)  [IARG_PTR_##x]  = #x

static const char *ATTR_NAMES[] = {
        "ATTR_CONST",
        "ATTR_STACK",
};

static const char *PTR_NAMES[] = {
        IARGP(AP),
        IARGP(FP),
        IARGP(CP),
        IARGP(SEEK),
        IARGP(THIS)
};

static const char *FUNCARG_NAMES[] = {
        IARG(HAVE_DICT),
        IARG(NO_DICT),
};

static const char *POP_NAMES[] = {
        IARG(POP_PRINT),
        IARG(POP_NORMAL),
};

static const char *BLOCK_NAMES[] = {
        IARG(BLOCK),
        IARG(LOOP),
        IARG(CONTINUE),
        IARG(TRY),
};

static const char *FUNC_ATTRARG_NAMES[] = {
        IARG(FUNC_MINARGS),
        IARG(FUNC_MAXARGS),
        IARG(FUNC_OPTIND),
        IARG(FUNC_KWIND),
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
        Object *v;

        if (i >= ex->n_rodata) {
                /*
                 * XXX: Bug necessarily? Could be from a malformed
                 * byte-code file
                 */
                DBUG("idx=%d >= n_rodata=%d", (int)i, ex->n_rodata);
                bug();
        }
        v = ex->rodata[i];

        if (isvar_xptr(v)) {
                fprintf(fp, "<%p>", v);
        } else {
                Object *str = var_str(v);
                fprintf(fp, "%s", string_get_cstring(str));
                VAR_DECR_REF(str);
        }
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
        fprintf(fp, "# enumerations for FUNC_SETATTR arg1\n");
        ADD_DEFINES(FUNC_ATTRARG_NAMES);
        putc('\n', fp);
        fprintf(fp, "# enumerations for CMP arg1\n");
        ADD_DEFINES(CMP_NAMES);
        putc('\n', fp);
        fprintf(fp, "# enumerations for PUSH_BLOCK arg1\n");
        ADD_DEFINES(BLOCK_NAMES);
        putc('\n', fp);
        fprintf(fp, "# enumerations for POP arg1\n");
        ADD_DEFINES(POP_NAMES);
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

        fprintf(fp, "%8s%-16s", "", instruction_name(ii->code));
        switch (ii->code) {
        case INSTR_ASSIGN:
        case INSTR_LOAD:
                fprintf(fp, "%s, %hd\n",
                        SAFE_NAME(PTR, ii->arg1), ii->arg2);
                break;

        case INSTR_FUNC_SETATTR:
                fprintf(fp, "%s, %hd\n",
                        SAFE_NAME(FUNC_ATTRARG, ii->arg1), ii->arg2);
                break;
        case INSTR_CALL_FUNC:
                fprintf(fp, "%s, %hd\n",
                        SAFE_NAME(FUNCARG, ii->arg1), ii->arg2);
                break;

        case INSTR_CMP:
                fprintf(fp, "%s, %hd\n",
                        SAFE_NAME(CMP, ii->arg1), ii->arg2);
                break;

        case INSTR_PUSH_BLOCK:
                fprintf(fp, "%s, %hd\n",
                        SAFE_NAME(BLOCK, ii->arg1), ii->arg2);
                break;

        case INSTR_POP:
                fprintf(fp, "%s, %hd\n",
                        SAFE_NAME(POP, ii->arg1), ii->arg2);
                break;

        case INSTR_B:
        case INSTR_B_IF:
                len = fprintf(fp, "%d, %hd", ii->arg1, ii->arg2);
                if (len < 16)
                        spaces(fp, 16 - len);
                fprintf(fp, "# label %d\n",
                        line_to_label(i + ii->arg2 + 1, ex));
                break;

        case INSTR_LOAD_CONST:
                len = fprintf(fp, "%d, %hd", ii->arg1, ii->arg2);
                if (len < 16)
                        spaces(fp, 16 - len);
                fprintf(fp, "# ");
                print_rodata_str(fp, ex, ii->arg2);
                putc('\n', fp);
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
        fprintf(fp, ".start <%p>\n", ex);
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
                Object *v = ex->rodata[i];
                if (isvar_xptr(v)) {
                        disassemble_recursive(fp,
                                        (struct xptrvar_t *)v, verbose);
                }
        }
}

void
disassemble(FILE *fp, Object *ex, const char *sourcefile_name)
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
disassemble_lite(FILE *fp, Object *ex)
{
        bug_on(!isvar_xptr(ex));
        disassemble_recursive(fp, (struct xptrvar_t *)ex, false);
}

