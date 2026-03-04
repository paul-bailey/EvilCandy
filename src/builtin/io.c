/* builtin/io.c - Implementation of the __gbl__.Io built-in object */
#include <evilcandy.h>
#include <fcntl.h> /* open() */
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h> /* fstat() */

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
 * @fr_write:    Function for internal C code to call to write
 * @fr_read:     Function for internal C code to call to read
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
        ssize_t (*fr_write)(struct rawfile_t *self, Object *data);
        Object *(*fr_read)(struct rawfile_t *self, ssize_t size);
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
#define ft_read         ft_raw.fr_read
#define ft_write        ft_raw.fr_write
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
#define fb_fdpos        fb_raw.fr_pos
#define fb_read         fb_raw.fr_read
#define fb_write        fb_raw.fr_write
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
        raw->fr_magic    = DICT_MAGIC_FILE;
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
filerr_sys(const char *fname)
{
        err_setstr(SystemError, "%s%ssystem call failed",
                   fname ? fname : "", fname ? "() " : "");
}

static void
filerr_malformed(const char *fname)
{
        filerr(fname, "file's dictionary corrupted");
}

static void
filerr_permit(const char *fname, bool iswrite)
{
        static const char *unread = "file is not readable";
        static const char *unwrite = "file is not writable";
        filerr(fname, iswrite ? unwrite : unread);
}

/*
 * TYPE##file_get_priv - functions that get private data out of dict.
 * @fo:    Dictionary
 * @fname: Name of function (as user would see it), for error reporting
 * @check_open: if true, throw an exception if the file is not open.
 */

