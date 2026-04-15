/*
 * global.c - constructor/destructor for GlobalObject & gbl
 *
 *    - GlobalObject will appear to user as __gbl__.
 *    - gbl is for the C code, containing things like string consts
 *      and such.
 *
 * TODO: Separate the exception stuff and put in in another C file.
 *
 * XXX REVISIT: global/gbl_xxxx is now a misleading name.
 * Maybe runtime/rt_xxx is more appropriate?
 */
#include <evilcandy/vm.h>
#include <evilcandy/global.h>
#include <evilcandy/types/bytes.h>
#include <evilcandy/types/class.h>
#include <evilcandy/types/function.h>
#include <evilcandy/types/dict.h>
#include <evilcandy/types/string.h>
#include <evilcandy/types/set.h>
#include <evilcandy/types/number_types.h>
#include <internal/init.h>
#include <internal/err.h>
#include <internal/cwd.h>
#include <internal/global.h>
#include <internal/codec.h>
#include <internal/import.h>
#include <internal/token.h>
#include <internal/types/string.h>
#include <internal/types/internal_types.h>

static struct global_t {
        bool interactive;
        Object *nl;
        Object *stdout_file;
        Object *strconsts[N_STRCONST];
        Object *one;
        Object *zero;
        Object *empty_bytes;
        Object *cwd;
        Object *mns[N_MNS];
        /* c-api handles to some built-in classes */
        Object *classes[N_GBL_CLASSES];
        Object *interned_strings; /*< a set */

        /*
         * TODO: vm in vm.c belongs here.
         */
        struct {
                struct gbl_token_subsys_t *token;       /*< token.c */
                struct gbl_codec_subsys_t *codec;       /*< codec.c */
                struct gbl_err_subsys_t *err;           /*< err.c */
                struct gbl_import_subsys_t *import;     /*< import.c */
        } subsys;
} gbl;

Object *ErrorVar;
Object *NullVar;
Object *GlobalObject;

Object *ArgumentError;
Object *KeyError;
Object *IndexError;
Object *NameError;
Object *NumberError;
Object *NotImplementedError;
Object *RangeError;
Object *RecursionError;
Object *RuntimeError;
Object *SyntaxError;
Object *SystemError;
Object *TypeError;
Object *ValueError;

#define STRCONST_CSTR(X) [STRCONST_IDX_##X] = #X
static void
initialize_string_consts(void)
{
        /* Keep sync'd with evcenums.h */
        static const char *STRCONST_CSTRS[N_STRCONST] = {
                STRCONST_CSTR(byteorder),
                STRCONST_CSTR(encoding),
                STRCONST_CSTR(end),
                STRCONST_CSTR(file),
                STRCONST_CSTR(imag),
                STRCONST_CSTR(keepends),
                STRCONST_CSTR(maxsplit),
                STRCONST_CSTR(real),
                STRCONST_CSTR(sep),
                STRCONST_CSTR(sorted),
                STRCONST_CSTR(tabsize),
                STRCONST_CSTR(_sys),
                STRCONST_CSTR(import_path),
                STRCONST_CSTR(breadcrumbs),
                STRCONST_CSTR(addr),
                STRCONST_CSTR(flags),
                STRCONST_CSTR(_priv),
                STRCONST_CSTR(closefd),
                STRCONST_CSTR(buffering),
                STRCONST_CSTR(null),
                STRCONST_CSTR(_),
                STRCONST_CSTR(get),
                STRCONST_CSTR(set),
                STRCONST_CSTR(__str__),
                STRCONST_CSTR(__init__),
                STRCONST_CSTR(read),
                STRCONST_CSTR(write),
                STRCONST_CSTR(socket),
                STRCONST_CSTR(close),
                STRCONST_CSTR(__optarg__),
                STRCONST_CSTR(__kwarg__),
                [STRCONST_IDX_spc] = " ",
                [STRCONST_IDX_mpty] = "",
                [STRCONST_IDX_wtspc] = " \r\n\t\v\f",
                [STRCONST_IDX_locked_array_str] = "[...]",
                [STRCONST_IDX_locked_dict_str] = "{...}",
                [STRCONST_IDX_nomsg] = "(no message provided)",
                [STRCONST_IDX_emptyset] = "set()",
                [STRCONST_IDX_dot_evc] = ".evc",
        };

        int i;
        for (i = 0; i < N_STRCONST; i++) {
                gbl.strconsts[i] = set_intern(
                                        gbl.interned_strings,
                                        stringvar_new(STRCONST_CSTRS[i]));
        }
}
#undef STRCONST_CSTR

