/*
 * global.c - constructor/destructor for GlobalObject & gbl
 *
 *    - GlobalObject will appear to user as __gbl__.
 *    - gbl is for the C code, containing things like string consts
 *      and such.
 */
#include <evilcandy.h>

struct global_t gbl;

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
        int i;

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

        for (i = 0; i < N_CODEC; i++)
                gbl.codecs[i] = intvar_new(i);

        gbl.neg_one     = intvar_new(-1LL);
        gbl.one         = intvar_new(1LL);
        gbl.zero        = intvar_new(0LL);
        gbl.eight       = intvar_new(8LL);
        gbl.empty_bytes = bytesvar_new((unsigned char *)"", 0);
        gbl.spc_bytes   = bytesvar_new((unsigned char *)" ", 1);
        gbl.fzero       = floatvar_new(0.0);

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
        res = var_setattr(self, msgname, message);
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

        exception = classvar_new(NULL, dict, class_name);

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
        subclass = classvar_new(from, dict, nameobj);

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
        VAR_DECR_REF(gbl.neg_one);
        VAR_DECR_REF(gbl.one);
        VAR_DECR_REF(gbl.zero);
        VAR_DECR_REF(gbl.eight);
        VAR_DECR_REF(gbl.empty_bytes);
        VAR_DECR_REF(gbl.spc_bytes);
        VAR_DECR_REF(gbl.fzero);
        VAR_DECR_REF(gbl.cwd);

        for (i = 0; i < N_STRCONST; i++)
                VAR_DECR_REF(gbl.strconsts[i]);

        for (i = 0; i < N_MNS; i++) {
                if (gbl.mns[i])
                        VAR_DECR_REF(gbl.mns[i]);
        }

        for (i = 0; i < N_CODEC; i++) {
                if (gbl.codecs[i])
                        VAR_DECR_REF(gbl.codecs[i]);
        }

        for (i = 0; i < N_GBL_CLASSES; i++) {
                if (gbl.classes[i])
                        VAR_DECR_REF(gbl.classes[i]);
        }

        VAR_DECR_REF(gbl.interned_strings);

        if (gbl.iatok.line)
                efree(gbl.iatok.line);
        memset(&gbl.iatok, 0, sizeof(gbl.iatok));

        if (gbl.exception_last)
                VAR_DECR_REF(gbl.exception_last);

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

/* not really sure where's a good place to put this */
char *
codec_str(int codec, char *buf, size_t size)
{
        const char *ret = "?";
        Object *str = codec_strobj(codec);
        if (str) {
                if (isvar_string(str))
                        ret = string_cstring(str);
                VAR_DECR_REF(str);
        }
        memset(buf, 0, size);
        strncpy(buf, ret, size-1);
        return buf;
}

Object *
codec_strobj(int codec)
{
        Object *value;
        if (codec >= N_CODEC
            || gbl.codecs[codec] == NULL
            || gbl.mns[MNS_CODEC] == NULL) {
                return NULL;
        }
        value = dict_getitem(gbl.mns[MNS_CODEC], gbl.codecs[codec]);
        if (!value || !isvar_string(value))
                return NULL;

        return value;
}
