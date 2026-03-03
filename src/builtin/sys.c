#include <evilcandy.h>
#include <unistd.h>

#define STDIO_FMT          "s/fnsmi/"
#define STDIO_ARGS(X, Y)   #X, X, "<" #X ">", FMODE_##Y | FMODE_PROTECT

/*
 * XXX: Remove this hack-declare
 */
void
moduleinit_sys(void)
{
        Object *o, *k, *v;

        o = var_from_format("{O[]O[Os]}",
                            STRCONST_ID(breadcrumbs),
                            STRCONST_ID(import_path), gbl.cwd, RCDATADIR);
        dict_setitem(GlobalObject, STRCONST_ID(_sys), o);

        v = evc_file_open(STDIN_FILENO, "<stdin>",
                          false, false, CODEC_UTF8, 1);
        bug_on(v == ErrorVar);
        k = stringvar_new("stdin");
        dict_setitem(o, k, v);
        VAR_DECR_REF(v);
        VAR_DECR_REF(k);

        v = evc_file_open(STDOUT_FILENO, "<stdout>",
                          false, false, CODEC_UTF8, 1);
        bug_on(v == ErrorVar);
        k = stringvar_new("stdout");
        dict_setitem(o, k, v);
        VAR_DECR_REF(v);
        VAR_DECR_REF(k);

        k = stringvar_new("stderr");
        v = evc_file_open(STDERR_FILENO, "<stderr>",
                          false, false, CODEC_UTF8, 1);
        bug_on(v == ErrorVar);
        dict_setitem(o, k, v);
        VAR_DECR_REF(v);
        VAR_DECR_REF(k);

        k = stringvar_new("sys");
        vm_add_global(k, o);
        VAR_DECR_REF(k);

        VAR_DECR_REF(o);
}
#undef STDIO_ARGS
#undef STDIO_FMT

Object *
sys_getitem(Object *key)
{
        Object *ret, *o;

        o = dict_getitem(GlobalObject, STRCONST_ID(_sys));
        bug_on(!o);
        ret = dict_getitem(o, key);
        VAR_DECR_REF(o);
        return ret;
}

Object *
sys_getitem_cstr(const char *key)
{
        Object *ret, *okey;

        okey = stringvar_new(key);
        ret = sys_getitem(okey);
        VAR_DECR_REF(okey);
        return ret;
}