static struct rawfile_t *
file_get_priv(Object *fo, const char *fname,
              bool check_open, enum file_type_t type)
{
        struct rawfile_t *ret;

        if (!isvar_dict(fo)) {
                filerr(fname, "object is not a file");
                return NULL;
        }
        ret = (struct rawfile_t *)dict_get_priv(fo);
        if (!ret || ret->fr_magic != DICT_MAGIC_FILE ||
            ((int)type >= 0 && ret->fr_type != type)) {
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
        return file_get_priv(fo, fname, check_open, FILE_RAW);
}

static struct rawfile_t *
rawfile_fget_priv(Frame *fr, const char *fname, bool check_open)
{
        Object *fo = vm_get_this(fr);
        bug_on(!isvar_dict(fo));
        return rawfile_get_priv(fo, fname, check_open);
}

static inline struct textfile_t *
textfile_get_priv(Object *fo, const char *fname, bool check_open)
{
        return (struct textfile_t *)file_get_priv(fo, fname,
                                                  check_open, FILE_TEXT);
}

static struct textfile_t *
textfile_fget_priv(Frame *fr, const char *fname, bool check_open)
{
        Object *fileobj = vm_get_this(fr);
        bug_on(!isvar_dict(fileobj));
        return textfile_get_priv(fileobj, fname, check_open);
}

/* **********************************************************************
 *                  Any-file callbacks et al.
 ***********************************************************************/

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
 * common to XXX_destructor().
 * If return value is NULL, destructor should bail early
 */
static struct rawfile_t *
destroy_common(Object *fo)
{
        bool err;
        struct rawfile_t *raw;
        int fd;
        char *s;

        err = err_occurred();
        raw = rawfile_get_priv(fo, NULL, 0);
        if (!raw) {
                if (!err)
                        err_clear();
                return NULL;
        }
        fd = raw->fr_fd;
        raw->fr_fd = -1;
        if (raw->fr_closefd && fd >= 0)
                close(fd);

        s = raw->fr_name;
        raw->fr_name = NULL;
        if (s)
                efree(s);

        s = raw->fr_mode;
        raw->fr_mode = NULL;
        if (s)
                efree(s);

        return raw;
}

/*
 * Common code to get file private from Frame during str method,
 * since it's error-prone, being a rare case where 'this' is arg 0
 * instead of vm_get_this().
 *
 * If NULL, bail early and return NullVar without producing a ref.
 */
static struct rawfile_t *
file_str_get_priv(Frame *fr)
{
        Object *fo;
        struct rawfile_t *raw;
        bool err;

        err = err_occurred();
        /*
         * Dict .str alternative acts upon arg 0, not 'this'
         */
        fo = vm_get_arg(fr, 0);
        if (!fo || !isvar_dict(fo))
                return NULL;
        raw = rawfile_get_priv(fo, NULL, 0);
        if (!raw) {
                if (!err)
                        err_clear();
                return NULL;
        }
        return raw;
}

/* **********************************************************************
 *              Raw (unbuffered binary) files
 ***********************************************************************/

static ssize_t
raw_read_wrapper(int fd, void *buf, size_t size)
{
        void *p = buf;
        void *end = buf + size;
        while (p < end) {
                ssize_t nread;

                errno = 0;
                nread = read(fd, p, end-p);
                if (nread < 0) {
                        if (errno == EINTR)
                                continue;
                        filerr_sys("read");
                        efree(buf);
                        return -1;
                } else if (nread == 0) {
                        break;
                }
                p += nread;
        }
        return p - buf;
}

static Object *
do_raw_read_(struct rawfile_t *raw, ssize_t size)
{
        void *buf;
        ssize_t nread;

        if (!size) {
                /* something sus here, but roll with it */
                return gbl.empty_bytes
                        ? VAR_NEW_REF(gbl.empty_bytes)
                        : bytesvar_new((unsigned char *)"", 0);
        }

        if (size < 0) {
                struct stat st;
                if (fstat(raw->fr_fd, &st) < 0 ||
                    st.st_size < raw->fr_pos) {
                        err_setstr(SystemError,
                                "read() cannot stat file%s%s",
                                raw->fr_name ? " " : "", raw->fr_name);
                        return ErrorVar;
                }
                size = st.st_size;
        }

        /* XXX: SystemError instead of abort? */
        buf = emalloc(size);
        nread = raw_read_wrapper(raw->fr_fd, buf, size);
        if (nread < 0)
                return ErrorVar;
        if (nread != size)
                buf = erealloc(buf, nread);
        return bytesvar_nocopy(buf, nread);
}

static ssize_t
do_raw_write_(struct rawfile_t *raw, Object *bo)
{
        ssize_t nwritten;
        void *buf, *p, *end;

        bug_on(!isvar_bytes(bo));
        buf = p = bytes_get_data(bo);
        end = buf + seqvar_size(bo);
        while (p < end) {
                errno = 0;
                nwritten = write(raw->fr_fd, p, end - p);
                if (nwritten <= 0) {
                        if (errno == EINTR)
                                continue;
                        filerr_sys("write");
                        return RES_ERROR;
                }
                p += nwritten;
        }
        return RES_OK;
}

static Object *
do_raw_read(Frame *fr)
{
        long long size = -1LL;
        struct rawfile_t *raw = rawfile_fget_priv(fr, "read", 1);
        if (!raw)
                return ErrorVar;
        if (vm_getargs(fr, "[|l]:read", &size) == RES_ERROR)
                return ErrorVar;

        if (!raw->fr_readable) {
                filerr_permit("read", 0);
                return ErrorVar;
        }

        return do_raw_read_(raw, (ssize_t)size);
}

static Object *
do_raw_write(Frame *fr)
{
        ssize_t ret;
        Object *bo;
        struct rawfile_t *raw = rawfile_fget_priv(fr, "write", 1);
        if (!raw)
                return ErrorVar;
        if (vm_getargs(fr, "<s>:write", &bo) == RES_ERROR)
                return ErrorVar;
        if (!raw->fr_writable) {
                filerr_permit("write", 1);
                return ErrorVar;
        }
        ret = do_raw_write_(raw, bo);
        if (ret < 0)
                return ErrorVar;
        return intvar_new(ret);
}

static Object *
do_raw_close(Frame *fr)
{
        int fd;
        struct rawfile_t *raw = rawfile_fget_priv(fr, "close", 0);
        if (!raw)
                return ErrorVar;

        fd = raw->fr_fd;
        raw->fr_fd = -1;
        if (fd >= 0)
                close(fd);
        return NULL;
}

static void
raw_destructor(Object *fo)
{
        struct rawfile_t *raw = destroy_common(fo);
        if (raw)
                efree(raw);
}

static Object *
raw_str(Frame *fr)
{
        struct buffer_t b;
        struct rawfile_t *raw = file_str_get_priv(fr);
        if (!raw)
                return NullVar;

        buffer_init(&b);
        buffer_printf(&b, "<file name='%s' mode='%s'>",
                        raw->fr_name ? raw->fr_name : "!",
                        raw->fr_mode);
        return stringvar_from_buffer(&b);
}

static Object *
open_raw(int fd, struct fileconfig_t *cfg)
{
        static const struct type_inittbl_t rawfile_cb_methods[] = {
                V_INITTBL("read",       do_raw_read,     1, 1,  0, -1),
                V_INITTBL("write",      do_raw_write,    1, 1, -1, -1),
                V_INITTBL("close",      do_raw_close,    0, 0, -1, -1),
                V_INITTBL("getfd",      do_getfd,        0, 0, -1, -1),
                V_INITTBL("iseof",      do_iseof,        0, 0, -1, -1),
                TBLEND,
        };
        struct rawfile_t *raw;
        Object *ret, *strfunc;

        raw = file_new(fd, sizeof(*raw), cfg);
        if (cfg->readable)
                raw->fr_read = do_raw_read_;
        if (cfg->writable)
                raw->fr_write = do_raw_write_;
        strfunc = funcvar_new_intl(raw_str, 1, 1);

        ret = dictvar_from_methods(NULL, rawfile_cb_methods);
        dict_set_priv(ret, raw);
        dict_add_cdestructor(ret, raw_destructor);
        dict_setstr(ret, strfunc);

        VAR_DECR_REF(strfunc);

        return ret;
}

/* **********************************************************************
 *              (Buffered) binary files
 ***********************************************************************/

static Object *
open_binary(int fd, struct fileconfig_t *cfg)
{
        err_setstr(NotImplementedError, "binary files not yet implemented");
        return ErrorVar;
}

/* **********************************************************************
 *              (Buffered) text files
 ***********************************************************************/

/* helper to text_append_chunk */
static ssize_t
text_read_chunk(struct textfile_t *txt, void *buf, size_t bufsize)
{
        ssize_t res = raw_read_wrapper(txt->ft_fd, buf, bufsize);
        if (res > 0)
                txt->ft_fdpos += res;
        return res;
}

static enum result_t
text_append_chunk(struct textfile_t *txt)
{
        enum { CHUNK_SIZE = 256 };
        unsigned char *chunk = emalloc(CHUNK_SIZE);
        unsigned char *readp;
        ssize_t nread, chunksize, nstraggler;

        readp = chunk;
        chunksize = CHUNK_SIZE;
        nstraggler = txt->ft_nstraggler;
        txt->ft_nstraggler = 0;
        if (nstraggler) {
                memcpy(chunk, txt->ft_stragglers, nstraggler);
                readp += nstraggler;
                chunksize -= nstraggler;
        }
        nread = text_read_chunk(txt, readp, chunksize);
        if (nread < 0) {
                /* TODO: need more error handling than just this */
                efree(chunk);
                filerr_sys("readline");
                return RES_ERROR;
        } else if (!nread) {
                txt->ft_eof = true;
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
                if (txt->ft_buf) {
                        string_writer_append_strobj(&wr, txt->ft_buf);
                        VAR_DECR_REF(txt->ft_buf);
                        txt->ft_buf = NULL;
                }

                nappend = string_writer_decode(&wr, chunk,
                                               nread, txt->ft_codec, true);
                if (nappend < 0) {
                        string_writer_destroy(&wr);
                        return RES_ERROR;
                } else if (nappend != nread) {
                        /* copy new stagglers */
                        txt->ft_nstraggler = nread - nappend;
                        bug_on(nstraggler > sizeof(txt->ft_stragglers));
                        memcpy(txt->ft_stragglers, &chunk[nread],
                               txt->ft_nstraggler);
                        txt->ft_nstraggler = nstraggler;
                }
                txt->ft_buf = stringvar_from_writer(&wr);
        }
        efree(chunk);
        return RES_OK;
}

static char *
readinto_chunkly(int fd, size_t *size, ssize_t lim)
{
        enum { GROWSIZ = 256 };
        char *buf = erealloc(NULL, GROWSIZ);
        size_t allocsize = GROWSIZ;
        if (lim >= 0 && allocsize > lim)
                allocsize = lim;
        char *p = buf;
        char *end = buf + allocsize;
        for (;;) {
                /* XXX: wrap text_read_chunk above instead of this */
                size_t pi, gs;
                while (p < end) {
                        ssize_t n = read(fd, p, end - p);
                        if (n < 0) {
                                efree(buf);
                                filerr_sys("read");
                                return NULL;
                        } else if (n == 0) {
                                break;
                        }
                        p += n;
                }
                if (p != end)
                        break;
                pi = p - buf;
                gs = GROWSIZ;
                if (lim > 0 && allocsize + gs > lim)
                        gs = lim - allocsize;
                if (!gs)
                        break;
                allocsize += gs;
                buf = erealloc(buf, allocsize);
                p = buf + pi;
                end = p + gs;
        }
        *size = p - buf;
        return buf;
}

static char *
readinto(int fd, off_t pos, size_t *nread, ssize_t lim)
{
        struct stat st;
        size_t allocbytes;
        char *buf, *p, *end;

        if (fstat(fd, &st) < 0)
                goto slowpath;
        if (st.st_size < pos) /* ?!?! */
                goto slowpath;
        allocbytes = (size_t)(st.st_size - pos);
        if (lim >= 0 && allocbytes > lim)
                allocbytes = lim;
        buf = emalloc(allocbytes);
        end = buf + allocbytes;
        p = buf;

        while (p < end) {
                ssize_t n = read(fd, p, end - p);
                if (n < 0) {
                        efree(buf);
                        filerr_sys("read");
                        return NULL;
                } else if (n == 0) {
                        /* ?!?! */
                        break;
                }
                p += n;
        }
        *nread = p - buf;
        return buf;

slowpath:
        return readinto_chunkly(fd, nread, lim);
}

static Object *
do_text_read_(struct rawfile_t *fr, ssize_t size)
{
        Object *ret;
        size_t nread;
        char *tbuf;
        struct textfile_t *txt = (struct textfile_t *)fr;
        if (txt->ft_eof) {
        eof:
                if (txt->ft_buf) {
                        ret = txt->ft_buf;
                        txt->ft_buf = NULL;
                        /*
                         * Don't need to produce ref, we're handing
                         * it off instead.
                         * FIXME: what if seqvar_size(ret) > size?
                         */
                } else {
                        ret = VAR_NEW_REF(STRCONST_ID(mpty));
                }
                return ret;
        }
        /*
         * FIXME: this function here as well as readinto...
         * need to take txt->ft_buf into account, and also @size
         * refers to Unicode points, not bytes.
         */
        tbuf = readinto(txt->ft_fd, txt->ft_fdpos, &nread, size);
        if (!tbuf) {
                return ErrorVar;
        } else if (!nread) {
                txt->ft_eof = true;
                efree(tbuf);
                goto eof;
        }
        if (txt->ft_buf) {
                Object *new;

                /* XXX: Surely there's a better way! */
                new = stringvar_from_binary(tbuf, nread, txt->ft_codec);
                ret = string_cat(txt->ft_buf, new);
                VAR_DECR_REF(new);
                VAR_DECR_REF(txt->ft_buf);
                txt->ft_buf = NULL;
                return ret;
        } else {
                return stringvar_from_binary(tbuf, nread, txt->ft_codec);
        }
 }

static Object *
do_text_read(Frame *fr)
{
        long long size = -1LL;
        struct textfile_t *txt = textfile_fget_priv(fr, "read", 1);
        if (!txt)
                return ErrorVar;
        if (vm_getargs(fr, "[|l]:read", &size) == RES_ERROR)
                return ErrorVar;
        if (!txt->ft_readable) {
                filerr_permit("read", 0);
                return ErrorVar;
        }
        return do_text_read_((struct rawfile_t *)txt, (ssize_t)size);
}

static Object *
do_text_readline(Frame *fr)
{
        Object *ret = NULL;
        ssize_t haveidx = 0;
        size_t seekpos;
        struct textfile_t *txt = textfile_fget_priv(fr, "readline", 1);
        if (!txt)
                return ErrorVar;

        if (!txt->ft_readable) {
                filerr_permit("readline", 0);
                return ErrorVar;
        }

        seekpos = txt->ft_bufpos;
        while (1) {
                if (txt->ft_buf) {
                        /*
                         * XXX: change string_search to also take a startpos,
                         * and increment that in this loop, so that we don't
                         * end up repeating search of earlier characters.
                         */
                        haveidx = string_search(txt->ft_buf, txt->ft_eol, seekpos);
                        if (haveidx >= 0) {
                                /* EOL exists in our buffer */
                                haveidx += seqvar_size(txt->ft_eol);
                                bug_on(haveidx > seqvar_size(txt->ft_buf));
                                break;
                        }
                        /* Don't re-seek old misses */
                        seekpos = seqvar_size(txt->ft_buf) - seqvar_size(txt->ft_eol);
                }

                if (txt->ft_eof) {
                        if (txt->ft_buf) {
                                haveidx = seqvar_size(txt->ft_buf);
                                break;
                        } else {
                                return VAR_NEW_REF(STRCONST_ID(mpty));
                        }
                } else {
                        text_append_chunk(txt);
                }
        }

        /* Return the buffer */
        bug_on(!txt->ft_buf);
        bug_on(haveidx <= txt->ft_bufpos);
        if (txt->ft_bufpos == 0 && haveidx == seqvar_size(txt->ft_buf)) {
                /*
                 * Special rare case: we have exactly a line.  No need to
                 * produce a reference, because we would just consume it
                 * again right here.
                 */
                ret = txt->ft_buf;
                txt->ft_buf = NULL;
                txt->ft_bufpos = 0;
                return ret;
        }

        bug_on(haveidx > seqvar_size(txt->ft_buf));
        ret = string_getslice(txt->ft_buf, txt->ft_bufpos,
                              haveidx, 1);
        txt->ft_bufpos = haveidx;
        if (txt->ft_bufpos == seqvar_size(txt->ft_buf)) {
                Object *tmp = txt->ft_buf;
                txt->ft_buf = NULL;
                txt->ft_bufpos = 0;
                VAR_DECR_REF(tmp);
        }
        return ret;
}

static ssize_t
do_text_write_(struct rawfile_t *fr, Object *so)
{
        ssize_t ret;
        struct textfile_t *txt = (struct textfile_t *)fr;
        /*
         * TODO: Should use txt->ft_buf... flush that one and
         * write until eol, or deliberately add eol here, if
         * the file is so configured.
         */
        if (string_isascii(so) || txt->ft_codec == CODEC_UTF8) {
                /* Easy thing: Just write the C string as-is */
                size_t len = string_nbytes(so);
                ret = write(txt->ft_fd, string_cstring(so), len);
        } else if (txt->ft_codec == CODEC_ASCII) {
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
                ret = write(txt->ft_fd, data, seqvar_size(so));
                if (ret < 0)
                        filerr_sys("write");
        }
        return ret;
}

static Object *
do_text_write(Frame *fr)
{
        ssize_t ret;
        Object *so;
        struct textfile_t *txt = textfile_fget_priv(fr, "write", 1);
        if (!txt)
                return ErrorVar;
        if (vm_getargs(fr, "<s>:write", &so) == RES_ERROR)
                return ErrorVar;
        if (!txt->ft_writable) {
                filerr_permit("write", 1);
                return ErrorVar;
        }
        ret = do_text_write_((struct rawfile_t *)txt, so);
        if (ret < 0)
                return ErrorVar;
        return intvar_new(ret);
}

static Object *
do_text_close(Frame *fr)
{
        int fd;
        struct textfile_t *txt = textfile_fget_priv(fr, "close", 0);
        if (!txt)
                return ErrorVar;

        /* TODO: If buffer, flush it */
        fd = txt->ft_fd;
        txt->ft_fd = -1;
        if (fd >= 0)
                close(fd);
        return NULL;
}

static Object *
text_str(Frame *fr)
{
        struct textfile_t *txt;
        const char *codecstr;
        struct buffer_t b;

        txt = (struct textfile_t *)file_str_get_priv(fr);
        if (!txt)
                return NullVar;

        switch (txt->ft_codec) {
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
                      txt->ft_name ? txt->ft_name : "!",
                      txt->ft_mode, codecstr);
        return stringvar_from_buffer(&b);
}

static void
text_destructor(Object *fo)
{
        struct textfile_t *txt = (struct textfile_t *)destroy_common(fo);
        if (txt) {
                Object *o;

                o = txt->ft_buf;
                txt->ft_buf = NULL;
                if (o)
                        VAR_DECR_REF(o);

                o = txt->ft_eol;
                txt->ft_eol = NULL;
                if (o)
                        VAR_DECR_REF(o);

                efree(txt);
        }
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
                V_INITTBL("read",       do_text_read,     1, 1,  0, -1),
                V_INITTBL("readline",   do_text_readline, 0, 0, -1, -1),
                V_INITTBL("write",      do_text_write,    1, 1, -1, -1),
                V_INITTBL("close",      do_text_close,    0, 0, -1, -1),
                V_INITTBL("getfd",      do_getfd,         0, 0, -1, -1),
                V_INITTBL("iseof",      do_iseof,         0, 0, -1, -1),
                TBLEND,
        };
        struct textfile_t *txt;
        Object *ret, *strfunc;

        txt = file_new(fd, sizeof(*txt), cfg);
        txt->ft_codec = codec;
        /* FIXME: this should be an argument */
        txt->ft_eol = stringvar_new("\n");
        if (cfg->readable)
                txt->ft_read = do_text_read_;
        if (cfg->writable)
                txt->ft_write = do_text_write_;

        strfunc = funcvar_new_intl(text_str, 1, 1);

        ret = dictvar_from_methods(NULL, textfile_cb_methods);
        dict_set_priv(ret, txt);
        dict_add_cdestructor(ret, text_destructor);
        dict_setstr(ret, strfunc);

        VAR_DECR_REF(strfunc);

        return ret;
}

