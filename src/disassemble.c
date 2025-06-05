/*
 * disassemble.c - Code that handles the -d or -D option, disassemble
 *                 bytecode back into text a user could read.
 */
#include <token.h>
#include <evilcandy.h>
#include <xptr.h>

#define IARG(x)   [IARG_##x]  = #x
#define IARGP(x)  [IARG_PTR_##x]  = #x

enum {
        DF_VERBOSE      = 0x01,
        DF_DEFINE       = 0x02,
        DF_ENUMERATE    = 0x04,
        DF_HEADER       = 0x08,
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
        IARG(GT),
        IARG(HAS),
        IARG(EQ3),
        IARG(NEQ3),
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

#define ADD_DEFINES(fp, arr, verb, comment) do { \
        int i; \
        if (verb) \
                fprintf(fp, "# %s\n", comment); \
        for (i = 0; i < ARRAY_SIZE(arr); i++) \
                fprintf(fp, ".define %-24s%d\n", arr[i], i); \
        putc('\n', fp); \
} while (0)

static void
print_rodata_str(FILE *fp, struct xptrvar_t *ex,
                 unsigned int i, bool in_comment)
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
                /* XXX: it's a bug not to put this in configure.ac */
                bug_on(sizeof(void *) > sizeof(long long));
                fprintf(fp, "<%p>", v);
        } else {
                Object *str = var_str(v);
                if (in_comment) {
                        const char *s = string_cstring(str);
                        size_t len = strlen(s);
                        if (len > 20) {
                                len = 20;
                                while (len--)
                                        fputc(*s++, fp);
                                fprintf(fp, "...");
                        } else {
                                fprintf(fp, "%s", s);
                        }
                } else {
                        /*
                         * Need to print whole thing no matter how
                         * long it is.
                         */
                        fprintf(fp, "%s", string_cstring(str));
                }
                VAR_DECR_REF(str);
        }
}

static void
dump_rodata(FILE *fp, struct xptrvar_t *ex)
{
        int i;
        for (i = 0; i < ex->n_rodata; i++) {
                fprintf(fp, ".rodata ");
                print_rodata_str(fp, ex, i, false);
                putc('\n', fp);
        }
}

static void
add_defines(FILE *fp, unsigned int flags)
{
        bool verbose = !!(flags & DF_VERBOSE);

        ADD_DEFINES(fp, ATTR_NAMES, verbose,
                "enuerations for GETATTR/SETATTR arg1");
        ADD_DEFINES(fp, FUNCARG_NAMES, verbose,
                "enumerations for CALL_FUNC arg1");
        ADD_DEFINES(fp, FUNC_ATTRARG_NAMES, verbose,
                "enumerations for FUNC_SETATTR arg1");
        ADD_DEFINES(fp, CMP_NAMES, verbose,
                "enumerations for CMP arg1");
        ADD_DEFINES(fp, BLOCK_NAMES, verbose,
                "enumerations for PUSH_BLOCK arg1");
        ADD_DEFINES(fp, POP_NAMES, verbose,
                "enumerations for POP arg1");
        ADD_DEFINES(fp, PTR_NAMES, verbose,
                "enumerations for LOAD/ASSIGN_xxx arg1");
        putc('\n', fp);
}

static void
disassemble_start(FILE *fp, const char *sourcefile_name, unsigned int flags)
{
        if (!!(flags & DF_VERBOSE) && sourcefile_name != NULL)
                fprintf(fp, "# Disassembly for file %s\n\n", sourcefile_name);
        fprintf(fp, ".evilcandy \"" VERSION "\"\n\n");
        if (!!(flags & DF_DEFINE))
                add_defines(fp, flags);
}

