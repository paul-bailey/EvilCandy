/* q-builtin.c - Built-in callbacks for script */
#include <evilcandy.h>
#include <stdlib.h> /* exit */
#include <errno.h>  /* strtol errno check */

static Object *
do_typeof(Frame *fr)
{
        Object *p = frame_get_arg(fr, 0);
        if (!p) {
                err_frame_minargs(fr, 1);
                return ErrorVar;
        }
        return stringvar_new(typestr(p));
}

static Object *
do_print(Frame *fr)
{
        size_t i, n;
        Object *arg, *kw, *file, *sep, *end;
        enum result_t status;
        Object *res = NULL;

        arg = vm_get_arg(fr, 0);
        kw = vm_get_arg(fr, 1);
        bug_on(!arg || !isvar_array(arg));
        bug_on(!kw || !isvar_dict(kw));

        dict_unpack(kw,
                    STRCONST_ID(sep),  &sep,  STRCONST_ID(spc),
                    STRCONST_ID(file), &file, gbl.stdout_file,
                    STRCONST_ID(end),  &end,  gbl.nl,
                    NULL);
        n = seqvar_size(arg);
        if (n == 0) {
                if (file_write(file, end) != RES_OK)
                        res = ErrorVar;
                goto done;
        }

        for (i = 0; i < n; i++) {
                Object *p;

                if (i > 0) {
                        if (file_write(file, sep) != RES_OK) {
                                res = ErrorVar;
                                break;
                        }
                }

                p = array_getitem(arg, i);
                bug_on(!p);

                if (isvar_string(p)) {
                        status = file_write(file, p);
                } else {
                        Object *xpr = var_str(p);
                        status = file_write(file, xpr);
                        VAR_DECR_REF(xpr);
                }
                VAR_DECR_REF(p);
                if (status != RES_OK) {
                        res = ErrorVar;
                        break;
                }
        }

        if (res != ErrorVar) {
                status = file_write(file, end);
                if (status != RES_OK)
                        res = ErrorVar;
        }

done:
        VAR_DECR_REF(sep);
        VAR_DECR_REF(file);
        VAR_DECR_REF(end);
        return res;
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
                err_frame_minargs(fr, 2);
                return ErrorVar;
        }
        if (!isvar_string(file_name) || !isvar_string(mode)) {
                err_setstr(TypeError, "import: file name and mode should be strings");
                return ErrorVar;
        }

        modestr = string_cstring(mode);
        if (!strcmp(modestr, "r")) {
                how = R; /* read script and return it as a function */
        } else if (!strcmp(modestr, "x")) {
                how = X; /* execute script and return its results */
        } else {
                err_setstr(ValueError, "import: incorrect MODE argument");
                return ErrorVar;
        }

        fnamestr = string_cstring(file_name);

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

        /*
         * @res may or may not keep a reference to 'ex' alive depending
         * on the 'mode' arg and on how the imported file implements
         * things, but we still have our own separate reference that we
         * need to consume.
         */
        VAR_DECR_REF(ex);
        return res;
}

static Object *
do_exit(Frame *fr)
{
        Object *p = frame_get_arg(fr, 0);
        if (p && isvar_string(p))
                printf("%s\n", string_cstring(p));
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
do_abs(Frame *fr)
{
        const struct operator_methods_t *opm;
        Object *v = vm_get_arg(fr, 0);
        if (!v) {
                err_frame_minargs(fr, 1);
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
                err_frame_minargs(fr, 1);
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
                err_frame_minargs(fr, 1);
                return ErrorVar;
        }
        result = var_any(v, &status);
        if (status != RES_OK)
                return ErrorVar;
        return intvar_new((int)result);
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
                err_va_minargs(arg, 1);
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
                err_va_minargs(arg, 1);
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

static Object *
do_ord(Frame *fr)
{
        Object *str = vm_get_arg(fr, 0);
        long ord;

        if (arg_type_check(str, &StringType) == RES_ERROR)
                return ErrorVar;

        if (seqvar_size(str) != 1) {
                err_setstr(ValueError,
                           "Expected single character but got string of length %ld",
                           seqvar_size(str));
                return ErrorVar;
        }

        ord = string_ord(str, 0);
        bug_on(ord < 0L);
        return intvar_new(ord);
}

static Object *
do_disassemble(Frame *fr)
{
        /* TODO: optional file arg */
        Object *method = NULL;
        Object *func = vm_get_arg(fr, 0);
        Object *ex;
        if (!func) {
                err_frame_minargs(fr, 1);
                return ErrorVar;
        }

        if (isvar_method(func)) {
                enum result_t status;
                Object *tmp;
                method = func;
                status = methodvar_tofunc(method, &func, &tmp);
                bug_on(status == RES_ERROR);
                (void)status;
                VAR_DECR_REF(tmp);
                func = tmp;
                bug_on(!isvar_function(func));
        } else if (!isvar_function(func)) {
                err_setstr(TypeError,
                           "Cannot disassemble uncallable '%s'",
                           typestr(func));
                return ErrorVar;
        }

        ex = function_get_executable(func);
        if (method)
                VAR_DECR_REF(func);
        if (!ex) {
                err_setstr(TypeError,
                           "Cannot disassemble internal function");
                return ErrorVar;
        }
        disassemble_lite(stdout, ex);
        VAR_DECR_REF(ex);
        return NULL;
}

static const struct type_inittbl_t builtin_inittbl[] = {
        /*         name     callback  min max opt kw */
        V_INITTBL("abs",    do_abs,    1, 1, -1, -1),
        V_INITTBL("all",    do_all,    1, 1, -1, -1),
        V_INITTBL("any",    do_any,    1, 1, -1, -1),
        V_INITTBL("disassemble", do_disassemble, 1, 1, -1, -1),
        V_INITTBL("length", do_length, 1, 1, -1, -1),
        V_INITTBL("min",    do_min,    1, 1,  0, -1),
        V_INITTBL("max",    do_max,    1, 1,  0, -1),
        V_INITTBL("ord",    do_ord,    1, 1, -1, -1),
        V_INITTBL("print",  do_print,  2, 2,  0,  1),
        V_INITTBL("setnl",  do_setnl,  1, 1, -1, -1),
        V_INITTBL("typeof", do_typeof, 1, 1, -1, -1),
        /* XXX: maybe exit should be a method of __gbl__._sys */
        V_INITTBL("exit",   do_exit,   0, 0, -1, -1),
        V_INITTBL("exists", do_exists, 1, 1, -1, -1),
        V_INITTBL("import", do_import, 1, 2, -1, -1),
        { .name = NULL },
};

void
moduleinit_builtin(void)
{
        Object *k = stringvar_new("_builtins");
        Object *o = dictvar_from_methods(NULL, builtin_inittbl);
        dict_setitem(GlobalObject, k, o);
        VAR_DECR_REF(k);
        VAR_DECR_REF(o);
}
