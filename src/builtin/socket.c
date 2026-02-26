#include <evilcandy.h>
#include <errno.h>

/* TODO: Auto-conf all these things */
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

struct evc_sockaddr_t {
        union {
                struct sockaddr sa;
                /* Used for AF_INET */
                struct sockaddr_in in;
                /* Used for AF_UNIX */
                struct sockaddr_un un;
        };
};

/* return length of struct sockaddr used */
static ssize_t
dom2alen(int domain)
{
        switch (domain) {
        case AF_INET:
                return sizeof(struct sockaddr_in);
        case AF_UNIX:
                return sizeof(struct sockaddr_un);
        default:
                return -1;
        }
}

static enum result_t
parse_ip_addr(const char *name, struct evc_sockaddr_t *sa, int domain)
{
        /* TODO: Manage 'broadcast' (=255.255.255.255). */
        /* TODO: Use @domain arg, currently only allowing IPv4 */
        /* TODO: Does one of the below methods support "localhost"? */
        memset(sa, 0, sizeof(*sa));
        if (inet_pton(AF_INET, name, &sa->in.sin_addr) > 0) {
                sa->in.sin_family = AF_INET;
                sa->in.sin_len = sizeof(sa->in);
                return RES_OK;
        } else {
                /* Not of the n.n.n.n format? Perform name resolution */
                struct addrinfo hints, *info;
                int res;
                size_t addrlen = sizeof(sa->in);
                memset(&hints, 0, sizeof(hints));
                hints.ai_family = domain;
                res = getaddrinfo(name, NULL, &hints, &info);
                if (res) {
                        err_setstr(SystemError,
                                   "Cannot get address of '%s' (%s)",
                                   name, gai_strerror(res));
                        return RES_ERROR;
                }
                /*
                 * FIXME: Cycle through list, find info which has
                 * matching domain.
                 */
                if (info->ai_addrlen != addrlen) {
                        err_setstr(TypeError,
                                   "Unexpected address length for %s",
                                   name);
                        freeaddrinfo(info);
                        return RES_ERROR;
                }
                memcpy(&sa->in, info->ai_addr, addrlen);
                freeaddrinfo(info);
                return RES_OK;
        }
}

static enum result_t
parse_address_arg(struct evc_sockaddr_t *sa, Object *arg,
                  int domain, const char *fname)
{
        if (domain == AF_UNIX) {
                const char *name;
                if (!isvar_string(arg)) {
                        err_setstr(TypeError, "expected: socket file name");
                        return RES_ERROR;
                }
                name = string_cstring(arg);
                if (strlen(name) > (sizeof(sa->un.sun_path) - 1)) {
                        err_setstr(ValueError, "socket path name too long");
                        return RES_ERROR;
                }
                memset(sa, 0, sizeof(*sa));
                sa->un.sun_family = domain;
                strcpy(sa->un.sun_path, name);
                return RES_OK;
        }

        if (domain == AF_INET) {
                const char *name;
                unsigned short port;
                /*
                 * XXX: don't wanna do this, since fname is only needed
                 * for the error path.
                 */
                char buf[32];
                snprintf(buf, sizeof(buf)-1, "(sh):%s", fname);
                if (vm_getargs_sv(arg, buf, &name, &port)
                    == RES_ERROR) {
                        /* TODO: clearerr if string, try again */
                        return RES_ERROR;
                }
                if (parse_ip_addr(name, sa, domain) == RES_ERROR)
                        return RES_ERROR;
                /*
                 * memset to zero occurs in parse_ip_addr,
                 * so hold off setting port until now.
                 */
                sa->in.sin_port = htons(port);
                return RES_OK;
        }

        err_setstr(NotImplementedError, "Domain not implemented");
        return -1;
}

/*
 * validate_int - Check that @ival is positive and matches one of the
 *                enumerated values in @tbl, which terminates with -1.
 *                @argname is for error reporting.
 */
