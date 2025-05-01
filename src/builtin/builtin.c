/* q-builtin.c - Built-in callbacks for script */
#include "builtin.h"
#include <evilcandy.h>
#include <stdlib.h> /* exit */
#include <errno.h>  /* strtol errno check */

/* XXX bad name for this, it should go into io.c */
static struct gbl_private_t {
        Object *nl;
        Object *stdout_file;
} gbl;

/*
 * function.c does not trap too small var-args, some callbacks do not
 * consider 0 args an error, others do.
 */
/* XXX: move to var.c, consolidate with types/function.c */
static void
err_varargs(int expect, int got)
{
        err_setstr(ArgumentError,
                   "Missing argument: expected at least %d but got %d",
                   expect, got);
}

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

static Object *
do_print(Frame *fr)
{
        size_t i, n;
        Object *arg;

        arg = vm_get_arg(fr, 0);
        bug_on(!arg);
        bug_on(!isvar_array(arg));

        n = seqvar_size(arg);
        if (n == 0)
                err_varargs(1, 0);

        for (i = 0; i < n; i++) {
                Object *p = array_getitem(arg, i);
                if (i > 0)
                        putchar(' ');
                bug_on(!p);
                if (isvar_string(p)) {
                        file_write(gbl.stdout_file, p);
                } else {
                        Object *xpr = var_str(p);
                        file_write(gbl.stdout_file, xpr);
                        VAR_DECR_REF(xpr);
                }
                VAR_DECR_REF(p);
        }
        file_write(gbl.stdout_file, gbl.nl);
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
                res = vm_exec_func(fr, func, 0, NULL, false);
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
        if (!isvar_string(nl)) {
                err_argtype("string");
                return ErrorVar;
        }
        if (gbl.nl)
                VAR_DECR_REF(gbl.nl);
        VAR_INCR_REF(nl);
        gbl.nl = nl;
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
        int start, stop, step;
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
                stop  = intvar_toi(arg);
                break;
        case 3:
        case 2:
                arg = vm_get_arg(fr, 0);
                if (!isvar_int(arg))
                        goto needint;
                start = intvar_toi(arg);
                arg = vm_get_arg(fr, 1);
                if (!isvar_int(arg))
                        goto needint;
                stop = intvar_toi(arg);
                if (argc == 2)
                        break;
                /* case 3, fall through */
                arg = vm_get_arg(fr, 2);
                if (!isvar_int(arg))
                        goto needint;
                step = intvar_toi(arg);
        }
        if (err_occurred()) {
                err_clear();
                err_setstr(ValueError,
                           "Range values currently must fit in type 'int'");
                return ErrorVar;
        }
        return rangevar_new(start, stop, step);

needint:
        err_argtype("integer");
        return ErrorVar;
}

static Object *
do_abs(Frame *fr)
{
        const struct operator_methods_t *opm;
        Object *v = vm_get_arg(fr, 0);
        if (!v) {
                err_setstr(ArgumentError, "Expected: number");
                return ErrorVar;
        }
        opm = v->v_type->opm;
        if (!opm || !opm->abs) {
                err_setstr(TypeError, "Wrong type for abs() '%s'",
                           typestr(v));
                return ErrorVar;
        }
        return opm->abs(v);
}

static Object *
do_all(Frame *fr)
{
        enum result_t status;
        bool result;
        Object *v = vm_get_arg(fr, 0);
        if (!v) {
                err_setstr(ArgumentError, "Expected: sequential object");
                return ErrorVar;
        }
        result = var_all(v, &status);
        if (status != RES_OK)
                return ErrorVar;
        return intvar_new((int)result);
}

static Object *
do_any(Frame *fr)
{
        enum result_t status;
        bool result;
        Object *v = vm_get_arg(fr, 0);
        if (!v) {
                err_setstr(ArgumentError, "Expected: sequential object");
                return ErrorVar;
        }
        result = var_any(v, &status);
        if (status != RES_OK)
                return ErrorVar;
        return intvar_new((int)result);
}

