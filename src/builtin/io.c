/* builtin/io.c - Implementation of the __gbl__.Io built-in object */
#include <evilcandy.h>
#include <fcntl.h> /* open() */
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h> /* fstat() */

/*
 * XXX: See isvar_file().
 * We need a single header for all our magic numbers, to keep them unique.
 */
enum { FILE_MAGIC = 'F' << 24 | 'I' << 16 | 'L' << 8 | 'E' };

enum file_type_t {
        FILE_TEXT,
        FILE_BINARY,
        FILE_RAW,
};

/**
 * struct rawfile_t - Raw unbuffered file
 * @fr_fd:       File descriptor, or -1 if an in-memory file only
 * @fr_writable: True if file is writable
 * @fr_readable: True if file is readable
 * @fr_eof:      True if file is at end-of-file
 * @fr_closefd:  True to close file descriptor during garbage collection
 * @fr_pos:      Position in the file
 */
struct rawfile_t {
        int fr_magic;
        enum file_type_t fr_type;
        int fr_fd;
        char *fr_mode;
        char *fr_name;
        bool fr_writable;
        bool fr_readable;
        bool fr_eof;
        bool fr_closefd;
        off_t fr_pos;
};

struct textfile_t {
        struct rawfile_t ft_raw;
#define ft_magic        ft_raw.fr_magic
#define ft_type         ft_raw.fr_type
#define ft_fd           ft_raw.fr_fd
#define ft_mode         ft_raw.fr_mode
#define ft_name         ft_raw.fr_name
#define ft_writable     ft_raw.fr_writable
#define ft_readable     ft_raw.fr_readable
#define ft_eof          ft_raw.fr_eof
#define ft_closefd      ft_raw.fr_closefd
#define ft_fdpos        ft_raw.fr_pos
        int ft_codec;
        Object *ft_eol;
        Object *ft_buf;
        size_t ft_bufpos;
        unsigned char ft_stragglers[8];
        unsigned char ft_nstraggler;
        off_t ft_upos;
};

struct binfile_t {
        struct rawfile_t fb_raw;
#define fb_magic        fb_raw.fr_magic
#define fb_type         fb_raw.fr_type
#define fb_fd           fb_raw.fr_fd
#define fb_mode         fb_raw.fr_mode
#define fb_name         fb_raw.fr_name
#define fb_writable     fb_raw.fr_writable
#define fb_readable     fb_raw.fr_readable
#define fb_eof          fb_raw.fr_eof
#define fb_closefd      fb_raw.fr_closefd
#define fb_fdpos        fd_raw.fr_pos
        Object *fb_buf;
        size_t fb_bufpos;
        off_t fb_upos;
};

struct fileconfig_t {
        bool readable;
        bool writable;
        bool closefd;
        enum file_type_t type;
        char *name;
        char *mode;
};

static void *
file_new(int fd, size_t size, struct fileconfig_t *cfg)
{
        struct rawfile_t *raw = (struct rawfile_t *)emalloc(size);

        bug_on(size < sizeof(struct rawfile_t));
        /* XXX: permit this? It doesn't make sense */

        memset(raw, 0, size);
        raw->fr_magic    = FILE_MAGIC;
        raw->fr_type     = cfg->type;
        raw->fr_fd       = fd;
        raw->fr_mode     = cfg->mode;
        raw->fr_name     = cfg->name;
        raw->fr_closefd  = cfg->closefd;
        raw->fr_writable = cfg->writable;
        raw->fr_readable = cfg->readable;
        return (void *)raw;
}

static void
filerr(const char *fname, const char *msg)
{
        err_setstr(TypeError, "%s%s%s",
                   fname ? fname : "", fname ? "() " : "", msg);
}

static void
filerr_malformed(const char *fname)
{
        filerr(fname, "file's dictionary corrupted");
}

static struct rawfile_t *
file_get_priv(Object *fo, const char *fname,
              bool check_open, size_t checksize, enum file_type_t type)
{
        struct rawfile_t *ret;

        ret = (struct rawfile_t *)dict_get_priv(fo);
        if (!ret || ret->fr_magic != FILE_MAGIC || ret->fr_type != type) {
                filerr_malformed(fname);
                return NULL;
        }

        if (check_open && ret->fr_fd < 0)  {
                filerr(fname, "file closed");
                return NULL;
        }
        return ret;
}

