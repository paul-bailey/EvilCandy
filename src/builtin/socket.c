/*
 * socket.c - Implementation of BSD-like sockets for EvilCandy
 *
 * ADDRESS EXPRESSIONS
 * -------------------
 *
 * The EvilCandy object types for network addresses (as arguments to
 * sendto and bind or results from accept and recvfrom) are different,
 * based on the socket's address family. In EvilCandy code, they are
 * expressed like this:
 *
 * - For AF_UNIX, address is a string, eg.
 *      '/path/to/socket/file'
 *
 * - For AF_INET, address is a (IP, PORT) tuple, where IP is a string
 *   and PORT is an integer from 0 to 65535, eg.
 *      ('www.google.com', 443)
 *      ('192.168.1.5', 23)
 *
 * "Address family" is called "domain" in the code below because one,
 * it's just one word; and two, the socket(2) man page for macOS also
 * calls address families "domains."
 *
 * API
 * ---
 *
 * This bears way more resemblance to the C API than JavaScript does.
 * The C callbacks for socket methods are the do_XXX() functions below,
 * see comments atop these functions for documentation on specific
 * methods.  The basic methods are:
 *
 * sk = socket()           Create a socket
 * sk.accept()             Accept a connection to a socket
 * sk.bind()               Bind a name to a socket
 * sk.connect()            Initiate a connection on a socket
 * sk.listen()             Listen for connections on a socket
 * sk.recv()               Receive data from a connected socket
 * sk.recvfrom()           Receive data from an unconnected socket
 * sk.send()               Send data over a connected socket
 * sk.sendto()             Send data over an unconnected socket
 * sk.close()              Close a socket
 */
#include <evilcandy/vm.h>
#include <evilcandy/global.h>
#include <evilcandy/err.h>
#include <evilcandy/errmsg.h>
#include <evilcandy/ewrappers.h>
#include <evilcandy/types/bytes.h>
#include <evilcandy/types/class.h>
#include <evilcandy/types/dict.h>
#include <evilcandy/types/string.h>
#include <evilcandy/types/number_types.h>
#include <internal/types/string.h>
#include <internal/types/sequential_types.h>
#include <internal/init.h>
#include <lib/helpers.h>

#include <errno.h>
/* TODO: Auto-conf all these things */
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

/*
 * XXX: This for one function, maybe have an abstraction-layer file.
 * cf. Python, fileutils.c
 */
#include <unistd.h> /* for close() */

static Object *socket_create_from(Object *instance, int fd, int domain,
                                  int type, int proto);

struct evc_sockaddr_t {
        union {
                struct sockaddr sa;
                /* Used for AF_INET */
                struct sockaddr_in in;
                /* Used for AF_UNIX */
                struct sockaddr_un un;
        };
};

/*
 * No Object head, because this isn't an actual object,
 * since a socket is just a dictionary.  It's a struct
 * stored as '_priv' in the socket dict.
 */