static Object *
do_int(Frame *fr)
{
        Object *v = vm_get_arg(fr, 0);
        Object *b = vm_get_arg(fr, 1);

        if (isvar_complex(v)) {
                err_setstr(TypeError,
                           "%s type invalid for int().  Did you mean abs()?",
                           typestr(v));
                return ErrorVar;
        }

        if (isvar_int(v) || isvar_float(v)) {
                if (b) {
                        err_setstr(ArgumentError,
                                "base argument invalid when converting type %s",
                                typestr(v));
                        return ErrorVar;
                }
                if (isvar_int(v)) {
                        VAR_INCR_REF(v);
                        return v;
                }
                return intvar_new((long long)floatvar_tod(v));
        }

        if (isvar_string(v)) {
                int base;
                const char *s = string_get_cstring(v);
                char *endptr;
                long long ival;

                if (b) {
                        if (!isvar_int(b)) {
                                err_setstr(TypeError,
                                        "base argument must be an integer");
                                return ErrorVar;
                        }
                        base = intvar_toi(b);
                        if (base < 2 || err_occurred()) {
                                err_clear();
                                err_setstr(ValueError,
                                           "Base argument %lld out of range",
                                           intvar_toll(b));
                                return ErrorVar;
                        }
                } else {
                        base = 10;
                }
                errno = 0;
                while (*s != '\0' && isspace((int)(*s)))
                        s++;
                ival = strtoll(s, &endptr, base);
                if (errno || endptr == s)
                        goto bad;
                s = endptr;
                while (*s != '\0' && isspace((int)(*s)))
                        s++;
                if (*s != '\0')
                        goto bad;

                return intvar_new(ival);

bad:
                if (!errno)
                        errno = EINVAL;
                err_setstr(ValueError,
                          "Cannot convert string '%s' base %d to int (%s)",
                          string_get_cstring(v), base, strerror(errno));
                return ErrorVar;
        }

        err_setstr(TypeError,
                "Invalid type '%s' for int()", typestr(v));
        return ErrorVar;
}

static Object *
do_length(Frame *fr)
{
        Object *v = vm_get_arg(fr, 0);
        bug_on(!v);

        if (isvar_seq(v) || isvar_dict(v)) {
                return intvar_new(seqvar_size(v));
        } else if (isvar_map(v)) {
                err_setstr(NotImplementedError,
                        "length() for non-dict mappable objects not yet supported");
                return ErrorVar;
        } else {
                err_setstr(TypeError, "Invalid type '%s' for length()",
                           typestr(v));
                return ErrorVar;
        }
}

static Object *
do_max(Frame *fr)
{
        Object *res, *arg;
        size_t i, n;

        arg = vm_get_arg(fr, 0);
        bug_on(!arg);
        bug_on(!isvar_array(arg));
        n = seqvar_size(arg);
        if (n == 0) {
                err_varargs(1, 0);
                return ErrorVar;
        }
        if (n == 1) {
                /* caller provided one arg, an object to min() */
                Object *obj = array_getitem(arg, 0);
                res = var_max(obj);
                VAR_DECR_REF(obj);
                return res;
        }

        res = NULL;
        for (i = 0; i < n; i++) {
                int cmp;
                Object *v = array_getitem(arg, i);
                bug_on(!v);

                if (!res) {
                        res = v;
                        continue;
                }

                cmp = var_compare(v, res);
                if (cmp > 0) {
                        if (res)
                                VAR_DECR_REF(res);
                        res = v;
                } else {
                        VAR_DECR_REF(v);
                }
        }
        bug_on(!res);
        return res;
}

static Object *
do_min(Frame *fr)
{
        Object *res, *arg;
        size_t i, n;

        arg = vm_get_arg(fr, 0);
        bug_on(!arg);
        bug_on(!isvar_array(arg));
        n = seqvar_size(arg);
        if (n == 0) {
                err_varargs(1, 0);
                return ErrorVar;
        }
        if (n == 1) {
                /* caller provided one arg, an object to min() */
                Object *obj = array_getitem(arg, 0);
                res = var_min(obj);
                VAR_DECR_REF(obj);
                return res;
        }

        res = NULL;
        for (i = 0; i < n; i++) {
                int cmp;
                Object *v = array_getitem(arg, i);
                bug_on(!v);

                if (!res) {
                        res = v;
                        continue;
                }

                cmp = var_compare(v, res);
                if (cmp < 0) {
                        if (res)
                                VAR_DECR_REF(res);
                        res = v;
                } else {
                        VAR_DECR_REF(v);
                }
        }
        bug_on(!res);
        return res;
}