static inline struct rawfile_t *
rawfile_get_priv(Object *fo, const char *fname, bool check_open)
{
        return file_get_priv(fo, fname, check_open,
                             sizeof(struct rawfile_t), FILE_RAW);
}
static struct rawfile_t *
rawfile_fget_priv(Frame *fr, const char *fname, bool check_open)
{
        Object *fo = vm_get_this(fr);
        bug_on(!isvar_dict(fo));
        return rawfile_get_priv(fo, fname, check_open);
}

static Object *
do_getfd(Frame *fr)
{
        struct rawfile_t *raw = rawfile_fget_priv(fr, "getfd", 1);
        if (!raw)
                return ErrorVar;
        return intvar_new(raw->fr_fd);
}

static Object *
do_iseof(Frame *fr)
{
        struct rawfile_t *raw = rawfile_fget_priv(fr, "iseof", 1);
        if (!raw)
                return ErrorVar;
        return raw->fr_eof
                ? VAR_NEW_REF(gbl.one)
                : VAR_NEW_REF(gbl.zero);
}

/*
 *              Text-specific callbacks
 */

static inline struct textfile_t *
textfile_get_priv(Object *fo, const char *fname, bool check_open)
{
        return (struct textfile_t *)file_get_priv(fo, fname, check_open,
                                           sizeof(struct textfile_t),
                                           FILE_TEXT);
}

static struct textfile_t *
textfile_fget_priv(Frame *fr, const char *fname, bool check_open)
{
        Object *fileobj = vm_get_this(fr);
        bug_on(!isvar_dict(fileobj));
        return textfile_get_priv(fileobj, fname, check_open);
}

/* helper to text_append_chunk */
static ssize_t
text_read_chunk(struct textfile_t *ft, void *buf, size_t bufsize)
{
        void *start = buf;
        void *end = buf + bufsize;
        while (buf < end) {
                errno = 0;
                ssize_t res = read(ft->ft_fd, buf, end - buf);
                if (res <= 0) {
                        if (errno == EINTR)
                                continue;
                        if (res == 0)
                                break;
                        /*
                         * FIXME: if buf != start,
                         * file is in unknown state!
                         */
                        return res;
                }
                ft->ft_fdpos += res;
                buf += res;
        }
        return buf - start;
}

static enum result_t
text_append_chunk(struct textfile_t *ft)
{
        enum { CHUNK_SIZE = 256 };
        unsigned char *chunk = emalloc(CHUNK_SIZE);
        unsigned char *readp;
        ssize_t nread, chunksize, nstraggler;

        readp = chunk;
        chunksize = CHUNK_SIZE;
        nstraggler = ft->ft_nstraggler;
        ft->ft_nstraggler = 0;
        if (nstraggler) {
                memcpy(chunk, ft->ft_stragglers, nstraggler);
                readp += nstraggler;
                chunksize -= nstraggler;
        }
        nread = text_read_chunk(ft, readp, chunksize);
        if (nread < 0) {
                /* TODO: need more error handling than just this */
                efree(chunk);
                err_errno("readline() system call error");
                return RES_ERROR;
        } else if (!nread) {
                ft->ft_eof = true;
                nread = nstraggler;
        }
        nread += nstraggler;

        if (nread) {
                /*
                 * XXX REVISIT: I'd prefer just a string_writer,
                 * so that we don't have to keep creating strings
                 * and all that.
                 */
                struct string_writer_t wr;
                size_t nappend;

                string_writer_init(&wr, 1);
                if (ft->ft_buf) {
                        string_writer_append_strobj(&wr, ft->ft_buf);
                        VAR_DECR_REF(ft->ft_buf);
                        ft->ft_buf = NULL;
                }

                nappend = string_writer_decode(&wr, chunk,
                                               nread, ft->ft_codec, true);
                if (nappend < 0) {
                        string_writer_destroy(&wr);
                        return RES_ERROR;
                } else if (nappend != nread) {
                        /* copy new stagglers */
                        ft->ft_nstraggler = nread - nappend;
                        bug_on(nstraggler > sizeof(ft->ft_stragglers));
                        memcpy(ft->ft_stragglers, &chunk[nread],
                               ft->ft_nstraggler);
                        ft->ft_nstraggler = nstraggler;
                }
                ft->ft_buf = stringvar_from_writer(&wr);
        }
        efree(chunk);
        return RES_OK;
}

