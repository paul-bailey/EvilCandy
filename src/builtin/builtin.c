/* q-builtin.c - Built-in callbacks for script */
#include "builtin.h"
#include <evilcandy.h>
#include <stdlib.h> /* exit */

#define NLMAX 8

/*
 * FIXME: this is Windows-level of 'who does this?'
 * Replace it with more professional-looking locale
 * interface with user.
 */
/* Private data for the global object */
static struct gbl_private_t {
        char nl[NLMAX];
} gbl;

static Object *
do_typeof(Frame *fr)
{
        Object *p = frame_get_arg(fr, 0);
        if (!p) {
                err_setstr(ArgumentError, "Expected: any data type");
                return ErrorVar;
        }
        return stringvar_new(typestr(p));
}

static void
print_nl(void)
{
        char *s;
        for (s = gbl.nl; *s; s++)
                putchar((int)*s);
}

static Object *
do_print(Frame *fr)
{
        int argc = vm_get_argc(fr);
        int i;
        if (!argc) {
                err_setstr(ArgumentError,
                           "Expected: at least one argument to print");
                return ErrorVar;
        }
        for (i = 0; i < argc; i++) {
                Object *p = vm_get_arg(fr, i);
                if (i > 0)
                        putchar(' ');
                bug_on(!p);
                if (isvar_string(p)) {
                        char *s = string_get_cstring(p);
                        while (*s)
                                putchar((int)*s++);
                } else {
                        Object *xpr = var_str(p);
                        printf("%s", string_get_cstring(xpr));
                        VAR_DECR_REF(xpr);
                }
        }
        print_nl();
        return NULL;
}

static Object *
do_import(Frame *fr)
{
        Object *file_name = frame_get_arg(fr, 0);
        Object *mode      = frame_get_arg(fr, 1);
        Object *res;
        Object *ex;
        const char *modestr, *fnamestr;
        enum { R, X } how;
        FILE *fp;
        int status;

        if (!file_name || !mode) {
                err_setstr(ArgumentError, "Expected: import(MODULE, MODE)");
                return ErrorVar;
        }
        if (!isvar_string(file_name) || !isvar_string(mode)) {
                err_setstr(TypeError, "import: file name and mode should be strings");
                return ErrorVar;
        }

        modestr = string_get_cstring(mode);
        if (!strcmp(modestr, "r")) {
                how = R; /* read script and return it as a function */
        } else if (!strcmp(modestr, "x")) {
                how = X; /* execute script and return its results */
        } else {
                err_setstr(ValueError, "import: incorrect MODE argument");
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
                if (!err_occurred()) {
                        err_setstr(RuntimeError,
                                   "Failed to import module '%s'", fnamestr);
                }
                return ErrorVar;
        }

        res = funcvar_new_user(ex);
        if (how == X) {
                Object *func = res;
                res = vm_exec_func(fr, func, NULL, 0, NULL);
                VAR_DECR_REF(func);
        }
        /* else, how == R */
        return res;
}

static Object *
do_exit(Frame *fr)
{
        Object *p = frame_get_arg(fr, 0);
        if (p && isvar_string(p))
                printf("%s\n", string_get_cstring(p));
        exit(0);

        /*
         * we'll obviously never reach this.
         * Compilers make me do these things.
         */
        return NULL;
}