/* **********************************************************************
 *              Module-level code and API
 ***********************************************************************/

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

/**
 * evc_file_read - Read from a file
 * @fo: File object to read from
 * @size: Number of bytes to read if file is binary
 *        Number of Unicode points to read if file is text
 *        -1 to read the whole file
 *
 * Return: A bytes object if the file is binary
 *         A string object if the file is text
 *         ErrorVar if there was an error.
 *
 * This is for the internal C code to read from files.
 * User code calls @fo's 'read[line]' method.
 */
Object *
evc_file_read(Object *fo, ssize_t size)
{
        struct rawfile_t *f;
        f = file_get_priv(fo, NULL, 1, -1);
        if (!f)
                return ErrorVar;

        if (!f->fr_read) {
                filerr_permit("internal_read", 0);
                return ErrorVar;
        }
        return f->fr_read(f, size);
}

/**
 * evc_file_write - Write to a file
 * @fo: File object to write to
 * @data: Data to write, either bytes or string,
 *        depending on the type and mode of the file.
 *
 * Return: Number of bytes (not Unicode points) written
 *         -1 if there was an error.
 *         XXX: assymetric to evc_file_read w/r/t nbytes
 *
 * This is for the internal C code to write to files.
 * User code calls @fo's 'write' method.
 */
ssize_t
evc_file_write(Object *fo, Object *data)
{
        struct rawfile_t *f;
        f = file_get_priv(fo, NULL, 1, -1);
        if (!f)
                return -1;

        if (!f->fr_write) {
                filerr_permit("internal_write", 1);
                return -1;
        }
        return f->fr_write(f, data);
}

/**
 * isvar_file - Return true if @o is a file
 */
bool
isvar_file(Object *o)
{
        if (isvar_dict(o)) {
                struct rawfile_t *raw = dict_get_priv(o);
                if (raw)
                        return raw->fr_magic == DICT_MAGIC_FILE;
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