static char *
readinto_chunkly(int fd, size_t *size)
{
        enum { GROWSIZ = 256 };
        char *buf = erealloc(NULL, GROWSIZ);
        size_t allocsize = GROWSIZ;
        char *p = buf;
        char *end = buf + allocsize;
        for (;;) {
                /* XXX: wrap text_read_chunk above instead of this */
                size_t pi;
                while (p < end) {
                        ssize_t n = read(fd, p, end - p);
                        if (n < 0) {
                                efree(buf);
                                err_errno("read system call failed");
                                return NULL;
                        } else if (n == 0) {
                                break;
                        }
                        p += n;
                }
                if (p != end)
                        break;
                pi = p - buf;
                allocsize += GROWSIZ;
                buf = erealloc(buf, allocsize);
                p = buf + pi;
                end = p + GROWSIZ;
        }
        *size = p - buf;
        return buf;
}

static char *
readinto(int fd, off_t pos, size_t *size)
{
        struct stat st;
        size_t allocbytes;
        char *buf, *p, *end;

        if (fstat(fd, &st) < 0)
                goto slowpath;
        if (st.st_size < pos) /* ?!?! */
                goto slowpath;
        allocbytes = (size_t)(st.st_size - pos);
        buf = emalloc(allocbytes);
        end = buf + allocbytes;
        p = buf;

        while (p < end) {
                ssize_t n = read(fd, p, end - p);
                if (n < 0) {
                        efree(buf);
                        err_errno("read system call failed");
                        return NULL;
                } else if (n == 0) {
                        /* ?!?! */
                        break;
                }
                p += n;
        }
        *size = p - buf;
        return buf;

slowpath:
        return readinto_chunkly(fd, size);
}

static Object *
do_text_read(Frame *fr)
{
        Object *ret;
        size_t nread;
        char *tbuf;
        struct textfile_t *ft = textfile_fget_priv(fr, "readline", 1);
        if (!ft)
                return ErrorVar;
        if (!ft->ft_readable) {
                err_setstr(TypeError, "file is not readable");
                return ErrorVar;
        }

        if (ft->ft_eof) {
        eof:
                if (ft->ft_buf) {
                        ret = ft->ft_buf;
                        ft->ft_buf = NULL;
                        /*
                         * Don't need to produce ref, we're handing
                         * it off instead.
                         */
                } else {
                        ret = VAR_NEW_REF(STRCONST_ID(mpty));
                }
                return ret;
        }
        /*
         * XXX REVISIT: Better to use fstat at open time to get the file
         * size, then maintain a position marker during open cycle.
         */
        tbuf = readinto(ft->ft_fd, ft->ft_fdpos, &nread);
        if (!tbuf) {
                return ErrorVar;
        } else if (!nread) {
                ft->ft_eof = true;
                efree(tbuf);
                goto eof;
        }
        if (ft->ft_buf) {
                Object *new;

                /* XXX: Surely there's a better way! */
                new = stringvar_from_binary(tbuf, nread, ft->ft_codec);
                ret = string_cat(ft->ft_buf, new);
                VAR_DECR_REF(new);
                VAR_DECR_REF(ft->ft_buf);
                ft->ft_buf = NULL;
                return ret;
        } else {
                return stringvar_from_binary(tbuf, nread, ft->ft_codec);
        }
}

