#include <evilcandy.h>
#include <sys/socket.h>

/**
 * intenum_toul - Interpret a function argument when it is expected to be
 *                text enumerating an integer >= zero.
 * @enumstr: Argument from the script, which is expected to be a string
 *           enumerating the value.
 * @valid:   Array of permitted values for this argument.
 * @argname: Name of argument, for error text.
 *
 * Return: Interpreted enumeration or -1 if invalid argument.
 */
static int
intenum_toul(Object *enumstr, const int *valid, const char *argname)
{
        Object *enumi;
        int ret;

        bug_on(!enumstr); /* 'arg exists' was checked upstream */
        bug_on(!gbl.socket_enums);

        if (arg_type_check(enumstr, &StringType) != 0)
                return -1;
        enumi = dict_getitem(gbl.socket_enums, enumstr);
        if (!enumi) {
                VAR_DECR_REF(enumi);
                goto err;
        }
        ret = intvar_toi(enumi);
        VAR_DECR_REF(enumi);

        if (valid) {
                while (*valid >= 0) {
                        if (ret == *valid)
                                return ret;
                        valid++;
                }
                goto err;
        }
        return ret;

err:
        err_setstr(ValueError, "invalid %s arg: %s",
                   argname, string_cstring(enumstr));
        return -1;
}

/*
 * TODO: kwargs, default to AF_UNIX, SOCK_STREAM,
 * option to make socketpair instead of single socket.
 */
static Object *
do_socket(Frame *fr)
{
        static const int VALID_DOMAINS[] = { AF_INET, AF_UNIX, -1 };
        static const int VALID_TYPES[] = {
                SOCK_STREAM, SOCK_DGRAM, SOCK_SEQPACKET, -1
        };
        int fd, domain, type;
        Object *skobj;

        domain = intenum_toul(frame_get_arg(fr, 0), VALID_DOMAINS, "domain");
        if (domain < 0)
                return ErrorVar;
        type = intenum_toul(frame_get_arg(fr, 1), VALID_TYPES, "type");
        if (type < 0)
                return ErrorVar;

        /*
         * FIXME: fd is an integer, so there's no way to know to close
         * it if this socket goes out of scope.  This requires one of
         * the following solutions:
         *
         * 1. Use a FileType var instead of fd
         * 2. Add policy that user must take care to close fd before
         *    socket goes out of scope
         * 3. Add policy that if a dict has a '__cleanup__' entry and
         *    it's a function, execute that from dict_reset().
         * 4. Make a field in struct dictvar_t called .cleanup, a C-only
         *    (no VM or frames) function. Have a dict.c's .reset() call
         *    this callback if it was installed.
         *
         * These all suck. #1 is heavy-handed, since a socket might only
         * wish to do some light-weight IPC stuff. #2 damn near
         * guarantees the piling up of zombie sockets, since programmers
         * of high-level languages tend to be sloppy.  #3 would be best,
         * but the frame handle is not passed to VAR_DECR_REF(), which
         * means we can't make downstream calls to vm_exec_func() from an
         * object's .reset method. #4 solves this, but only for built-in
         * dicts like this one; it doesn't allow for a user-defined
         * cleanup function written in the script.
         */
        fd = socket(domain, type, 0);
        if (fd < 0) {
                err_errno("Cannot create socket");
                return ErrorVar;
        }

        skobj = var_from_format("{sisisisO}",
                                "fd", fd, "domain", domain,
                                "type", type, "addr", NullVar);

        /*
         * TODO: fill skobj with bind, accept, listen, etc.,
         * probably with a struct type_inittbl_t
         */
        return skobj;
}

static const struct type_inittbl_t socket_inittbl[] = {
        /* TODO: socketpair, or else get rid of this extra layer */
        V_INITTBL("socket", do_socket, 2, 2, -1, -1),
        TBLEND,
};

static void
initdict(void)
{
        static const struct dtbl_t {
                int e;
                const char *name;
        } dtbl[] = {
#define DTB(n_) { .e = n_, .name = #n_ }
                DTB(AF_UNIX),
                DTB(AF_INET),
                DTB(PF_UNIX),
                DTB(PF_INET),
                /* TODO: The rest of the AF_.../PF_... */
                DTB(SOCK_STREAM),
                DTB(SOCK_DGRAM),
                DTB(SOCK_SEQPACKET),
#undef DTB
                { -1, NULL },
        };
        const struct dtbl_t *t;

        bug_on(gbl.socket_enums != NULL);
        gbl.socket_enums = dictvar_new();

        for (t = dtbl; t->name != NULL; t++) {
                Object *v = intvar_new(t->e);
                Object *k = stringvar_new(t->name);
                dict_setitem(gbl.socket_enums, k, v);
                VAR_DECR_REF(v);
                VAR_DECR_REF(k);
        }
}

static Object *
create_socket_instance(Frame *fr)
{
        if (!gbl.socket_enums)
                initdict();
        return dictvar_from_methods(NULL, socket_inittbl);
}

void
moduleinit_socket(void)
{
        Object *k = stringvar_new("_socket");
        Object *o = var_from_format("<xmM>",
                                    create_socket_instance, 0, 0);
        dict_setitem(GlobalObject, k, o);
        VAR_DECR_REF(k);
        VAR_DECR_REF(o);
}

