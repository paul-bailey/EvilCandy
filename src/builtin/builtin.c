/* q-builtin.c - Built-in callbacks for script */
#include "builtin.h"
#include <evilcandy.h>
#include <stdlib.h> /* exit */

#define NLMAX 8

/* Private data for the global object */
static struct gbl_private_t {
        char nl[NLMAX];
} gbl;

static struct var_t *
do_typeof(struct vmframe_t *fr)
{
        struct var_t *p = frame_get_arg(fr, 0);
        return stringvar_new(typestr(p));
}

static void
print_nl(void)
{
        char *s;
        for (s = gbl.nl; *s; s++)
                putchar((int)*s);
}

static struct var_t *
do_print(struct vmframe_t *fr)
{
        struct var_t *p = frame_get_arg(fr, 0);
        if (!p) {
                err_setstr(RuntimeError,
                           "Expected: at least one argument to print");
                return ErrorVar;
        }
        if (isvar_string(p)) {
                char *s = string_get_cstring(p);
                while (*s)
                        putchar((int)*s++);
        } else {
                struct var_t *xpr = var_str(p);
                printf("%s", string_get_cstring(xpr));
                VAR_DECR_REF(xpr);
        }
        print_nl();
        return NULL;
}

static struct var_t *
do_import(struct vmframe_t *fr)
{
        struct var_t *file_name = frame_get_arg(fr, 0);
        struct var_t *mode      = frame_get_arg(fr, 1);
        struct var_t *res;
        struct var_t *ex;
        const char *modestr, *fnamestr;
        enum { R, X } how;
        FILE *fp;
        int status;

        if (!file_name || !mode) {
                err_setstr(RuntimeError, "Expected: import(MODULE, MODE)");
                return ErrorVar;
        }
        if (!isvar_string(file_name) || !isvar_string(mode)) {
                err_setstr(RuntimeError, "import: file name and mode should be strings");
                return ErrorVar;
        }

        modestr = string_get_cstring(mode);
        if (!strcmp(modestr, "r")) {
                how = R; /* read script and return it as a function */
        } else if (!strcmp(modestr, "x")) {
                how = X; /* execute script and return its results */
        } else {
                err_setstr(RuntimeError, "import: incorrect MODE argument");
                return ErrorVar;
        }

        fnamestr = string_get_cstring(file_name);

        fp = push_path(fnamestr);
        if (!fp) {
                err_errno("Cannot access '%s' properly", fnamestr);
                return ErrorVar;
        }
        ex = assemble(fnamestr, fp, true, &status);
        pop_path(fp);

        /* we're assembling top, so ex should be NULL if error */
        bug_on(status != RES_OK && ex != NULL);

        if (!ex) {
                err_setstr(RuntimeError,
                           "Failed to import module '%s'", fnamestr);
                return ErrorVar;
        }

        res = funcvar_new_user(ex);
        if (how == X) {
                struct var_t *func = res;
                res = vm_exec_func(fr, func, NULL, 0, NULL);
                VAR_DECR_REF(func);
        }
        /* else, how == R */
        return res;
}

static struct var_t *
do_exit(struct vmframe_t *fr)
{
        struct var_t *p = frame_get_arg(fr, 0);
        if (p && isvar_string(p))
                printf("%s\n", string_get_cstring(p));
        exit(0);

        /*
         * we'll obviously never reach this.
         * Compilers make me do these things.
         */
        return NULL;
}

static struct var_t *
do_setnl(struct vmframe_t *fr)
{
        struct var_t *nl = frame_get_arg(fr, 0);
        char *s;
        if (!isvar_string(nl)) {
                err_argtype("string");
                return ErrorVar;
        }
        s = string_get_cstring(nl);
        memset(gbl.nl, 0, NLMAX);
        strncpy(gbl.nl, s, NLMAX-1);
        return NULL;
}