static Object *
do_text_readline(Frame *fr)
{
        Object *ret = NULL;
        ssize_t haveidx = 0;
        size_t seekpos;
        struct textfile_t *ft = textfile_fget_priv(fr, "readline", 1);
        if (!ft)
                return ErrorVar;

        if (!ft->ft_readable) {
                err_setstr(TypeError, "file is not readable");
                return ErrorVar;
        }

        seekpos = ft->ft_bufpos;
        while (1) {
                if (ft->ft_buf) {
                        /*
                         * XXX: change string_search to also take a startpos,
                         * and increment that in this loop, so that we don't
                         * end up repeating search of earlier characters.
                         */
                        haveidx = string_search(ft->ft_buf, ft->ft_eol, seekpos);
                        if (haveidx >= 0) {
                                /* EOL exists in our buffer */
                                haveidx += seqvar_size(ft->ft_eol);
                                bug_on(haveidx > seqvar_size(ft->ft_buf));
                                break;
                        }
                        /* Don't re-seek old misses */
                        seekpos = seqvar_size(ft->ft_buf) - seqvar_size(ft->ft_eol);
                }

                if (ft->ft_eof) {
                        if (ft->ft_buf) {
                                haveidx = seqvar_size(ft->ft_buf);
                                break;
                        } else {
                                return VAR_NEW_REF(STRCONST_ID(mpty));
                        }
                } else {
                        text_append_chunk(ft);
                }
        }

        /* Return the buffer */
        bug_on(!ft->ft_buf);
        bug_on(haveidx <= ft->ft_bufpos);
        if (ft->ft_bufpos == 0 && haveidx == seqvar_size(ft->ft_buf)) {
                /*
                 * Special rare case: we have exactly a line.  No need to
                 * produce a reference, because we would just consume it
                 * again right here.
                 */
                ret = ft->ft_buf;
                ft->ft_buf = NULL;
                ft->ft_bufpos = 0;
                return ret;
        }

        bug_on(haveidx > seqvar_size(ft->ft_buf));
        ret = string_getslice(ft->ft_buf, ft->ft_bufpos,
                              haveidx, 1);
        ft->ft_bufpos = haveidx;
        if (ft->ft_bufpos == seqvar_size(ft->ft_buf)) {
                Object *tmp = ft->ft_buf;
                ft->ft_buf = NULL;
                ft->ft_bufpos = 0;
                VAR_DECR_REF(tmp);
        }
        return ret;
}

static ssize_t
do_text_write_(struct textfile_t *ft, Object *so)
{
        ssize_t ret;
        if (!ft->ft_writable) {
                err_setstr(TypeError, "file is not writable");
                return -1;
        }

        /*
         * TODO: Should use ft->ft_buf... flush that one and
         * write until eol, or deliberately add eol here, if
         * the file is so configured.
         */
        if (string_isascii(so) || ft->ft_codec == CODEC_UTF8) {
                /* Easy thing: Just write the C string as-is */
                size_t len = string_nbytes(so);
                ret = write(ft->ft_fd, string_cstring(so), len);
        } else if (ft->ft_codec == CODEC_ASCII) {
                err_setstr(ValueError,
                        "Cannot write non-ascii string to ascii file");
                return -1;
        } else if (string_width(so) > 1) {
                /* Latin1, but too wide of characters exist */
                err_setstr(ValueError,
                        "Unicode points too big for Latin1");
                return -1;
        } else {
                /* Latin1, good to go */
                void *data = string_data(so);
                ret = write(ft->ft_fd, data, seqvar_size(so));
        }
        return ret;
}

static Object *
do_text_write(Frame *fr)
{
        ssize_t ret;
        Object *so;
        struct textfile_t *ft = textfile_fget_priv(fr, "write", 1);
        if (!ft)
                return ErrorVar;
        if (vm_getargs(fr, "<s>:write", &so) == RES_ERROR)
                return ErrorVar;
        ret = do_text_write_(ft, so);
        if (ret < 0)
                return ErrorVar;
        return intvar_new(ret);
}

static Object *
do_text_close(Frame *fr)
{
        int fd;
        struct textfile_t *ft = textfile_fget_priv(fr, "close", 0);
        if (!ft)
                return ErrorVar;

        fd = ft->ft_fd;
        ft->ft_fd = -1;
        if (fd >= 0)
                close(fd);
        return NULL;
}