struct str2enum_t {
        const char *s;
        int v;
};

/* XXX: This seems basic enough to put in helpers.c */
static const struct str2enum_t *
str2enum(const struct str2enum_t *t, const char *s)
{
        while (t->s != NULL) {
                if (!strcmp(t->s, s))
                        return t;
                t++;
        }
        return NULL;
}

static Object *
do_floats(Frame *fr)
{
        static const struct str2enum_t floats_enc_strs[] = {
                { .s = "binary64", .v = FLOATS_BINARY64 },
                { .s = "binary32", .v = FLOATS_BINARY32 },
                { .s = "uint64",   .v = FLOATS_UINT64 },
                { .s = "uint32",   .v = FLOATS_UINT32 },
                { .s = NULL }
        };
        static const struct str2enum_t floats_endian_strs[] = {
                { .s = "network",       .v = 0 },
                { .s = "bigendian",     .v = 0 },
                { .s = "big-endian",    .v = 0 },
                { .s = "be",            .v = 0 },
                { .s = "littleendian",  .v = 1 },
                { .s = "little-endian", .v = 1 },
                { .s = "le",            .v = 1 },
                { NULL }
        };

        Object *v, *enc_arg, *le_arg;
        const char *s_enc, *s_le;
        enum floats_enc_t enc;
        int le, argc;
        const struct str2enum_t *t;

        argc = vm_get_argc(fr);
        bug_on(argc == 0);
        v = vm_get_arg(fr, 0);
        if (isvar_array(v)) {
                if (argc > 1) {
                        err_setstr(ArgumentError,
                                   "floats() accepts only one argument if list");
                        return ErrorVar;
                }
                return floatsvar_from_list(v);
        } else if (!isvar_bytes(v)) {
                err_setstr(TypeError,
                           "Invalid type '%s' for floats()",
                           typestr(v));
                return ErrorVar;
        }

        /* bytes version */
        enc_arg = vm_get_arg(fr, 1);
        le_arg = vm_get_arg(fr, 2);
        bug_on(!enc_arg);
        if (!le_arg) {
                err_setstr(TypeError,
                        "Required: endianness argument for floats(bytes)");
                return ErrorVar;
        }
        if (arg_type_check(enc_arg, &StringType) != RES_OK)
                return ErrorVar;
        if (arg_type_check(le_arg, &StringType) != RES_OK)
                return ErrorVar;
        s_enc = string_get_cstring(enc_arg);
        s_le  = string_get_cstring(le_arg);

        t = str2enum(floats_enc_strs, s_enc);
        if (!t) {
                err_setstr(ValueError, "Invalid encoding '%s'", s_enc);
                return ErrorVar;
        }
        enc = t->v;

        t = str2enum(floats_endian_strs, s_le);
        if (!t) {
                err_setstr(ValueError, "Invalid endianness '%s'", s_le);
                return ErrorVar;
        }
        le = t->v;
        return floatsvar_from_bytes(v, enc, le);
}

