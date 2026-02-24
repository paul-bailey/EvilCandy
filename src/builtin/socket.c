#include <evilcandy.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>

struct evc_sockaddr_t {
        union {
                struct sockaddr sa;
                struct sockaddr_in in;
                struct sockaddr_un un;
        };
};

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

static void
skerr_field(const char *how, Object *what)
{
        bug_on(!what || !isvar_string(what));
        err_setstr(TypeError, "Socket %s '%s' field",
                   how, string_cstring(what));
}

#define skerr_missing(what)     skerr_field("missing", what)
#define skerr_mismatch(what)    skerr_field("has invalid", what)

static enum result_t __attribute__((unused))
socket_unpack_int(Object *skobj, Object *name, int *x)
{
        enum result_t res = RES_ERROR;
        Object *xo = dict_getitem(skobj, name);
        if (!xo) {
                skerr_missing(name);
                goto out_noref;
        }
        if (!isvar_int(xo)) {
                skerr_mismatch(name);
                goto out_ref;
        }
        *x = intvar_toi(xo);
        if (err_occurred()) {
                skerr_mismatch(name);
                goto out_ref;
        }
        res = RES_OK;
out_ref:
        VAR_DECR_REF(xo);
out_noref:
        return res;
}

#define socket_unpack__(sk, p, what) \
        socket_unpack_int(sk, STRCONST_ID(what), p)

#define socket_unpack_fd(sk, p)         socket_unpack__(sk, p, fd)
#define socket_unpack_domain(sk, p)     socket_unpack__(sk, p, domain)
#define socket_unpack_type(sk, p)       socket_unpack__(sk, p, type)
#define socket_unpack_proto(sk, p)      socket_unpack__(sk, p, proto)

/*
 * @addr_len is set by user to size allocated for @addr; this function
 *      will reduce it if actual address is smaller
 */
static enum result_t __attribute__((unused))
socket_unpack_address_(Object *skobj,
                       struct evc_sockaddr_t *addr, Object *name)
{
        enum result_t res = RES_ERROR;
        Object *ao = dict_getitem(skobj, name);
        if (!ao) {
                skerr_missing(name);
                goto out_noref;
        }
        if (!isvar_bytes(ao) ||
            seqvar_size(ao) != sizeof(struct evc_sockaddr_t)) {
                skerr_mismatch(name);
                goto out_ref;
        }
        memcpy(addr, bytes_get_data(ao), sizeof(*addr));
        res = RES_OK;
out_ref:
        VAR_DECR_REF(ao);
out_noref:
        return res;
}

static enum result_t __attribute__((unused))
socket_unpack_address(Object *skobj, struct evc_sockaddr_t *addr)
{
        return socket_unpack_address_(skobj, addr, STRCONST_ID(addr));
}

static enum result_t __attribute__((unused))
socket_unpack_raddress(Object *skobj, struct evc_sockaddr_t *addr)
{
        return socket_unpack_address_(skobj, addr, STRCONST_ID(raddr));
}

/* FIXME: is it too naive to have the address be just a string? */

/*
 * remote_sk = sk.accept();
 *
 * remote_sk's is a socket whose address will be the address of the
 * remote host.
 */
static Object *
do_accept(Frame *fr)
{
        err_setstr(NotImplementedError, "accept not implemented");
        return ErrorVar;
}

/*
 * sk.bind(address);
 *
 * address is a string.  It must make sense for the family used.
 * AF_INET:
 *      address may be 'localhost', 'INADDR_ANY', or an address of the
 *      form '#.#.#.#', ie. '192.168.0.1'.  It may not be a domain name
 *      like 'mycomputer@mycompany.net'.
 * AF_UNIX:
 *      address will look like a file name
 */
static Object *
do_bind(Frame *fr)
{
        err_setstr(NotImplementedError, "bind not implemented");
        return ErrorVar;
}

/*
 * sk.connect(address);
 * address is a string.  It must make sens for the family used.
 * AF_INET:
 *      address may be of the form '#.#.#.#', ie. '192.168.0.1'.
 *      It may not be a domain name like 'www.google.com'.  It may also
 *      be 'localhost' or 'INADDR_ANY'.
 * AF_UNIX:
 *      address will look like a file name
 */