static Object *
text_str(Frame *fr)
{
        Object *fo;
        const char *codecstr;
        struct textfile_t *ft;
        struct buffer_t b;
        bool err;

        err = err_occurred();
        fo = vm_get_arg(fr, 0);
        if (!fo || !isvar_dict(fo))
                return NullVar;
        ft = textfile_get_priv(fo, NULL, 0);
        if (!ft) {
                if (!err)
                        err_clear();
                return NullVar;
        }

        switch (ft->ft_codec) {
        case CODEC_ASCII:
                codecstr = "ascii";
                break;
        case CODEC_LATIN1:
                codecstr = "latin1";
                break;
        case CODEC_UTF8:
                codecstr = "utf-8";
                break;
        default:
                codecstr = "?";
        }

        buffer_init(&b);
        buffer_printf(&b, "<file name='%s' mode='%s' enc='%s'>",
                      ft->ft_name ? ft->ft_name : "!",
                      ft->ft_mode, codecstr);
        return stringvar_from_buffer(&b);
}

static void
text_destructor(Object *fo)
{
        bool err;
        struct textfile_t *ft;
        int fd;
        char *s;
        Object *o;

        err = err_occurred();
        ft = textfile_get_priv(fo, NULL, 0);
        if (!ft) {
                if (!err)
                        err_clear();
                /* Can't do anything */
                return;
        }

        fd = ft->ft_fd;
        ft->ft_fd = -1;
        if (ft->ft_closefd && fd >= 0)
                close(fd);

        s = ft->ft_name;
        ft->ft_name = NULL;
        if (s)
                efree(s);

        s = ft->ft_mode;
        ft->ft_mode = NULL;
        if (s)
                efree(s);

        o = ft->ft_buf;
        ft->ft_buf = NULL;
        if (o)
                VAR_DECR_REF(o);

        o = ft->ft_eol;
        ft->ft_eol = NULL;
        if (o)
                VAR_DECR_REF(o);
}

/**
 * open_text - Create a file object in text mode from an open file
 * @name: Name of file, for information purposes.
 * @mode: Mode of file, for information purposes.
 * @oflags: Flags used to open the file
 * @fd:   File descriptor of the open file.
 * @closefd: True to close the file when the object is destroyed
 * @codec:   A CODEC_xxx enumeration.
 *
 * Return: A file object (technically a dictionary), which can be
 *      used for print() and other operations.
 */
static Object *
open_text(int fd, struct fileconfig_t *cfg, int codec)
{
        static const struct type_inittbl_t textfile_cb_methods[] = {
                V_INITTBL("read",       do_text_read,     0, 0, -1, -1),
                V_INITTBL("readline",   do_text_readline, 0, 0, -1, -1),
                V_INITTBL("write",      do_text_write,    1, 1, -1, -1),
                V_INITTBL("close",      do_text_close,    0, 0, -1, -1),
                V_INITTBL("getfd",      do_getfd,         0, 0, -1, -1),
                V_INITTBL("iseof",      do_iseof,         0, 0, -1, -1),
                TBLEND,
        };
        struct textfile_t *fh;
        Object *ret, *strfunc;

        fh = file_new(fd, sizeof(*fh), cfg);
        fh->ft_codec = codec;
        /* FIXME: this should be an argument */
        fh->ft_eol = stringvar_new("\n");

        strfunc = funcvar_new_intl(text_str, 1, 1);

        ret = dictvar_from_methods(NULL, textfile_cb_methods);
        dict_set_priv(ret, fh);
        dict_add_cdestructor(ret, text_destructor);
        dict_setstr(ret, strfunc);

        VAR_DECR_REF(strfunc);

        return ret;
}

static Object *
open_raw(int fd, struct fileconfig_t *cfg)
{
        err_setstr(NotImplementedError, "raw files not yet implemented");
        return ErrorVar;
}

static Object *
open_binary(int fd, struct fileconfig_t *cfg)
{
        err_setstr(NotImplementedError, "binary files not yet implemented");
        return ErrorVar;
}

/*
 * C-level version of do_open - kept apart in case we want to call this
 * within the source tree.  Currently, we're just using stdio.h's FILE
 * for the interpreter and stderr.
 */