static Object *
do_setnl(Frame *fr)
{
        Object *nl = frame_get_arg(fr, 0);
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

static Object *
do_exists(Frame *fr)
{
        Object *key = vm_get_arg(fr, 0);
        bool exists;
        if (!key || !isvar_string(key)) {
                err_setstr(TypeError, "Expected: string");
                return ErrorVar;
        }
        exists = vm_symbol_exists(key);
        return intvar_new((int)exists);
}

static Object *
do_range(Frame *fr)
{
        int argc = vm_get_argc(fr);
        long long start, stop, step;
        Object *arg;
        if (argc < 1 || argc > 3) {
                err_setstr(ArgumentError, "Expected: 1 to 3 args");
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
                err_setstr(ValueError,
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
        TOFTBL("exists", do_exists, 1, 1),
        TOFTBL("import", do_import, 1, 2),
        { .name = NULL },
};

static const struct inittbl_t gblinit[] = {
        TOOTBL("_builtins", builtin_inittbl),
        TOOTBL("_math",     bi_math_inittbl__),
        TOOTBL("_io",       bi_io_inittbl__),
        { .name = NULL },
};

/**
 * build_internal_object - build up a C-defined object with a
 *                              linear table
 * @parent: The object to add new children to.  This is already set
 *          to be an object, and may or may not have children already
 * @tbl: Table to scan.  A unique child will be created for each member
 *       of this table.
 */
static void
build_internal_object(Object *parent, const struct inittbl_t *tbl)
{
        const struct inittbl_t *t;
        if (!tbl)
                return;
        for (t = tbl; t->name != NULL; t++) {
                Object *child, *key;
                switch (t->magic) {
                case TYPE_DICT:
                        child = dictvar_new();
                        build_internal_object(child, t->tbl);
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
                key = stringvar_new(t->name);
                if (dict_setattr(parent, key, child) != 0) {
                        /*
                         * Whether this is a "bug" or not is philosophical.
                         * Anyway, it can't be user error, so something
                         * internal failed this operation.
                         */
                        bug();
                }
                VAR_DECR_REF(key);
        }
}

static Object *
gblobject(const char *ks)
{
        Object *key = stringvar_new(ks);
        Object *ret = dict_getattr(GlobalObject, key);
        VAR_DECR_REF(key);
        return ret;
}

#define MAKE_EXCEPTION(X) do {                  \
        /*                                      \
         * Do not consume reference.            \
         * There's one in global vars for user  \
         * and one in C for us.                 \
         */                                     \
        X = stringvar_new(#X);                  \
        vm_add_global(X, X);                    \
} while (0)

/* initialize the builtin/ C file modules */
void
moduleinit_builtin(void)
{
        Object *o, *k;

        /* Do this first.  build_internal_object de-references it. */
        GlobalObject = dictvar_new();
        build_internal_object(GlobalObject, gblinit);


        MAKE_EXCEPTION(ArgumentError);
        MAKE_EXCEPTION(KeyError);
        MAKE_EXCEPTION(IndexError);
        MAKE_EXCEPTION(NameError);
        MAKE_EXCEPTION(NumberError);
        MAKE_EXCEPTION(RangeError);
        MAKE_EXCEPTION(RecursionError);
        MAKE_EXCEPTION(RuntimeError);
        MAKE_EXCEPTION(SyntaxError);
        MAKE_EXCEPTION(SystemError);
        MAKE_EXCEPTION(TypeError);
        MAKE_EXCEPTION(ValueError);

        o = gblobject("_builtins");
        bug_on(!o);
        dict_add_to_globals(o);
        VAR_DECR_REF(o);

        k = stringvar_new("__gbl__");
        vm_add_global(k, GlobalObject);
        VAR_DECR_REF(k);

        /* Set up gbl private data */
        strcpy(gbl.nl, "\n");
}

void
moduledeinit_builtin(void)
{
        VAR_DECR_REF(GlobalObject);

        VAR_DECR_REF(ArgumentError);
        VAR_DECR_REF(KeyError);
        VAR_DECR_REF(IndexError);
        VAR_DECR_REF(NameError);
        VAR_DECR_REF(NumberError);
        VAR_DECR_REF(RangeError);
        VAR_DECR_REF(RecursionError);
        VAR_DECR_REF(RuntimeError);
        VAR_DECR_REF(SyntaxError);
        VAR_DECR_REF(SystemError);
        VAR_DECR_REF(TypeError);
        VAR_DECR_REF(ValueError);
}