static void
initialize_global_object(void)
{
        Object *k, *o;

        /* gotta set this early because moduleinit_sys needs it */
        gbl.cwd = evc_getcwd();

        GlobalObject = dictvar_new();
        moduleinit_sys();
        moduleinit_builtin();
        moduleinit_math();
        moduleinit_io();
        moduleinit_socket();

        k = stringvar_from_ascii("__gbl__");
        vm_add_global(k, GlobalObject);
        VAR_DECR_REF(k);

        o = dict_getitem_cstr(GlobalObject, "_sys");
        bug_on(!o);
        gbl.nl = stringvar_from_ascii("\n");
        gbl.stdout_file = dict_getitem_cstr(o, "stdout");
        bug_on(!gbl.nl || !gbl.stdout_file);
        VAR_DECR_REF(o);

        gbl.subsys.codec  = codec_init_gbl();
        gbl.subsys.token  = token_init_gbl();
        gbl.subsys.import = import_init_gbl();
        gbl.subsys.err    = err_init_gbl();

        gbl.one         = intvar_new(1LL);
        gbl.zero        = intvar_new(0LL);
        gbl.empty_bytes = bytesvar_new((unsigned char *)"", 0);

        o = dict_getitem_cstr(GlobalObject, "_builtins");
        dict_add_to_globals(o);
        VAR_DECR_REF(o);
}

static Object *
exception_initcall(Frame *fr)
{
        Object *self = vm_get_this(fr);
        Object *msgname = stringvar_new("message");
        Object *message;
        enum result_t res;

        if (vm_getargs(fr, "[<s>!]{!}:__exception_init__", &message)
                       == RES_ERROR) {
                return ErrorVar;
        }
        res = var_setattr(fr, self, msgname, message);
        VAR_DECR_REF(msgname);
        return res == RES_OK ? NULL : ErrorVar;
}

static Object *
make_base_exception(const char *name)
{
        Object *method_name, *method_func, *class_name, *dict, *exception;

        class_name = stringvar_new(name);
        /* FIXME: I want to intern this, but we're too early. */
        method_name = stringvar_new("__init__");
        method_func = funcvar_new_intl(exception_initcall);

        dict = dictvar_new();
        dict_setitem(dict, method_name, method_func);
        VAR_DECR_REF(method_name);
        VAR_DECR_REF(method_func);

        exception = classvar_new(NULL, dict, class_name, NULL);

        VAR_DECR_REF(class_name);
        VAR_DECR_REF(dict);

        return exception;
}

static Object *
make_exception(const char *name, Object *from)
{
        Object *dict, *subclass, *nameobj;
        bug_on(!from);

        nameobj = stringvar_new(name);
        dict = dictvar_new();
        subclass = classvar_new(from, dict, nameobj, NULL);

        vm_add_global(nameobj, subclass);

        VAR_DECR_REF(nameobj);
        VAR_DECR_REF(dict);
        return subclass;
}