static Object *
evc_open(struct fileconfig_t *cfg, int oflags, int codec)
{
        Object *ret;
        int fd = open(cfg->name, oflags, 0666);
        if (fd < 0) {
                err_errno("cannot open %s", cfg->name);
                return ErrorVar;
        }

        switch (cfg->type) {
        case FILE_TEXT:
                ret = open_text(fd, cfg, codec);
                break;
        case FILE_BINARY:
                ret = open_binary(fd, cfg);
                break;
        case FILE_RAW:
                ret = open_raw(fd, cfg);
                break;
        default:
                bug();
                /* to keep the compiler happy */
                return ErrorVar;
        }

        return ret;
}

ssize_t
evc_file_write(Object *fo, Object *data)
{
        struct textfile_t *ft;
        if (!isvar_dict(fo)) {
                err_setstr(TypeError, "Cannot write: not a file");
                return -1;
        }
        ft = textfile_get_priv(fo, NULL, 1);
        if (!ft)
                return -1;

        /* FIXME: proper multiplexing of 'write' callbacks */
        if (ft->ft_type != FILE_TEXT) {
                err_setstr(TypeError,
                        "Write at this level only permitted on text-base files");
                return -1;
        }
        return do_text_write_(ft, data);
}

bool
isvar_file(Object *o)
{
        if (isvar_dict(o)) {
                struct rawfile_t *fr = dict_get_priv(o);
                if (fr)
                        return fr->fr_magic == FILE_MAGIC;
        }
        return false;
}

/**
 * evc_file_open - Open fd as an EvilCandy file object
 * @fd:         Open file descriptor
 * @name:       Name of file or NULL.  If not NULL, a copy will be stored.
 * @binary:     If true, open the file in binary mode
 * @closefd:    If true, close the file when this object is freed
 * @codec:      Encryption of the file, or -1 to disregard encryption.
 * @buffering:  If 0, no buffering (invalid for non-binary files).
 *              If 1, line buffering.  If >1, use a buffer of @buffering
 *              size.
 *
 * Return: File object.  Used as a C hook for program internals to write
 * to file objects.
 */
Object *
evc_file_open(int fd, const char *name, bool binary,
              bool closefd, int codec, size_t buffering)
{
        Object *ret;
        char mode[8];
        char *modep;
        int oflags;
        int rwchar;
        struct fileconfig_t cfg;

        oflags = fcntl(fd, F_GETFL);
        if (oflags < 0) {
                err_errno("Cannot get open flags");
                return ErrorVar;
        }

        /*
         * XXX: is this portable? O_RDWR is not a bitfield of O_RDONLY
         * with O_WRONLY, however standards do not guarantee that behavior
         */
        switch (oflags & (O_RDWR | O_RDONLY | O_WRONLY)) {
        case O_RDWR:
                cfg.readable = true;
                cfg.writable = true;
                break;
        case O_RDONLY:
                cfg.readable = true;
                cfg.writable = false;
                break;
        case O_WRONLY:
                cfg.readable = false;
                cfg.writable = true;
                break;
        default:
                err_errno("Malformed open flags");
                return ErrorVar;
        }

        bug_on(!cfg.readable && !cfg.writable);
        if (cfg.writable) {
                if (!!(oflags & O_APPEND))
                        rwchar = 'a';
                else if (!!(oflags & O_EXCL))
                        rwchar = 'x';
                else
                        rwchar = 'w';
        } else {
                rwchar = 'r';
        }
        modep = mode;
        *modep++ = rwchar;
        if (binary)
                *modep++ = 'b';
        if (cfg.readable && cfg.writable)
                *modep++ = '+';
        *modep = '\0';

        cfg.closefd = closefd;
        cfg.type = binary ? (buffering ? FILE_BINARY : FILE_RAW)
                          : FILE_TEXT;
        cfg.name = name ? estrdup(name) : NULL;
        cfg.mode = estrdup(mode);

        switch (cfg.type) {
        case FILE_TEXT:
                ret = open_text(fd, &cfg, codec);
                break;
        case FILE_BINARY:
                ret = open_binary(fd, &cfg);
                break;
        case FILE_RAW:
                ret = open_raw(fd, &cfg);
                break;
        default:
                bug();
                /* to keep the compiler happy */
                return ErrorVar;
        }

        return ret;
}