static Object *
do_connect(Frame *fr)
{
        err_setstr(NotImplementedError, "connect not implemented");
        return ErrorVar;
}

/*
 * sk.listen(backlog);
 * backlog is an integer >= 0
 *
 * XXX REVISIT: Until I either make this multi-threaded or I add
 * fork(), exec(), etc., calls, a backlog > 1 makes no sense.
 */
static Object *
do_listen(Frame *fr)
{
        err_setstr(NotImplementedError, "listen not implemented");
        return ErrorVar;
}

/*
 * msg = sk.recv(flags);
 *
 * @flags is either a tuple of strings containing zero or more of the
 * following: MSG_OOB, MSG_PEEK, MSG_WAITALL. (TODO: only support the
 * most portable of these.)
 *
 * @msg will be replied as a bytes object.
 */
static Object *
do_recv(Frame *fr)
{
        err_setstr(NotImplementedError, "recv not implemented");
        return ErrorVar;
}

/*
 * res = sk.recvfrom(flags);
 *
 * @flags is the same as with sk.recv.
 * @res is a tuple of the form (msg, addr)
 *      @msg is a bytes object
 *      @addr is the remote address
 */
static Object *
do_recvfrom(Frame *fr)
{
        err_setstr(NotImplementedError, "recvfrom not implemented");
        return ErrorVar;
}

/*
 * sk.send(msg, flags)
 *
 * msg is a bytes object.
 * @flags is a tuple of size zero to two which may contain any of the
 *      following strings: 'MSG_OOB', 'MSG_DONTROUTE'
 */
static Object *
do_send(Frame *fr)
{
        err_setstr(NotImplementedError, "send not implemented");
        return ErrorVar;
}

/*
 * sk.sendto(msg, addr, flags)
 *
 * @msg is a bytes object.
 * @addr is the address of the remote sender
 * @flags is a tuple of size zero to two which may contain any of the
 *      following strings: 'MSG_OOB', 'MSG_DONTROUTE'
 */
static Object *
do_sendto(Frame *fr)
{
        err_setstr(NotImplementedError, "sendto not implemented");
        return ErrorVar;
}

/*
 * TODO: kwargs, default to AF_UNIX, SOCK_STREAM,
 * option to make socketpair instead of single socket.
 */
static Object *
do_socket(Frame *fr)
{
        static const int VALID_DOMAINS[] = {
                AF_INET,
                AF_UNIX,
                -1
        };
        static const int VALID_TYPES[] = {
                SOCK_STREAM,
                SOCK_DGRAM,
                SOCK_SEQPACKET,
                SOCK_RAW,
                -1
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

        skobj = var_from_format("{OiOiOiOOOOOO}",
                                STRCONST_ID(fd),     fd,
                                STRCONST_ID(domain), domain,
                                STRCONST_ID(type),   type,
                                STRCONST_ID(proto),  NullVar,
                                STRCONST_ID(addr),   NullVar,
                                STRCONST_ID(raddr),  NullVar);

        static const struct type_inittbl_t sockmethods_inittbl[] = {
                V_INITTBL("accept",   do_accept,   0, 0, -1, -1),
                V_INITTBL("bind",     do_bind,     1, 1, -1, -1),
                V_INITTBL("connect",  do_connect,  1, 1, -1, -1),
                V_INITTBL("listen",   do_listen,   1, 1, -1, -1),
                V_INITTBL("recv",     do_recv,     1, 1, -1, -1),
                V_INITTBL("recvfrom", do_recvfrom, 1, 1, -1, -1),
                V_INITTBL("send",     do_send,     2, 2, -1, -1),
                V_INITTBL("sendto",   do_sendto,   3, 3, -1, -1),
                /* TODO: [gs]etsockopt and common ioctl wrappers */
                TBLEND,
        };
        return dictvar_from_methods(skobj, sockmethods_inittbl);
}

static const struct type_inittbl_t socket_inittbl[] = {
        /* TODO: gethostbyname, socketpair, getaddrinfo */
        V_INITTBL("socket",  do_socket,  2, 2, -1, -1),
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
                DTB(SOCK_RAW),
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
        Object *skobj;
        if (!gbl.socket_enums)
                initdict();
        skobj = dictvar_new();
        dict_copyto(skobj, gbl.socket_enums);
        return dictvar_from_methods(skobj, socket_inittbl);
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

