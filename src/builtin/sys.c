#include <evilcandy.h>

#define STDIO_FMT          "s/fnsmi/"
#define STDIO_ARGS(X, Y)   #X, X, "<" #X ">", FMODE_##Y | FMODE_PROTECT

void
moduleinit_sys(void)
{
        Object *o, *k;

        o = var_from_format("{" STDIO_FMT
                                STDIO_FMT
                                STDIO_FMT
                                "O[]"
                                "O[Os]"
                            "}",
                            STDIO_ARGS(stdin, READ),
                            STDIO_ARGS(stdout, WRITE),
                            STDIO_ARGS(stderr, WRITE),
                            STRCONST_ID(breadcrumbs),
                            STRCONST_ID(import_path), gbl.cwd, RCDATADIR);
        dict_setitem(GlobalObject, STRCONST_ID(_sys), o);

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