static Object *
do_open(Frame *fr)
{
        const char *name, *mode, *s;
        Object *encarg = NULL;
        struct fileconfig_t cfg;
        enum result_t res;
        bool binary = false;
        bool have_rw = false;
        bool have_plus = false;
        int buffering = true;
        int closefd = true;
        int codec = CODEC_OPEN_DEFAULT;
        int flags = 0;
        Object *ret;

        res = vm_getargs(fr, "ss{|<s>ii}:open", &name, &mode,
                         STRCONST_ID(encoding), &encarg,
                         STRCONST_ID(closefd), &closefd,
                         STRCONST_ID(buffering), &buffering);
        if (res == RES_ERROR)
                return ErrorVar;

        if (encarg) {
                bug_on(!gbl.mns[MNS_CODEC]);
                res = vm_getargs_sv(gbl.mns[MNS_CODEC],
                                    "{i}", encarg, &codec);
                if (res == RES_ERROR)
                        return ErrorVar;
        }

        cfg.readable = false;
        cfg.writable = false;
        cfg.closefd = closefd;
        cfg.type = FILE_TEXT;
        cfg.name = NULL;
        cfg.mode = NULL;

        s = mode;
        while (*s) {
                int c = *s++;
                switch (c) {
                case 'a':
                        if (have_rw) {
                        moderr:
                                err_setstr(ValueError,
                                        "mode must have only one of 'rwax' and at most one '+'");
                                return ErrorVar;
                        }
                        have_rw = true;
                        cfg.writable = true;
                        flags |= O_CREAT | O_APPEND;
                        break;
                case 'b':
                        binary = true;
                        break;
                case 'r':
                        if (have_rw)
                                goto moderr;
                        have_rw = true;
                        cfg.readable = true;
                        break;
                case 'w':
                        if (have_rw)
                                goto moderr;
                        have_rw = true;
                        cfg.writable = true;
                        flags |= O_CREAT | O_TRUNC;
                        break;
                case 'x':
                        if (have_rw)
                                goto moderr;
                        have_rw = true;
                        cfg.writable = true;
                        flags |= O_EXCL | O_CREAT;
                        break;
                case '+':
                        if (!have_rw || have_plus)
                                goto moderr;
                        have_plus = true;
                        cfg.writable = true;
                        cfg.readable = true;
                        break;
                default:
                    {
                        err_setstr(ValueError,
                                   "invalid character '%c' in mode '%s'",
                                   c, mode);
                        return ErrorVar;
                    }
                }
        }

        if (cfg.readable) {
                flags |= (cfg.writable ? O_RDWR : O_RDONLY);
        } else if (cfg.writable) {
                flags |= O_WRONLY;
        } else {
                err_setstr(ValueError,
                           "mode '%s' missing one of 'rwax'", mode);
                return ErrorVar;
        }

        if (binary && encarg) {
                err_setstr(ValueError, "cannot use encoding in binary mode");
                return ErrorVar;
        }

        /* TODO: parse codec */
        if (binary) {
                cfg.type = buffering ? FILE_BINARY : FILE_RAW;
        } else if (!buffering) {
                err_setstr(ValueError, "Cannot open in text mode without buffering");
                return ErrorVar;
        } else {
                cfg.type = FILE_TEXT;
        }
        cfg.name = estrdup(name);
        cfg.mode = estrdup(mode);
        ret = evc_open(&cfg, flags, codec);
        if (ret == ErrorVar) {
                efree(cfg.name);
                efree(cfg.mode);
        }
        return ret;
}

static const struct type_inittbl_t io_inittbl[] = {
        V_INITTBL("open", do_open, 3, 3, -1,  2),
        TBLEND,
};

static Object *
create_io_instance(Frame *fr)
{
        return dictvar_from_methods(NULL, io_inittbl);
}

void
moduleinit_io(void)
{
        Object *k = stringvar_new("_io");
        Object *o = var_from_format("<xmM>",
                                    create_io_instance, 0, 0);
        dict_setitem(GlobalObject, k, o);
        VAR_DECR_REF(k);
        VAR_DECR_REF(o);

        o = var_from_format("<xmMk>", do_open, 3, 3, 2);
        k = stringvar_new("open");
        vm_add_global(k, o);
        VAR_DECR_REF(k);
        VAR_DECR_REF(o);
}


