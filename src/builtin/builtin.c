/* q-builtin.c - Built-in callbacks for script */
#include <evilcandy.h>
#include <stdlib.h> /* exit */
#include <errno.h>  /* strtol errno check */

static Object *
do_setattr(Frame *fr)
{
        Object *instance, *key, *value, *ret;

        instance = NULL;
        key = NULL;
        value = NULL;
        if (vm_getargs(fr, "[<*><*><*>!]{!}:setattr",
                       &instance, &key, &value) == RES_ERROR) {
                return ErrorVar;
        }
        if (!isvar_instance(instance)) {
                err_argtype("instance");
                return ErrorVar;
        }
        ret = NULL;
        if (var_setattr(instance, key, value) == RES_ERROR)
                ret = ErrorVar;
        return ret;
}

static Object *
do_getattr(Frame *fr)
{
        Object *instance, *key, *value;

        instance = NULL;
        key = NULL;
        if (vm_getargs(fr, "[<*><*>!]{!}:getattr", &instance, &key)
            == RES_ERROR) {
                return ErrorVar;
        }
        value = var_getattr(instance, key);
        if (value == ErrorVar) {
                err_clear();
                value = VAR_NEW_REF(NullVar);
        }
        return value;
}

static Object *
do_dir(Frame *fr)
{
        Object *arg, *arr;

        arg = NULL;
        if (vm_getargs(fr, "[|<*>!]{!}:dir", &arg) == RES_ERROR)
                return ErrorVar;
        if (!arg) {
                arr = arrayvar_new(0);
                array_extend(arr, vm_globaldict());
                var_sort(arr);
        } else if (isvar_instance(arg)) {
                arr = instance_dir(arg);
        } else {
                bug_on(!arg->v_type->methods);
                arr = arrayvar_new(0);
                array_extend(arr, arg->v_type->methods);
                var_sort(arr);
        }
        return arr;
}

static Object *
do_typeof(Frame *fr)
{
        Object *p = NULL;
        if (vm_getargs(fr, "[<*>!]{!}:typeof", &p) == RES_ERROR)
                return ErrorVar;
        return stringvar_new(typestr(p));
}

static Object *
do_hash(Frame *fr)
{
        hash_t hash;
        Object *p = NULL;
        if (vm_getargs(fr, "[<*>!]{!}:hash", &p) == RES_ERROR)
                return ErrorVar;
        hash = var_hash(p);
        if (hash == HASH_ERROR) {
                err_hashable(p, "hash");
                return ErrorVar;
        }
        return intvar_new((unsigned long long)hash);
}

static Object *
do_print(Frame *fr)
{
        size_t i, n;
        Object *arg, *file, *sep, *end;
        enum result_t status;
        Object *res = NULL;

        arg = sep = file = end = NULL;
        if (vm_getargs(fr, "<[]>{|<s></><s>}:print",
                       &arg,
                       STRCONST_ID(sep), &sep,
                       STRCONST_ID(file), &file,
                       STRCONST_ID(end), &end) == RES_ERROR) {
                return ErrorVar;
        }
        bug_on(!arg);
        if (!sep)
                sep = STRCONST_ID(spc);
        if (!file)
                file = gbl.stdout_file;
        if (!end)
                end = gbl.nl;

        n = seqvar_size(arg);
        if (n == 0) {
                if (evc_file_write(file, end) < 0)
                        res = ErrorVar;
                goto done;
        }

        for (i = 0; i < n; i++) {
                Object *p;

                if (i > 0) {
                        if (evc_file_write(file, sep) < 0) {
                                res = ErrorVar;
                                break;
                        }
                }

                p = array_getitem(arg, i);
                bug_on(!p);

                if (isvar_string(p)) {
                        status = evc_file_write(file, p);
                } else {
                        Object *xpr = var_str(p);
                        status = evc_file_write(file, xpr);
                        VAR_DECR_REF(xpr);
                }
                VAR_DECR_REF(p);
                if (status < 0) {
                        res = ErrorVar;
                        break;
                }
        }

        if (res != ErrorVar) {
                status = evc_file_write(file, end);
                if (status < 0)
                        res = ErrorVar;
        }

done:
        return res;
}

/*
 * TODO: Get rid of 'x'/'r' arg, take optional/keyword args, and
 * pass them to script by appending them to an array in global variables,
 * perhaps something like sys.scriptargs[-1].  Then pop them off when
 * script returns.  It won't be thread-safe, but I won't be multi-threaded
 * for some time.
 */