static enum result_t
validate_int(int ival, const int *tbl, const char *argname)
{
        if (ival > 0LL) {
                while (*tbl >= 0) {
                        if (ival == *tbl)
                                return RES_OK;
                        tbl++;
                }
        }

        err_setstr(ValueError, "invalid %s arg: %d", argname, ival);
        return RES_ERROR;
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

static enum result_t
socket_unpack_open_fd(Object *skobj, int *fd)
{
        if (socket_unpack_fd(skobj, fd) < 0)
                return RES_ERROR;
        if (fd < 0) {
                err_setstr(TypeError, "socket has been closed");
                return RES_ERROR;
        }
        /* XXX: any sanity checks on fd? */
        return RES_OK;
}

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
        int fd, domain;
        Object *skobj, *addrarg;
        struct evc_sockaddr_t sa;
        ssize_t addrlen;

        skobj = vm_get_this(fr);
        addrarg = vm_get_arg(fr, 0);
        bug_on(!addrarg);

        if (socket_unpack_open_fd(skobj, &fd) == RES_ERROR)
                return ErrorVar;
        if (socket_unpack_domain(skobj, &domain) == RES_ERROR)
                return ErrorVar;
        if (parse_address_arg(&sa, addrarg, domain, "bind") == RES_ERROR)
                return ErrorVar;

        addrlen = dom2alen(domain);
        bug_on(addrlen < 0);

        if (bind(fd, &sa.sa, addrlen) < 0) {
                err_errno("bind() failed");
                return ErrorVar;
        }

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
        int fd, domain;
        Object *skobj, *addrarg;
        struct evc_sockaddr_t sa;
        ssize_t addrlen;

        /*
         * XXX REVISIT: some implementations of connect (2) interpret
         * a NULL address argument as "disconnect".  Maybe do the same
         * here?
         */
        skobj = vm_get_this(fr);
        addrarg = vm_get_arg(fr, 0);
        bug_on(!addrarg);

        if (socket_unpack_open_fd(skobj, &fd) < 0)
                return ErrorVar;
        if (socket_unpack_domain(skobj, &domain) < 0)
                return ErrorVar;

        if (parse_address_arg(&sa, addrarg, domain, "connect")
            == RES_ERROR) {
                return ErrorVar;
        }

        addrlen = dom2alen(domain);
        bug_on(addrlen < 0);

        if (connect(fd, &sa.sa, addrlen) < 0) {
                err_errno("Failed to connect");
                return ErrorVar;
        }

        return NULL;
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
        Object *skobj, *bklo;
        int fd, backlog;

        skobj = vm_get_this(fr);
        bklo = vm_get_arg(fr, 0);
        if (arg_type_check(bklo, &IntType) == RES_ERROR)
                return ErrorVar;
        if (socket_unpack_open_fd(skobj, &fd) == RES_ERROR)
                return ErrorVar;
        backlog = intvar_toi(bklo);
        if (err_occurred())
                return ErrorVar;
        if (listen(fd, backlog) < 0) {
                err_errno("failed to listen");
                return ErrorVar;
        }
        return NULL;
}

/*
 * msg = sk.recv(length, [flags=0]);
 *
 * @flags is an integer bitfield containing zero or more of the
 * following flags: MSG_OOB, MSG_PEEK, MSG_WAITALL.
 *
 * @msg will be replied as a bytes object.  Its length may be
 * shorter than the amount requested.
 */
