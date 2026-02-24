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
        static const char *STRCONST_CSTR[N_STRCONST] = {
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
                STRCONST_CSTR(fd),
                STRCONST_CSTR(domain),
                STRCONST_CSTR(type),
                STRCONST_CSTR(proto),
                STRCONST_CSTR(addr),
                STRCONST_CSTR(raddr),
                [STRCONST_IDX_spc] = " ",
                [STRCONST_IDX_mpty] = "",
                [STRCONST_IDX_wtspc] = " \r\n\t\v\f",
                [STRCONST_IDX_locked_array_str] = "[...]",
                [STRCONST_IDX_locked_dict_str] = "{...}",
        };

        int i;
        for (i = 0; i < N_STRCONST; i++)
                gbl.strconsts[i] = stringvar_new(STRCONST_CSTR[i]);
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

        k = stringvar_new("__gbl__");
        vm_add_global(k, GlobalObject);
        VAR_DECR_REF(k);

        o = dict_getitem_cstr(GlobalObject, "_sys");
        bug_on(!o);
        gbl.nl = stringvar_new("\n");
        gbl.stdout_file = dict_getitem_cstr(o, "stdout");
        bug_on(!gbl.nl || !gbl.stdout_file);
        VAR_DECR_REF(o);

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

#define MAKE_EXCEPTION(X) do {                  \
        /*                                      \
         * Do not consume reference.            \
         * There's one in global vars for user  \
         * and one in C for us.                 \
         */                                     \
        X = stringvar_new(#X);                  \
        vm_add_global(X, X);                    \
} while (0)

void
cfile_init_global(void)
{
        initialize_string_consts();
        /*
         * Keep this before initialize_global_object - We need it for
         * early calls to arrayvar_new(size) when size>0.
         */
        NullVar  = emptyvar_new();

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

        ErrorVar = stringvar_new("If you can see this from the console, this is a BUG!!!\n");
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

        if (gbl.socket_enums)
                VAR_DECR_REF(gbl.socket_enums);

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