static void
disinstr(FILE *fp, struct xptrvar_t *ex, unsigned int i, unsigned int flags)
{
        int label = line_to_label(i, ex);
        size_t len = 0;
        const char *argname;

        instruction_t *ii = &ex->instr[i];
        if (label >= 0) {
                putc('\n', fp);
                fprintf(fp, "%d:\n", label);
        }
        if (!(flags & DF_ENUMERATE)) {
                fprintf(fp, "%hhu %hhu %hd\n",
                        ii->code, ii->arg1, ii->arg2);
                return;
        }

        fprintf(fp, "%8s%-16s", "", instruction_name(ii->code));
        switch (ii->code) {
        case INSTR_ASSIGN:
        case INSTR_LOAD:
                argname = SAFE_NAME(PTR, ii->arg1);
                break;
        case INSTR_FUNC_SETATTR:
                argname = SAFE_NAME(FUNC_ATTRARG, ii->arg1);
                break;
        case INSTR_CALL_FUNC:
                argname = SAFE_NAME(FUNCARG, ii->arg1);
                break;
        case INSTR_CMP:
                argname = SAFE_NAME(CMP, ii->arg1);
                break;
        case INSTR_PUSH_BLOCK:
                argname = SAFE_NAME(BLOCK, ii->arg1);
                break;
        case INSTR_POP:
                argname = SAFE_NAME(POP, ii->arg1);
                break;
        default:
                argname = NULL;
        }
        if (argname)
                len = fprintf(fp, "%s, %hd", argname, ii->arg2);
        else
                len = fprintf(fp, "%hhd, %hd", ii->arg1, ii->arg2);

        if (!!(flags & DF_VERBOSE)) {
                enum { COMNTPOS = 16 };
                if (instr_uses_jump(*ii)) {
                        if (len < COMNTPOS)
                                spaces(fp, COMNTPOS - len);
                        fprintf(fp, "# label %d",
                                line_to_label(i + ii->arg2 + 1, ex));
                } else if (instr_uses_rodata(*ii)) {
                        if (len < COMNTPOS)
                                spaces(fp, COMNTPOS - len);
                        fprintf(fp, "# ");
                        print_rodata_str(fp, ex, ii->arg2, true);
                }
        }
        fputc('\n', fp);
}

static void
disassemble_recursive(FILE *fp, struct xptrvar_t *ex, unsigned int flags)
{
        int i;
        fprintf(fp, ".start <%p>\n", ex);
        if (!!(flags & DF_VERBOSE)) {
                fprintf(fp, "# in file \"%s\"\n", ex->file_name);
                fprintf(fp, "# starting at line %d\n", ex->file_line);
        }

        for (i = 0; i < ex->n_instr; i++) {
                disinstr(fp, ex, i, flags);
        }

        putc('\n', fp);
        dump_rodata(fp, ex);
        fprintf(fp, ".end\n\n\n");

        for (i = 0; i < ex->n_rodata; i++) {
                Object *v = ex->rodata[i];
                if (isvar_xptr(v)) {
                        disassemble_recursive(fp,
                                        (struct xptrvar_t *)v, flags);
                }
        }
}

static void
disassemble_(FILE *fp, Object *ex,
             const char *sourcefile_name, unsigned int flags)
{
        bug_on(!isvar_xptr(ex));
        if (!!(flags & DF_HEADER))
                disassemble_start(fp, sourcefile_name, flags);
        disassemble_recursive(fp, (struct xptrvar_t *)ex, flags);
}

/**
 * pretty disassemble - comments, header defines, enumerated
 *                      opcodes and arguments.
 * This is technically reverse-compilable, but the verbosity means
 * bigger file and slower parsing; for serialization, use
 * disassemble_minimal.
 */
void
disassemble(FILE *fp, Object *ex, const char *sourcefile_name)
{
        unsigned int flags = DF_VERBOSE | DF_DEFINE
                             | DF_ENUMERATE | DF_HEADER;
        disassemble_(fp, ex, sourcefile_name, flags);
}

/**
 * like disassemble, but without the header defines.
 * This cannot be reversed-compiled.
 * Used for debugging in interactive TTY mode.
 */
void
disassemble_lite(FILE *fp, Object *ex)
{
        disassemble_(fp, ex, NULL, DF_VERBOSE | DF_ENUMERATE);
}

/**
 * The not-very-readable version of disassemble.
 * Only the minimum information needed to re-compile is printed.
 */
void
disassemble_minimal(FILE *fp, Object *ex)
{
        disassemble_(fp, ex, NULL, DF_HEADER);
}