struct socketvar_t {
        int magic;
        int fd;
        int domain;
        int type;
        int proto;
        size_t addrlen;
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

static void
skerr(Object *exc, const char *msg, const char *fname)
{
        err_setstr(exc, "%s%s%s",
                   fname ? fname : "", fname ? "(): " : "", msg);
}

static void
skerr_gai(const char *msg, const char *fname, int gaires)
{
        err_setstr(SystemError, "%s%s%s (%s)",
                   fname ? fname : "", fname ? "(): " : "", msg,
                   gai_strerror(gaires));
}

static void
skerr_notimpl(const char *msg, const char *fname)
{
        skerr(NotImplementedError, msg, fname);
}

static void
skerr_syscall(const char *fname)
{
        err_errno("%s%ssystem call failed",
                  fname ? fname : "", fname ? "(): " : "");
}

static void
skerr_length(const char *fname)
{
        skerr(TypeError, "[maybe bug!] unexpected address length", fname);
}

/*
 * Helper to recvfrom and accept.
 * Convert a struct sockaddr into an object that can be used as an
 * argument to sendto.
 */
static Object *
addr2obj(struct socketvar_t *skv, struct evc_sockaddr_t *sa,
         size_t addrlen, const char *fname)
{
        if (addrlen != skv->addrlen) {
                skerr_length(fname);
                return ErrorVar;
        }

        switch (skv->domain) {
        case AF_INET:
                return var_from_format("(si)", inet_ntoa(sa->in.sin_addr),
                                       ntohs(sa->in.sin_port));
        case AF_UNIX:
            {
                size_t pathlen = sizeof(sa->un.sun_path);
                /*
                 * sa should have been memset to zero before calling
                 * recvfrom or accept.  However it's unclear whether
                 * a path whose length is exactly sizeof(...sun_path) is
                 * considered valid. If it is, then it will not have a
                 * nulchar terminator.
                 *
                 * Ignore .sun_len, that's not portable and I can't even
                 * find adequate documentation for it.
                 */
                if (sa->un.sun_path[pathlen-1] == '\0')
                        pathlen = strlen(sa->un.sun_path);
                return stringvar_newn(sa->un.sun_path, pathlen);
            }
        default:
                skerr_notimpl("unsupported address family", fname);
                return ErrorVar;
        }
}

/*
 * helper to obj2addr - fill @sa with IP address expressed by @name
 * domain is known to be AF_INET.
 */
static enum result_t
parse_ip_addr(const char *name, struct evc_sockaddr_t *sa,
              const char *fname)
{
        /* TODO: Manage 'broadcast' (=255.255.255.255). */
        /* TODO: Use @domain arg, currently only allowing IPv4 */
        memset(sa, 0, sizeof(*sa));
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
        sa->in.sin_len = sizeof(sa->in);
#endif
        sa->in.sin_family = AF_INET;
        if (name[0] == '\0') {
                sa->in.sin_addr.s_addr = INADDR_ANY;
                return RES_OK;
        }
        if (inet_pton(AF_INET, name, &sa->in.sin_addr) > 0) {
                return RES_OK;
        } else {
                /* Not of the n.n.n.n format? Perform name resolution */
                struct addrinfo hints, *info;
                int res;
                size_t addrlen = sizeof(sa->in);
                memset(&hints, 0, sizeof(hints));
                hints.ai_family = AF_INET;
                res = getaddrinfo(name, NULL, &hints, &info);
                if (res) {
                        skerr_gai("cannot get address", fname, res);
                        return RES_ERROR;
                }
                /*
                 * TODO: Not cycling through list, because we're only
                 * using AF_INET for hints.ai_family.  If we change this
                 * to use @domain arg, cycle through list, find info which
                 * has a matching domain.
                 */
                if (info->ai_addrlen != addrlen) {
                        skerr_length(fname);
                        freeaddrinfo(info);
                        return RES_ERROR;
                }
                memcpy(&sa->in, info->ai_addr, addrlen);
                freeaddrinfo(info);
                return RES_OK;
        }
}

static enum result_t
obj2addr_(struct evc_sockaddr_t *sa, Object *arg, int domain,
          const char *fmt, const char *fname)
{
        if (domain == AF_UNIX) {
                const char *name;
                size_t len;
                if (!isvar_string(arg)) {
                        skerr(TypeError, "expected: file name", fname);
                        return RES_ERROR;
                }
                name = string_cstring(arg);
                if ((len = string_nbytes(arg)) != strlen(name)) {
                        skerr(ValueError,
                              "path name contains embedded null char",
                              fname);
                        return RES_ERROR;
                }
                if (len > (sizeof(sa->un.sun_path) - 1)) {
                        skerr(ValueError, "path name too long", fname);
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
                if (vm_getargs_sv(arg, fmt, &name, &port)
                    == RES_ERROR) {
                        /* TODO: clearerr if string, try again */
                        return RES_ERROR;
                }
                if (parse_ip_addr(name, sa, fname) == RES_ERROR)
                        return RES_ERROR;
                /*
                 * memset to zero occurs in parse_ip_addr,
                 * so hold off setting port until now.
                 */
                sa->in.sin_port = htons(port);
                return RES_OK;
        }

        skerr_notimpl("domain not implemented", fname);
        return -1;
}

/*
 * Helper to send, bind, connect.
 * Convert an address argument into a struct sockaddr.
 * Opposite of addr2obj.
 *
 * @fname must be a literal, not a variable.
 */
#define obj2addr(sa, arg, dom, fname_) \
        obj2addr_(sa, arg, dom, "(sh):" fname_, fname_)

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

/*
 * If @check_open, fail if socket has been previously closed.
 */
static struct socketvar_t *
socket_get_priv(Object *skobj, const char *fname, bool check_open)
{
        struct socketvar_t *skv;

        skv = (struct socketvar_t *)instance_get_priv(skobj);
        if (check_open && skv->fd < 0)  {
                skerr(TypeError, "socket closed", fname);
                return NULL;
        }
        return skv;
}

static Object *
socket_str(Frame *fr)
{
        Object *skobj;
        struct socketvar_t *skv;
        char buf[256];

        skobj = vm_get_this(fr);
        bug_on(!skobj || !isvar_instance(skobj));

        skv = socket_get_priv(skobj, ".str", 0);

        if (!skv)
                return VAR_NEW_REF(NullVar);

        /* TODO: Something fancier than this */
        evc_sprintf(buf, sizeof(buf), "<socket @%p>", (void *)skobj);
        return stringvar_new(buf);
}

/* Don't ask what a "destroyer of socks" portends */
static void
sock_destructor(void *data)
{
        struct socketvar_t *skv = (struct socketvar_t *)data;
        int fd = skv->fd;
        skv->fd = -1;
        if (fd >= 0)
                close(fd);
        efree(skv);
}

static void
socket_initialize_private_data(Object *instance,
                int fd, int domain, int type, int protocol)
{
        struct socketvar_t *skv = emalloc(sizeof(*skv));
        skv->magic       = DICT_MAGIC_SOCK;
        skv->fd          = fd;
        skv->domain      = domain;
        skv->type        = type;
        skv->proto       = protocol;
        skv->addrlen     = dom2alen(domain);

        instance_set_priv(instance, sock_destructor, skv);
}

static Object *
socket_create_from(Object *instance, int fd, int domain, int type, int proto)
{
        Object *ret, *class;

        class = instance_get_class(instance);
        ret = instancevar_new(class, NULL, NULL, false);
        VAR_DECR_REF(class);
        socket_initialize_private_data(ret, fd, domain, type, proto);
        return ret;
}

/* (client, addr) = sk.accept() */
static Object *
do_accept(Frame *fr)
{
        struct socketvar_t *skv;
        Object *skobj, *sknew, *ret, *ao;
        struct evc_sockaddr_t sa;
        socklen_t addrlen;
        int newfd;

        skobj = vm_get_this(fr);
        skv = socket_get_priv(skobj, "accept", 1);
        if (!skv)
                return ErrorVar;

        memset(&sa, 0, sizeof(sa));
        if ((newfd = accept(skv->fd, &sa.sa, &addrlen)) < 0) {
                skerr_syscall("accept");
                return ErrorVar;
        }

        if (addrlen != skv->addrlen) {
                skerr_length("accept");
                goto err_closefd;
        }

        ao = addr2obj(skv, &sa, addrlen, "accept");
        if (!ao || ao == ErrorVar)
                goto err_closefd;

        sknew = socket_create_from(skobj, newfd, skv->domain,
                                   skv->type, skv->proto);
        bug_on(!sknew || sknew == ErrorVar);

        ret = var_from_format("(OO)", sknew, ao);
        VAR_DECR_REF(ao);
        VAR_DECR_REF(sknew);
        return ret;

err_closefd:
        close(newfd);
        return ErrorVar;
}

/*
 * sk.bind(address);
 *
 * address is a string.  It must make sense for the family used.
 * AF_INET:
 *      address may be 'localhost', 'INADDR_ANY', or an address of the
 *      form '#.#.#.#', ie. '192.168.0.1'.
 * AF_UNIX:
 *      address will look like a file name
 */
static Object *
do_bind(Frame *fr)
{
        struct socketvar_t *skv;
        Object *skobj, *addrarg;
        struct evc_sockaddr_t sa;

        skobj = vm_get_this(fr);
        if (vm_getargs(fr, "[<*>!]{!}:bind", &addrarg) == RES_ERROR)
                return ErrorVar;

        skv = socket_get_priv(skobj, "bind", 1);
        if (!skv)
                return ErrorVar;

        if (obj2addr(&sa, addrarg, skv->domain, "bind") == RES_ERROR)
                return ErrorVar;

        bug_on((ssize_t)skv->addrlen < 0);

        if (bind(skv->fd, &sa.sa, skv->addrlen) < 0) {
                skerr_syscall("bind");
                return ErrorVar;
        }
        return NULL;
}

/*
 * sk.connect(address);
 * address is a string.  It must make sens for the family used.
 * AF_INET:
 *      address may be of the form '#.#.#.#', ie. '192.168.0.1'.
 *      It may also be 'localhost' or 'INADDR_ANY'.
 * AF_UNIX:
 *      address will look like a file name
 */
static Object *
do_connect(Frame *fr)
{
        struct socketvar_t *skv;
        Object *skobj, *addrarg;
        struct evc_sockaddr_t sa;

        /*
         * XXX REVISIT: some implementations of connect (2) interpret
         * a NULL address argument as "disconnect".  Maybe do the same
         * here?
         */
        skobj = vm_get_this(fr);
        if (vm_getargs(fr, "[<*>!]{!}:connect", &addrarg) == RES_ERROR)
                return ErrorVar;

        skv = socket_get_priv(skobj, "connect", 1);
        if (!skv)
                return ErrorVar;

        if (obj2addr(&sa, addrarg, skv->domain, "connect") == RES_ERROR)
                return ErrorVar;

        bug_on((ssize_t)skv->addrlen < 0);

        if (connect(skv->fd, &sa.sa, skv->addrlen) < 0) {
                skerr_syscall("connect");
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
        struct socketvar_t *skv;
        Object *skobj;
        int backlog;

        skobj = vm_get_this(fr);
        if (vm_getargs(fr, "[i!]{!}", &backlog) == RES_ERROR)
                return ErrorVar;
        skv = socket_get_priv(skobj, "listen", 1);
        if (!skv)
                return ErrorVar;

        if (listen(skv->fd, backlog) < 0) {
                skerr_syscall("listen");
                return ErrorVar;
        }
        return NULL;
}

struct recv_data_t {
        struct evc_sockaddr_t sa;
        socklen_t addrlen;
        struct socketvar_t *skv;
};

static ssize_t
recvfrom_cb(struct socketvar_t *skv, void *buf, size_t len, int flags, void *data)
{
        struct recv_data_t *rdat = (struct recv_data_t *)data;
        rdat->skv = skv;
        /* Make sure this is filled in, otherwise we'll not get our address */
        rdat->addrlen = rdat->skv->addrlen;
        ssize_t ret = recvfrom(skv->fd, buf, len, flags,
                                &rdat->sa.sa, &rdat->addrlen);
        return ret;
}

static ssize_t
recv_cb(struct socketvar_t *skv, void *buf, size_t len, int flags, void *data)
{
        return recv(skv->fd, buf, len, flags);
}

/* common to recv and recvfrom */
static Object *
recv_common_(Frame *fr,
            ssize_t (*cb)(struct socketvar_t *, void *,
                          size_t, int, void *),
            void *data, const char *fmt, const char *fname)
{
        struct socketvar_t *skv;
        Object *skobj;
        int flags;
        long long length;
        ssize_t n;
        void *buf;

        flags = 0;
        length = 0;
        skobj = vm_get_this(fr);
        if (vm_getargs(fr, fmt, &length,
                       STRCONST_ID(flags), &flags) == RES_ERROR) {
                return ErrorVar;
        }

        skv = socket_get_priv(skobj, fname, 1);
        if (!skv)
                return ErrorVar;

        if (length < 0LL) {
                skerr(ValueError, "negative buffer size", fname);
                return ErrorVar;
        }
        buf = emalloc(length);
        do {
                errno = 0;
                n = cb(skv, buf, length, flags, data);
                if (n < 0 && errno != EINTR) {
                        skerr_syscall(fname);
                        efree(buf);
                        return ErrorVar;
                }
        } while (n < 0);
        if (n != length)
                buf = erealloc(buf, n ? n : 1);
        return bytesvar_nocopy(buf, n);
}

/* fname must be a literal, not a variable. */
#define recv_common(fr_, cb_, data_, fname_) \
        recv_common_(fr_, cb_, data_, "[l!]{|i}:" fname_, fname_)

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
        return recv_common(fr, recv_cb, NULL, "recv");
}

/*
 * (msg, addr) = sk.recvfrom(bufsize, [flags=0]);
 *
 * @flags is the same as with sk.recv.
 * @msg is a bytes object
 * @addr is the remote address, whose type depends on address family
 */
static Object *
do_recvfrom(Frame *fr)
{
        struct recv_data_t rdat;
        Object *msg, *ao, *ret;

        memset(&rdat, 0, sizeof(rdat));
        msg = recv_common(fr, recvfrom_cb, &rdat, "recvfrom");
        if (msg == ErrorVar)
                return ErrorVar;

        ao = addr2obj(rdat.skv, &rdat.sa, rdat.addrlen, "recvfrom");
        if (!ao || ao == ErrorVar) {
                VAR_DECR_REF(msg);
                return ErrorVar;
        }
        ret = var_from_format("(OO)", msg, ao);
        VAR_DECR_REF(msg);
        VAR_DECR_REF(ao);
        return ret;
}


/*
 * send_wrapper - helper to do_send, block until all data is sent or
 * there was an error other than EINTR.
 */
static enum result_t
send_wrapper(int fd, Object *msg, int flags,
             const struct sockaddr *addr, size_t addrlen)
{
        ssize_t n;
        const void *buf, *end;
        size_t bufsize;

        if (isvar_string(msg)) {
                buf = string_cstring(msg);
                bufsize = string_nbytes(msg);
        } else {
                bug_on(!isvar_bytes(msg));

                buf = bytes_get_data(msg);
                bufsize = seqvar_size(msg);
        }
        end = voidp_add(buf, bufsize);

        while (buf < end) {
                ssize_t sendlen = voidp_diff(end, buf);
                errno = 0;
                if (addr) {
                        n = sendto(fd, buf, sendlen,
                                   flags, addr, addrlen);
                } else {
                        n = send(fd, buf, sendlen, flags);
                }
                if (n < 0) {
                        /* XXX: what about EAGAIN, EWOULDBLOCK? */
                        if (errno == EINTR)
                                continue;
                        skerr_syscall(addr ? "sendto" : "send");
                        return RES_ERROR;
                }
                buf = voidp_add(buf, n);
        }
        return RES_OK;
}

/*
 * sk.send(msg, **kwargs), kwargs are { flags: 0 }
 *
 * @msg:        a string or bytes object
 * @flags:      an integer bitfield of any of the following flags:
 *              MSG_OOB, MSG_DONTROUTE.  If caller does not provide them
 *              then they must be NULL.
 */
static Object *
do_send(Frame *fr)
{
        struct socketvar_t *skv;
        Object *msg, *skobj;
        int flags;

        msg = NULL;
        flags = 0;

        skobj = vm_get_this(fr);
        skv = socket_get_priv(skobj, "send", 1);
        if (!skv)
                return ErrorVar;

        if (vm_getargs(fr, "[<bs>!]{|i}:send", &msg,
                        STRCONST_ID(flags), &flags) == RES_ERROR) {
                return ErrorVar;
        }

        if (send_wrapper(skv->fd, msg, flags, NULL, 0) == RES_ERROR)
                return ErrorVar;
        return NULL;
}

/*
 * sk.sendto(msg, addr, **wkargs), kwargs are { flags: 0 }
 *
 * @msg:        a string or bytes object
 * @addr:       Address to send to, see top of this file re: addresses
 * @flags:      an integer bitfield of any of the following flags:
 *              MSG_OOB, MSG_DONTROUTE.  If caller does not provide them
 *              then they must be NULL.
 */
static Object *
do_sendto(Frame *fr)
{
        struct socketvar_t *skv;
        struct evc_sockaddr_t addr;
        Object *skobj, *msg, *ao;
        int flags;

        skobj = vm_get_this(fr);
        skv = socket_get_priv(skobj, "sendto", 1);
        if (!skv)
                return ErrorVar;

        flags = 0;
        msg = ao = NULL;
        if (vm_getargs(fr, "[<bs><*>!]{|i}:sendto", &msg, &ao,
                        STRCONST_ID(flags), &flags) == RES_ERROR) {
                return ErrorVar;
        }
        bug_on(!msg || !ao); /* vm_getargs shoulda thrown error */
        if (obj2addr(&addr, ao, skv->domain, "sendto") == RES_ERROR)
                return ErrorVar;

        if (send_wrapper(skv->fd, msg, flags,
                         &addr.sa, skv->addrlen) == RES_ERROR) {
                return ErrorVar;
        }
        return NULL;
}

static Object *
do_close(Frame *fr)
{
        struct socketvar_t *skv;
        Object *skobj, *ret;

        skobj = vm_get_this(fr);
        skv = socket_get_priv(skobj, "close", 0);
        if (!skv)
                return ErrorVar;

        /* Be like Python: close may be called more than once. */
        if (skv->fd < 0)
                return NULL;

        ret = NULL;
        if (close(skv->fd)) {
                skerr_syscall("close");
                ret = ErrorVar;
                /* Set to <0 anyway, so fall through */
        }
        skv->fd = -1;
        return ret;
}

static Object *
socket_init(Frame *fr)
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
        Object *instance;

        fd = -1;
        instance = vm_get_this(fr);
        if (vm_getargs(fr, "[iii!]{!}", &domain, &type, &protocol) == RES_ERROR)
                return ErrorVar;
        if (validate_int(domain, VALID_DOMAINS, "domain") == RES_ERROR)
                return ErrorVar;
        if (validate_int(type, VALID_TYPES, "type") == RES_ERROR)
                return ErrorVar;
        if (protocol < 0) {
                skerr(TypeError, "protocol may not be a negative number",
                      "socket");
                return ErrorVar;
        }

        fd = socket(domain, type, protocol);
        if (fd < 0) {
                skerr_syscall("socket");
                return ErrorVar;
        }

        socket_initialize_private_data(instance, fd,
                                       domain, type, protocol);
        return NULL;
}

static Object *
create_socket_class(void)
{
        static const struct type_method_t sockmethods_inittbl[] = {
                {"accept",   do_accept},
                {"bind",     do_bind},
                {"connect",  do_connect},
                {"listen",   do_listen},
                {"recv",     do_recv},
                {"recvfrom", do_recvfrom},
                {"send",     do_send},
                {"sendto",   do_sendto},
                {"close",    do_close},
                {"__str__",  socket_str},
                {"__init__", socket_init},
                /* TODO: [gs]etsockopt and common ioctl wrappers */
                {NULL, NULL},
        };
        Object *methods = dictvar_from_methods(NULL, sockmethods_inittbl);
        Object *ret = classvar_new(NULL, methods, NULL, NULL);
        VAR_DECR_REF(methods);
        return ret;
}

static Object *
create_socket_instance(Frame *fr)
{
        Object *socket_class, *skobj, *enums;

        skobj = dictvar_new();
        enums = gbl_borrow_mns_dict(MNS_SOCKET);
        if (!enums) {
                /*
                 * First instance, we need to initialize dict.  Putting
                 * one of these in gbl means that we have more than one
                 * dict than there are instances of this library, but it
                 * forces keys to be globally unique.  So the memory
                 * footprint is bigger when we don't care (just a couple
                 * instantiations), but much smaller when we do care
                 * (many many instantiations).
                 *
                 * We don't do this in moduleinit_socket(), because we
                 * won't know yet if this library will even be used.
                 */
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
                        DTB(MSG_OOB),
                        DTB(MSG_PEEK),
                        DTB(MSG_WAITALL),
                        DTB(MSG_DONTROUTE),
#undef DTB
                        { -1, NULL },
                };

                const struct dtbl_t *t;
                enums = dictvar_new();

                for (t = dtbl; t->name != NULL; t++) {
                        Object *v = intvar_new(t->e);
                        Object *k = stringvar_new(t->name);
                        dict_setitem(enums, k, v);
                        VAR_DECR_REF(v);
                        VAR_DECR_REF(k);
                }
                gbl_set_mns_dict(MNS_SOCKET, enums);
        }
        dict_copyto(skobj, enums);
        socket_class = create_socket_class();
        dict_setitem(skobj, STRCONST_ID(socket), socket_class);
        VAR_DECR_REF(socket_class);
        return skobj;
}

void
moduleinit_socket(void)
{
        Object *k = stringvar_from_ascii("_socket");
        Object *o = var_from_format("<xmM>",
                                    create_socket_instance, 0, 0);
        dict_setitem(GlobalObject, k, o);
        VAR_DECR_REF(k);
        VAR_DECR_REF(o);
}