#define MAKE_EXCEPTION(X) \
        do { X = make_exception(#X, ErrorVar); } while (0)

void
cfile_init_global(void)
{
        gbl.interned_strings = setvar_new(NULL);
        initialize_string_consts();
        /*
         * Keep this before initialize_global_object - We need it for
         * early calls to arrayvar_new(size) when size>0.
         */
        NullVar  = emptyvar_new();

        initialize_global_object();

        ErrorVar = make_base_exception("ErrorVar");

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
}

void
cfile_deinit_global(void)
{
        int i;
        VAR_DECR_REF(GlobalObject);

        VAR_DECR_REF(gbl.stdout_file);
        VAR_DECR_REF(gbl.nl);
        VAR_DECR_REF(gbl.one);
        VAR_DECR_REF(gbl.zero);
        VAR_DECR_REF(gbl.empty_bytes);
        VAR_DECR_REF(gbl.cwd);

        for (i = 0; i < N_STRCONST; i++)
                VAR_DECR_REF(gbl.strconsts[i]);

        for (i = 0; i < N_MNS; i++) {
                if (gbl.mns[i])
                        VAR_DECR_REF(gbl.mns[i]);
        }

        for (i = 0; i < N_GBL_CLASSES; i++) {
                if (gbl.classes[i])
                        VAR_DECR_REF(gbl.classes[i]);
        }

        VAR_DECR_REF(gbl.interned_strings);

        codec_deinit_gbl(gbl.subsys.codec);
        token_deinit_gbl(gbl.subsys.token);
        import_deinit_gbl(gbl.subsys.import);
        err_deinit_gbl(gbl.subsys.err);

        memset(&gbl, 0, sizeof(gbl));

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
        VAR_DECR_REF(ErrorVar);
        VAR_DECR_REF(NullVar);
}

Object *
gbl_borrow_bool(bool cond)
{
        if (cond)
                return gbl.one;
        else
                return gbl.zero;
}

Object *
gbl_new_bool(bool cond)
{
        return VAR_NEW_REF(gbl_borrow_bool(cond));
}

Object *
gbl_borrow_strconst(enum evc_strconst_t id)
{
        bug_on((unsigned)id >= N_STRCONST);
        return gbl.strconsts[id];
}

Object *
gbl_new_empty_bytes(void)
{
        return gbl.empty_bytes
                ? VAR_NEW_REF(gbl.empty_bytes)
                : bytesvar_new((unsigned char *)"", 0);
}

void
gbl_set_interactive(bool is_interactive)
{
        gbl.interactive = is_interactive;
}

bool
gbl_is_interactive(void)
{
        return gbl.interactive;
}

Object *
gbl_borrow_mns_dict(enum gbl_mns_t mns)
{
        bug_on((unsigned)mns >= N_MNS);
        return gbl.mns[mns];
}

void
gbl_set_mns_dict(enum gbl_mns_t mns, Object *dict)
{
        bug_on((unsigned)mns >= N_MNS);
        gbl.mns[mns] = dict;
}

Object *
gbl_intern_string(Object *str)
{
        return set_intern(gbl.interned_strings, str);
}

Object *
gbl_borrow_builtin_class(enum gbl_class_idx_t idx)
{
        bug_on((unsigned)idx >= N_GBL_CLASSES);
        return gbl.classes[idx];
}

Object *
gbl_cwd(void)
{
        return gbl.cwd;
}

Object *
gbl_get_nl(void)
{
        return gbl.nl;
}

void
gbl_set_nl(Object *nl)
{
        if (gbl.nl)
                VAR_DECR_REF(gbl.nl);
        gbl.nl = VAR_NEW_REF(nl);
}

Object *
gbl_get_stdout(void)
{
        return gbl.stdout_file;
}

/* This produces a reference for class */
void
gbl_set_builtin_class(enum gbl_class_idx_t idx, Object *class)
{
        bug_on((unsigned)idx >= N_GBL_CLASSES);
        if (gbl.classes[idx])
                VAR_DECR_REF(gbl.classes[idx]);
        gbl.classes[idx] = VAR_NEW_REF(class);
}

/*
 * TODO; think up a good test of these bug traps.
 * No one should call these functions in between
 * xxx_init_gbl() and xxx_deinit_gbl().
 */
struct gbl_token_subsys_t *
gbl_get_token_subsys(void)
{
        bug_on(!gbl.subsys.token);
        return gbl.subsys.token;
}

struct gbl_codec_subsys_t *
gbl_get_codec_subsys(void)
{
        bug_on(!gbl.subsys.codec);
        return gbl.subsys.codec;
}

struct gbl_import_subsys_t *
gbl_get_import_subsys(void)
{
        return gbl.subsys.import;
}

struct gbl_err_subsys_t *
gbl_get_err_subsys(void)
{
        bug_on(!gbl.subsys.err);
        return gbl.subsys.err;
}