static struct var_t *
do_range(struct vmframe_t *fr)
{
        int argc = vm_get_argc(fr);
        long long start, stop, step;
        struct var_t *arg;
        if (argc < 1 || argc > 3) {
                err_setstr(RuntimeError, "Expected: 1 to 3 args");
                return ErrorVar;
        }
        /* defaults */
        start = 0LL;
        step  = 1LL;
        switch (argc) {
        case 1:
                arg = vm_get_arg(fr, 0);
                if (!isvar_int(arg))
                        goto needint;
                stop  = intvar_toll(arg);
                break;
        case 3:
        case 2:
                arg = vm_get_arg(fr, 0);
                if (!isvar_int(arg))
                        goto needint;
                start = intvar_toll(arg);
                arg = vm_get_arg(fr, 1);
                if (!isvar_int(arg))
                        goto needint;
                stop = intvar_toll(arg);
                if (argc == 2)
                        break;
                /* case 3, fall through */
                arg = vm_get_arg(fr, 2);
                if (!isvar_int(arg))
                        goto needint;
                step = intvar_toll(arg);
        }
        if (start < INT_MIN || start > INT_MAX
                || stop < INT_MIN || stop > INT_MAX
                || step < INT_MIN || step > INT_MAX) {
                err_setstr(RuntimeError,
                           "Range values currently must fit in type 'int'");
                return ErrorVar;
        }
        return rangevar_new(start, stop, step);

needint:
        err_argtype("integer");
        return ErrorVar;
}

static const struct inittbl_t builtin_inittbl[] = {
        TOFTBL("print",  do_print,  1, -1),
        TOFTBL("setnl",  do_setnl,  1, 1),
        TOFTBL("typeof", do_typeof, 1, 1),
        TOFTBL("range",  do_range,  1, 3),
        /* XXX: maybe exit should be a method of __gbl__._sys */
        TOFTBL("exit",   do_exit,   0, -1),
        TOFTBL("import", do_import, 0, 2),
        { .name = NULL },
};

static const struct inittbl_t gblinit[] = {
        TOOTBL("_builtins", builtin_inittbl),
        TOOTBL("_math",  bi_math_inittbl__),
        TOOTBL("_io",    bi_io_inittbl__),
        TOSTBL("ParserError", "Parser Error"),
        TOSTBL("RuntimeError",  "Runtime Error"),
        /*
         * these are for non-fatal errors. Things like bug traps
         * or failed malloc calls result in a stderr message and exit(),
         * so no fancy error handling for that.
         */
        TOSTBL("SystemError",   "System error"),
        { .name = NULL },
};

/**
 * bi_build_internal_object__ - build up a C-defined object with a
 *                              linear table
 * @parent: The object to add new children to.  This is already set
 *          to be an object, and may or may not have children already
 * @tbl: Table to scan.  A unique child will be created for each member
 *       of this table.
 */
void
bi_build_internal_object__(struct var_t *parent, const struct inittbl_t *tbl)
{
        const struct inittbl_t *t;
        if (!tbl)
                return;
        for (t = tbl; t->name != NULL; t++) {
                struct var_t *child;
                switch (t->magic) {
                case TYPE_DICT:
                        child = objectvar_new();
                        bi_build_internal_object__(child, t->tbl);
                        break;
                case TYPE_FUNCTION:
                        child = funcvar_new_intl(t->cb,
                                                 t->minargs, t->maxargs);
                        break;
                case TYPE_STRING:
                        child = stringvar_new(t->s);
                        break;
                case TYPE_INT:
                        child = intvar_new(t->i);
                        break;
                case TYPE_FLOAT:
                        child = floatvar_new(t->f);
                        break;
                default:
                        child = NULL;
                        bug();
                }
                if (object_setattr(parent, t->name, child) != 0) {
                        /*
                         * Whether this is a "bug" or not is philosophical.
                         * Anyway, it can't be user error, so something
                         * internal failed this operation.
                         */
                        bug();
                }
        }
}

/* initialize the builtin/ C file modules */
void
moduleinit_builtin(void)
{
        struct var_t *o;

        /* Do this first.  bi_build_internal_object__ de-references it. */
        GlobalObject = objectvar_new();
        object_set_priv(GlobalObject, &gbl, NULL);
        bi_build_internal_object__(GlobalObject, gblinit);

        ParserError  = object_getattr(GlobalObject, "ParserError");
        RuntimeError = object_getattr(GlobalObject, "RuntimeError");
        SystemError  = object_getattr(GlobalObject, "SystemError");
        if (!ParserError || !RuntimeError || !SystemError) {
                fail("Could not create error objects");
        }
        o = object_getattr(GlobalObject, literal_put("_builtins"));
        bug_on(!o);
        object_add_to_globals(o);
        vm_add_global(literal_put("__gbl__"), GlobalObject);

        /* Set up gbl private data */
        strcpy(gbl.nl, "\n");
}