static Object *
do_import(Frame *fr)
{
        const char *file_name;
        long mode;
        Object *res, *ex, *kwargs, *arg_wrapper;
        enum { R, X } how;
        FILE *fp;

        if (vm_getargs(fr, "<[]><{}>:import", &arg_wrapper, &kwargs)
            == RES_ERROR) {
                return ErrorVar;
        }
        if (vm_getargs_sv(arg_wrapper, "[sc]:import", &file_name, &mode)
            == RES_ERROR) {
                return ErrorVar;
        }

        if (mode == 'r') {
                how = R; /* read script and return it as a function */
        } else if (mode == 'x') {
                how = X; /* execute script and return its results */
        } else {
                err_setstr(ValueError, "import: incorrect MODE argument");
                return ErrorVar;
        }

        fp = push_path(file_name);
        if (!fp) {
                err_errno("Cannot access '%s' properly", file_name);
                return ErrorVar;
        }
        ex = assemble(file_name, fp, NULL);
        pop_path(fp);

        if (!ex || ex == ErrorVar) {
                if (!err_occurred()) {
                        err_setstr(RuntimeError,
                                   "Failed to import module '%s'", file_name);
                }
                return ErrorVar;
        }

        /* arg settings for script top-level: function(*args, **kwargs) */
        res = funcvar_new_user(ex);
        function_setattr(res, IARG_FUNC_MINARGS, 2);
        function_setattr(res, IARG_FUNC_MAXARGS, 2);
        function_setattr(res, IARG_FUNC_OPTIND, 0);
        function_setattr(res, IARG_FUNC_KWIND, 1);
        if (how == X) {
                Object *args, *func;
                args = array_getslice(arg_wrapper, 2,
                                      seqvar_size(arg_wrapper), 1);
                func = res;
                res = vm_exec_func(fr, func, args, kwargs);
                VAR_DECR_REF(func);
                VAR_DECR_REF(args);
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
        char *msg;
        if (vm_getargs(fr, "[|s]{!}:exit", &msg) == RES_ERROR)
                return ErrorVar;
        if (msg)
                fprintf(stderr, "%s\n", msg);
        /*
         * XXX: This clears some bug traps that will trigger due to an
         * exit from an arbitrary depth in our context.  But it still
         * leaves a lot of objects dangling in memory, especially wrt
         * vm.c.  We need a hook, say, a "__vm_unwind()".
         *
         * FIXME: ...But we can't do that.  We'll know where our stack
         * pointer is (we can pass @fr to __vm_unwind()), but our current
         * implementation does not allow a breadcrumb trail of previous
         * frames to clear.  What if, for example, @fr's parent is from
         * a generator, and therefore runs on a separate stack from the
         * mainline one?
         */
        /* main.c */
        extern void end_program(void);
        extern void vm_clear_frames_for_exit(void);
        vm_clear_frames_for_exit();
        end_program();
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
        Object *nl;

        if (vm_getargs(fr, "[<s>!]{!}:setnl", &nl) == RES_ERROR)
                return ErrorVar;
        if (gbl.nl)
                VAR_DECR_REF(gbl.nl);
        gbl.nl = VAR_NEW_REF(nl);
        return NULL;
}

static Object *
do_exists(Frame *fr)
{
        Object *key;
        bool exists;

        if (vm_getargs(fr, "[<s>!]{!}:exists", &key) == RES_ERROR)
                return ErrorVar;
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
        Object *v;
        if (vm_getargs(fr, "[<*>!]{!}:abs", &v) == RES_ERROR)
                return ErrorVar;
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
        Object *v;

        if (vm_getargs(fr, "[<*>!]{!}:all", &v) == RES_ERROR)
                return ErrorVar;
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
        Object *v;

        if (vm_getargs(fr, "[<*>!]{!}:any", &v) == RES_ERROR)
                return ErrorVar;
        result = var_any(v, &status);
        if (status != RES_OK)
                return ErrorVar;
        return intvar_new((int)result);
}

static Object *
do_length(Frame *fr)
{
        Object *v;
        if (vm_getargs(fr, "[<*>!]{!}:length", &v) == RES_ERROR)
                return ErrorVar;

        if (hasvar_len(v)) {
                return intvar_new(seqvar_size(v));
        } else {
                err_setstr(TypeError, "Invalid type '%s' for length()",
                           typestr(v));
                return ErrorVar;
        }
}

static Object *
do_max(Frame *fr)
{
        Object *arg;

        /* FIXME: Change @arg to be any iterable object, not just an array */
        if (vm_getargs(fr, "<[]>{!}:max", &arg) == RES_ERROR)
                return ErrorVar;
        switch (seqvar_size(arg)) {
        case 0:
                err_va_minargs(arg, 1);
                return ErrorVar;
        case 1:
                return var_max(array_borrowitem(arg, 0));
        default:
                return var_max(arg);
        }
}

static Object *
do_min(Frame *fr)
{
        Object *arg;

        /* FIXME: Change @arg to be any iterable object, not just an array */
        if (vm_getargs(fr, "<[]>{!}:min", &arg) == RES_ERROR)
                return ErrorVar;
        switch (seqvar_size(arg)) {
        case 0:
                err_va_minargs(arg, 1);
                return ErrorVar;
        case 1:
                return var_min(array_borrowitem(arg, 0));
        default:
                return var_min(arg);
        }
}

static Object *
do_ord(Frame *fr)
{
        Object *str;
        long ord;

        if (vm_getargs(fr, "[<c>!]{!}:ord", &str) == RES_ERROR)
                return ErrorVar;

        ord = string_ord(str, 0);
        bug_on(ord < 0L);
        return intvar_new(ord);
}

static Object *
do_disassemble(Frame *fr)
{
        /* TODO: optional file arg */
        Object *method, *func, *ex;

        if (vm_getargs(fr, "[<*>!]{!}:disassemble", &func) == RES_ERROR)
                return ErrorVar;

        method = NULL;
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
                /* TODO: if string, compile and disassemble that */
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

static Object *
do_eval(Frame *fr)
{
        char *expr;
        Object *ex, *ret;

        if (vm_getargs(fr, "[s!]{!}:eval", &expr) == RES_ERROR)
                return ErrorVar;
        ex = assemble_string(expr, true);
        if (!ex || ex == ErrorVar) {
                ret = ex;
        } else {
                ret = vm_exec_script(ex, fr);
                VAR_DECR_REF(ex);
        }
        return ret;
}

static const struct type_method_t builtin_inittbl[] = {
        {"abs",    do_abs},
        {"all",    do_all},
        {"any",    do_any},
        {"dir",    do_dir},
        {"disassemble", do_disassemble},
        {"eval",   do_eval},
        {"getattr", do_getattr},
        {"hash",   do_hash},
        {"length", do_length},
        {"min",    do_min},
        {"max",    do_max},
        {"ord",    do_ord},
        {"print",  do_print},
        {"setattr", do_setattr},
        {"setnl",  do_setnl},
        {"typeof", do_typeof},
        /* XXX: maybe exit should be a method of __gbl__._sys */
        {"exit",   do_exit},
        {"exists", do_exists},
        {"import", do_import},
        { NULL, NULL },
};

void
moduleinit_builtin(void)
{
        static const struct codectbl_t {
                int e;
                const char *name;
        } codectbl[] = {
                { .e = CODEC_UTF8,   .name = "utf-8"    },
                { .e = CODEC_UTF8,   .name = "UTF-8"    },
                { .e = CODEC_UTF8,   .name = "utf8"     },
                { .e = CODEC_UTF8,   .name = "UTF8"     },
                { .e = CODEC_LATIN1, .name = "latin1"   },
                { .e = CODEC_LATIN1, .name = "Latin1"   },
                { .e = CODEC_LATIN1, .name = "LATIN1"   },
                { .e = CODEC_LATIN1, .name = "latin-1"  },
                { .e = CODEC_LATIN1, .name = "Latin-1"  },
                { .e = CODEC_LATIN1, .name = "LATIN-1"  },
                /* XXX iso-88something-something... */
                { .e = CODEC_ASCII,  .name = "ascii"    },
                { .e = CODEC_ASCII,  .name = "ASCII"    },
                { .e = -1,           .name = NULL       },
        };
        const struct codectbl_t *t;

        Object *k = stringvar_from_ascii("_builtins");
        Object *o = dictvar_from_methods(NULL, builtin_inittbl);
        dict_setitem(GlobalObject, k, o);
        VAR_DECR_REF(k);
        VAR_DECR_REF(o);

        /*
         * Anywhere I could initialize this seems inappropriate, so I'll
         * just initialize it here.
         */
        bug_on(!!gbl.mns[MNS_CODEC]);
        Object *codecs = dictvar_new();
        for (t = codectbl; t->name != NULL; t++) {
                o = intvar_new(t->e);
                k = stringvar_new(t->name);
                dict_setitem(codecs, k, o);

                /* Reverse key-value for some default names */
                if (!strcmp(t->name, "utf-8") ||
                    !strcmp(t->name, "Latin1") ||
                    !strcmp(t->name, "ascii")) {
                        dict_setitem(codecs, o, k);
                }

                VAR_DECR_REF(k);
                VAR_DECR_REF(o);
        }
        gbl.mns[MNS_CODEC] = codecs;
}
