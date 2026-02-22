/*
 * TODO: Rewrite to be entirely a dict instead of a new
 *       built-in class.
 */
#include <evilcandy.h>
#include <sys/socket.h>

/* FIXME: need to de-initialize these at end of program */
static bool socketvar_init = false;

/* forward-declared so it could be put beneath SocketType */
static bool isvar_socket(Object *skobj);

struct socketvar_t {
        Object base;
        int fd;
        int domain;
        int type;
};

#define O2SK(o_)        ((struct socketvar_t *)(o_))

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

static Object *
socket_str(Object *skobj)
{
        struct buffer_t b;
        bug_on(!isvar_socket(skobj));

        buffer_init(&b);
        /* TODO: Status, better ID */
        buffer_printf(&b, "<socket %p>", skobj);
        return stringvar_from_buffer(&b);
}

static int
socket_cmp(Object *a, Object *b)
{
        int fda, fdb;

        bug_on(!isvar_socket(a) || !isvar_socket(b));

        fda = O2SK(a)->fd;
        fdb = O2SK(b)->fd;

        return OP_CMP(fda, fdb);
}

static bool
socket_cmpz(Object *skobj)
{
        return 0;
}

static void
socket_reset(Object *skobj)
{
        /* TODO: delete everything in skobj */
#warning "not implemented yet; mem leaks will be detected"
}

static const struct type_prop_t socket_prop_getsets[] = {
        /* TODO: status and properties of socket */
        { .name = NULL },
};
static const struct type_inittbl_t socket_cb_methods[] = {
        /* TODO: listen, bind, send, etc. */
        TBLEND,
};

static struct type_t SocketType = {
        .flags  = 0,
        .name   = "socket",
        .opm    = NULL,
        .cbm    = socket_cb_methods,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct socketvar_t),
        .str    = socket_str,
        .cmp    = socket_cmp,
        .cmpz   = socket_cmpz,
        .reset  = socket_reset,
        .prop_getsets   = socket_prop_getsets,
};

static bool
isvar_socket(Object *skobj)
{
        return skobj->v_type == &SocketType;
}

static Object *
socketvar_new(int fd, int domain, int type)
{
        Object *skobj;
        struct socketvar_t *sk;

        if (!socketvar_init) {
                var_initialize_type(&SocketType);
                socketvar_init = true;
        }
        skobj = var_new(&SocketType);
        sk = O2SK(skobj);

        sk->domain = domain;
        sk->type = type;
        sk->fd = fd;
        return skobj;
}

/* TODO: kwargs, default to AF_UNIX, SOCK_STREAM */
static Object *
do_socket(Frame *fr)
{
        static const int VALID_DOMAINS[] = { AF_INET, AF_UNIX, -1 };
        static const int VALID_TYPES[] = {
                SOCK_STREAM, SOCK_DGRAM, SOCK_SEQPACKET, -1
        };
        int fd, domain, type;

        domain = intenum_toul(frame_get_arg(fr, 0), VALID_DOMAINS, "domain");
        if (domain < 0)
                return ErrorVar;
        type = intenum_toul(frame_get_arg(fr, 1), VALID_TYPES, "type");
        if (type < 0)
                return ErrorVar;

        fd = socket(domain, type, 0);
        if (fd < 0) {
                err_errno("Cannot create socket");
                return ErrorVar;
        }

        return socketvar_new(fd, domain, type);
}

static const struct type_inittbl_t socket_inittbl[] = {
        /* TODO: socketpair */
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


/*
 * FIXME: Better than below, have code near initdict and
 * var_initialize_type call something like var_schedule_cleanup
 * for de-init.
 */

/* Exists for symmetry with cfile_deinit_socket */
void
cfile_init_socket(void)
{
        ;
}

void
cfile_deinit_socket(void)
{
#warning "not used yet; mem leaks will be detected"
        /* TODO: typedef destroy SocketType */
}