static Object *
do_recv(Frame *fr)
{
        Object *skobj;
        int flags, fd;
        long long length;
        ssize_t n;
        void *buf;

        flags = 0;
        length = 0;
        skobj = vm_get_this(fr);
        if (vm_getargs(fr, "l{|i}:recv", &length,
                       STRCONST_ID(flags), &flags) == RES_ERROR)
                return ErrorVar;
        if (socket_unpack_open_fd(skobj, &fd) == RES_ERROR)
                return ErrorVar;

        if (length < 0LL) {
                err_setstr(ValueError,
                           "recv() may not use a negative buffer size");
                return ErrorVar;
        }

        /* XXX: Ought to hard-cap length here */
        buf = emalloc(length);
        for (;;) {
                n = recv(fd, buf, length, flags);
                if (n > 0)
                        break;
                /* XXX: What about EWOULDBLOCK, EAGAIN? */
                if (errno == EINTR)
                        continue;
                err_errno("recv(): system call failed");
                efree(buf);
                return ErrorVar;
        }

        /*
         * XXX: Surely there are better ways of determining
         * whether or not a realloc is called for.
         */
        if (length - n > 1024)
                buf = erealloc(buf, n);
        return bytesvar_nocopy(buf, n);
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
 * send_wrapper - helper to do_send, block until all data is sent or
 * there was an error other than EINTR.
 */
static enum result_t
send_wrapper(int fd, const void *buf, size_t bufsize, int flags,
             const struct sockaddr *addr, size_t addrlen)
{
        ssize_t n;
        const void *end = buf + bufsize;
        while (buf < end) {
                errno = 0;
                if (addr) {
                        n = sendto(fd, buf, end - buf,
                                   flags, addr, addrlen);
                } else {
                        n = send(fd, buf, end - buf, flags);
                }
                if (n < 0) {
                        /* XXX: what about EAGAIN, EWOULDBLOCK? */
                        if (errno == EINTR)
                                continue;
                        err_errno("send(): send system call failed");
                        return RES_ERROR;
                }
                buf += n;
        }
        return RES_OK;
}

/*
 * sk.send(msg, **kwargs)
 *
 * @kwargs are { flags: 0, addr: null}
 *
 * @msg:        a bytes object containing the message.
 * @flags:      an integer bitfield of any of the following flags:
 *              MSG_OOB, MSG_DONTROUTE.  If caller does not provide them
 *              then they must be NULL.
 *
 * @addr:       If the address family is AF_INET, then @addr is a tuple
 *              of the form (IPADDR, PORT) where IPADDR is a string and
 *              PORT is an integer from 0 to 65535, eg:
 *                      ("192.168.1.0", 23)
 *              If the address family is AF_UNIX, then @addr is a string
 *              containing a socket file path, eg:
 *                      "/usr/tmp/my_socket_file"
 *              If this file is not used, then the connection address
 *              will be used instead.
 */
static Object *
do_send(Frame *fr)
{
        Object *msg, *addrarg, *skobj;
        int flags, fd, domain;
        struct evc_sockaddr_t addr;
        size_t addrlen;
        struct sockaddr *sa;
        const void *buf;
        size_t bufsize;

        msg = addrarg = NULL;
        flags = 0;

        skobj = vm_get_this(fr);
        if (socket_unpack_open_fd(skobj, &fd) == RES_ERROR)
                return ErrorVar;
        if (socket_unpack_domain(skobj, &domain) == RES_ERROR)
                return ErrorVar;
        if (vm_getargs(fr, "<bs>{|i<*>}:send", &msg,
                        STRCONST_ID(flags), &flags,
                        STRCONST_ID(addr), &addrarg) == RES_ERROR) {
                return ErrorVar;
        }
        /*
         * TODO: verify flags, add enumerations for them.
         */
        if (addrarg) {
                if (parse_address_arg(&addr, addrarg, domain, "send")
                    == RES_ERROR) {
                        return ErrorVar;
                }
                sa = &addr.sa;
                addrlen = dom2alen(domain);
        } else {
                sa = NULL;
                addrlen = 0;
        }
        if (isvar_string(msg)) {
                buf = string_cstring(msg);
                /* size must be byte-size, not # of Unicode chars */
                bufsize = strlen((char *)buf);
        } else {
                bug_on(!isvar_bytes(msg));
                buf = bytes_get_data(msg);
                bufsize = seqvar_size(msg);
        }

        if (send_wrapper(fd, buf, bufsize, flags, sa, addrlen)
            == RES_ERROR) {
                return ErrorVar;
        }
        return NULL;
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
        int fd, domain, type, protocol;
        Object *skobj;

        if (vm_getargs(fr, "iii", &domain, &type, &protocol) == RES_ERROR)
                return ErrorVar;
        if (validate_int(domain, VALID_DOMAINS, "domain") == RES_ERROR)
                return ErrorVar;
        if (validate_int(type, VALID_TYPES, "type") == RES_ERROR)
                return ErrorVar;
        if (protocol < 0) {
                err_setstr(TypeError, "protocol cannot be a negative number");
                return ErrorVar;
        }

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
        fd = socket(domain, type, protocol);
        if (fd < 0) {
                err_errno("Cannot create socket");
                return ErrorVar;
        }

        skobj = var_from_format("{OiOiOiOi}",
                                STRCONST_ID(fd),     fd,
                                STRCONST_ID(domain), domain,
                                STRCONST_ID(type),   type,
                                STRCONST_ID(proto),  protocol);

        static const struct type_inittbl_t sockmethods_inittbl[] = {
                V_INITTBL("accept",   do_accept,   0, 0, -1, -1),
                V_INITTBL("bind",     do_bind,     1, 1, -1, -1),
                V_INITTBL("connect",  do_connect,  1, 1, -1, -1),
                V_INITTBL("listen",   do_listen,   1, 1, -1, -1),
                V_INITTBL("recv",     do_recv,     2, 2, -1,  1),
                V_INITTBL("recvfrom", do_recvfrom, 1, 1, -1, -1),
                V_INITTBL("send",     do_send,     2, 2, -1,  1),
                /* TODO: [gs]etsockopt and common ioctl wrappers */
                TBLEND,
        };
        return dictvar_from_methods(skobj, sockmethods_inittbl);
}

static const struct type_inittbl_t socket_inittbl[] = {
        /* TODO: gethostbyname, socketpair, getaddrinfo */
        V_INITTBL("socket",  do_socket,  3, 3, -1, -1),
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
                /* TODO: support INET6 */
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