static const struct type_inittbl_t builtin_inittbl[] = {
        /*         name     callback  min max opt kw */
        V_INITTBL("abs",    do_abs,    1, 1, -1, -1),
        V_INITTBL("all",    do_all,    1, 1, -1, -1),
        V_INITTBL("any",    do_any,    1, 1, -1, -1),
        V_INITTBL("floats", do_floats, 1, 3, -1, -1),
        V_INITTBL("int",    do_int,    1, 2, -1, -1),
        V_INITTBL("length", do_length, 1, 1, -1, -1),
        V_INITTBL("min",    do_min,    1, 1,  0, -1),
        V_INITTBL("max",    do_max,    1, 1,  0, -1),
        V_INITTBL("print",  do_print,  1, 1,  0, -1),
        V_INITTBL("setnl",  do_setnl,  1, 1, -1, -1),
        V_INITTBL("typeof", do_typeof, 1, 1, -1, -1),
        V_INITTBL("range",  do_range,  1, 3, -1, -1),
        /* XXX: maybe exit should be a method of __gbl__._sys */
        V_INITTBL("exit",   do_exit,   0, 0, -1, -1),
        V_INITTBL("exists", do_exists, 1, 1, -1, -1),
        V_INITTBL("import", do_import, 1, 2, -1, -1),
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
build_internal_object(Object *parent, const struct type_inittbl_t *tbl)
{
        const struct type_inittbl_t *t;
        if (!tbl)
                return;
        for (t = tbl; t->name != NULL; t++) {
                /* TODO: add 'ok', optind, kwargs */
                Object *func, *key;
                func = funcvar_from_lut(t);
                key = stringvar_new(t->name);
                dict_setitem(parent, key, func);
                VAR_DECR_REF(func);
                VAR_DECR_REF(key);
        }
}

#define gblobject(ks) dict_getitem_cstr(GlobalObject, ks)

#define MAKE_EXCEPTION(X) do {                  \
        /*                                      \
         * Do not consume reference.            \
         * There's one in global vars for user  \
         * and one in C for us.                 \
         */                                     \
        X = stringvar_new(#X);                  \
        vm_add_global(X, X);                    \
} while (0)

static void
initialize_global_object(void)
{
        const struct gblinit_t {
                const char *name;
                const struct type_inittbl_t *tbl;
        } GLOBAL_MODULES[] = {
                { "_builtins",  builtin_inittbl },
                { "_math",      bi_math_inittbl__ },
                { "_io",        bi_io_inittbl__ },
                { NULL }
        };
        const struct gblinit_t *t = GLOBAL_MODULES;
        Object *o, *k;

        GlobalObject = dictvar_new();

        for (t = GLOBAL_MODULES; t->name != NULL; t++) {
                k = stringvar_new(t->name);
                o = dictvar_new();
                build_internal_object(o, t->tbl);
                dict_setitem(GlobalObject, k, o);
                VAR_DECR_REF(k);
                VAR_DECR_REF(o);
        }

        /*
         * sys is mostly data, so build it differently than above modules
         */

#define STDIO_FMT          "s/fnsmi/"
#define STDIO_ARGS(X, Y)   #X, X, "<" #X ">", FMODE_##Y | FMODE_PROTECT

        o = var_from_format("{ss" STDIO_FMT STDIO_FMT STDIO_FMT "}",
                        "nl", "\n",
                        STDIO_ARGS(stdin, READ),
                        STDIO_ARGS(stdout, WRITE),
                        STDIO_ARGS(stderr, WRITE));

#undef STDIO_ARGS

        k = stringvar_new("_sys");
        dict_setitem(GlobalObject, k, o);
        VAR_DECR_REF(k);
        VAR_DECR_REF(o);
}

static Object *
sysget(const char *name)
{
        Object *sys = gblobject("_sys");
        Object *res = dict_getitem_cstr(sys, name);
        VAR_DECR_REF(sys);
        return res;
}

/* initialize the builtin/ C file modules */
void
moduleinit_builtin(void)
{
        Object *o, *k;

        /* Do this first.  build_internal_object de-references it. */
        initialize_global_object();

        MAKE_EXCEPTION(ArgumentError);
        MAKE_EXCEPTION(KeyError);
        MAKE_EXCEPTION(IndexError);
        MAKE_EXCEPTION(NameError);
        MAKE_EXCEPTION(NotImplementedError);
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
        gbl.nl = sysget("nl");
        gbl.stdout_file = sysget("stdout");
        bug_on(!gbl.nl);
        bug_on(!gbl.stdout_file);
}

void
moduledeinit_builtin(void)
{
        VAR_DECR_REF(GlobalObject);

        VAR_DECR_REF(gbl.stdout_file);
        VAR_DECR_REF(gbl.nl);

        VAR_DECR_REF(ArgumentError);
        VAR_DECR_REF(KeyError);
        VAR_DECR_REF(IndexError);
        VAR_DECR_REF(NameError);
        VAR_DECR_REF(NotImplementedError);
        VAR_DECR_REF(NumberError);
        VAR_DECR_REF(RangeError);
        VAR_DECR_REF(RecursionError);
        VAR_DECR_REF(RuntimeError);
        VAR_DECR_REF(SyntaxError);
        VAR_DECR_REF(SystemError);
        VAR_DECR_REF(TypeError);
        VAR_DECR_REF(ValueError);
}

