#include "instructions.h"
#include "token.h"
#include <evilcandy.h>
#include <stdio.h>

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
line_to_label(unsigned int line, struct executable_t *ex)
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
print_rodata_str(FILE *fp, struct executable_t *ex, unsigned int i)
{
        struct var_t *v;
        if (i >= ex->n_rodata)
                fprintf(fp, "%s", undefstr);
        v = ex->rodata[i];

        switch (v->magic) {
        case TYPE_INT:
                fprintf(fp, "0x%016llx", v->i);
                break;
        case TYPE_FLOAT:
                fprintf(fp, "%.8le", v->f);
                break;
        case TYPE_STRPTR:
                print_escapestr(fp, v->strptr, '"');
                break;
        case TYPE_XPTR:
                fprintf(fp, "<function-pointer>");
                break;
        default:
                fprintf(fp, "%s", undefstr);
        }
}

static void
dump_rodata(FILE *fp, struct executable_t *ex)
{
        int i;
        for (i = 0; i < ex->n_rodata; i++) {
                fprintf(fp, ".rodata ");
                print_rodata_str(fp, ex, i);
                putc('\n', fp);
        }
}

void
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
        fprintf(fp, "# enumerations for PUSH_PTR arg1\n");
        ADD_DEFINES(PTR_NAMES);
        putc('\n', fp);
        putc('\n', fp);
}

static void
disinstr(FILE *fp, struct executable_t *ex, unsigned int i)
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
                fprintf(fp, "# ");
                print_rodata_str(fp, ex, ii->arg2);
                putc('\n', fp);
                break;

        case INSTR_PUSH_PTR:
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

void
disassemble(FILE *fp, struct executable_t *ex)
{
        int i;
        const char *what = (ex->flags & FE_TOP) ? "script" : "function";
        fprintf(fp, ".start \"%s\"\n", what);
        fprintf(fp, "# in file \"%s\"\n", ex->file_name);
        fprintf(fp, "# starting at line %d\n", ex->file_line);

        for (i = 0; i < ex->n_instr; i++)
                disinstr(fp, ex, i);

        putc('\n', fp);
        dump_rodata(fp, ex);
        fprintf(fp, ".end \"%s\"\n\n\n", what);
}

