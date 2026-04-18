/*
 * builtin/io.c - Implementation of the __gbl__.Io built-in object
 *
 * XXX REVISIT:  There is double-indirection going on, where binary and
 * text bufferd files call vm_exec_func() on their contained raw-file
 * handles.  This is because I wanted to make their underlying raw-file
 * implementation be arbitrary and user-defined.  But so far, that isn't
 * the case in this library, so we're doing things the slow way basically
 * for nothing.
 */
#include <evilcandy/compiler.h>
#include <evilcandy/debug.h>
#include <evilcandy/vm.h>
#include <evilcandy/global.h>
#include <evilcandy/err.h>
#include <evilcandy/errmsg.h>
#include <evilcandy/ewrappers.h>
#include <evilcandy/string_writer.h>
#include <evilcandy/types/array.h>
#include <evilcandy/types/bytes.h>
#include <evilcandy/types/class.h>
#include <evilcandy/types/dict.h>
#include <evilcandy/types/string.h>
#include <evilcandy/types/tuple.h>
#include <evilcandy/types/number_types.h>
#include <internal/codec.h>
#include <internal/type_registry.h>
#include <internal/types/string.h>
#include <internal/types/number_types.h>
#include <internal/types/sequential_types.h>
#include <internal/builtin/io.h>
#include <internal/init.h>
#include <lib/utf8.h>
#include <lib/helpers.h>

#include <fcntl.h> /* open() */
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h> /* fstat() */

enum file_type_t {
        FILE_TEXT,
        FILE_BINARY,
        FILE_RAW,
        FILE_ANY,
};

enum {
        IO_BUFFER_SIZE = 8 * 1024, /* yeahsurewhynot */
};

struct fileconfig_t {
        bool readable;
        bool writable;
        bool closefd;
        enum file_type_t type;
        char *name;
        char *mode;
};

static void initialize_file_classes(void);

static bool
io_classes_initialized(void)
{
        return gbl_borrow_builtin_class(GBL_CLASS_IOBASE) != NULL;
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
filerr_permit(const char *fname, bool iswrite)
{
        static const char *unread = "file is not readable";
        static const char *unwrite = "file is not writable";
        filerr(fname, iswrite ? unwrite : unread);
}

static void
filerr_closed(const char *fname)
{
        filerr(fname, "file closed");
}

static Object *
file_call(Object *instance, Object *method_name, Object *args)
{
        Object *ret;
        Object *func = var_getattr(NULL, instance, method_name);
        if (func == ErrorVar)
                return ErrorVar;
        if (!isvar_method(func)) {
                err_setstr(TypeError, "attribute %N is not callable",
                           method_name);
                VAR_DECR_REF(func);
                return ErrorVar;
        }
        ret = vm_exec_func(NULL, func, args, NULL);
        VAR_DECR_REF(func);
        return ret;
}

static Object *
file_call_stack(Object *instance, Object *method_name,
                Object **argstack, size_t nr_args)
{
        Object *args = arrayvar_from_stack(argstack, nr_args, false);
        Object *result = file_call(instance, method_name, args);
        VAR_DECR_REF(args);
        return result;
}

static Object *
file_call_1arg(Object *instance, Object *method_name, Object *arg)
{
        return file_call_stack(instance, method_name, &arg, 1);
}

static Object *
file_call_1arg_int(Object *instance, Object *method_name, long arg)
{
        Object *ival = intvar_new(arg);
        Object *result = file_call_stack(instance, method_name, &ival, 1);
        VAR_DECR_REF(ival);
        return result;
}

static Object *
file_call_noarg(Object *instance, Object *method_name)
{
        Object *args = arrayvar_new(0);
        Object *result = file_call(instance, method_name, args);
        VAR_DECR_REF(args);
        return result;
}


/* **********************************************************************
 *              Raw (unbuffered binary) files
 ***********************************************************************/

/**
 * struct rawfile_t - Raw unbuffered file
 * @fr_fd:       File descriptor, or -1 if an in-memory file only
 * @fr_writable: True if file is writable
 * @fr_readable: True if file is readable
 * @fr_closefd:  True to close file descriptor during garbage collection
 * @fr_pos:      Position in the file
 * @fr_write:    Function for internal C code to call to write
 * @fr_read:     Function for internal C code to call to read
 */
struct rawfile_t {
        Object base;
        int fr_magic;
        enum file_type_t fr_type;
        int fr_fd;
        char *fr_mode;
        char *fr_name;
        bool fr_writable;
        bool fr_readable;
        bool fr_closefd;
        off_t fr_pos;
        ssize_t (*fr_write)(struct rawfile_t *self, Object *data);
        Object *(*fr_read)(struct rawfile_t *self, ssize_t size);
};

static ssize_t
raw_read_wrapper(int fd, void *buf, size_t size)
{
        void *p = buf;
        void *end = voidp_add(buf, size);
        while (p < end) {
                ssize_t nread;

                errno = 0;
                nread = read(fd, p, voidp_diff(end, p));
                if (nread < 0) {
                        if (errno == EINTR)
                                continue;
                        filerr_sys("read");
                        return -1;
                } else if (nread == 0) {
                        break;
                }
                p = voidp_add(p, nread);
        }
        return voidp_diff(p, buf);
}

static ssize_t
raw_write_wrapper(int fd, const void *buf, size_t size)
{
        const void *p, *end;

        buf = p = buf;
        end = voidp_add(buf, size);
        while (p < end) {
                ssize_t nwritten;

                errno = 0;
                nwritten = write(fd, p, voidp_diff(end, p));
                if (nwritten <= 0) {
                        if (errno == EINTR)
                                continue;
                        filerr_sys("write");
                        return -1;
                }
                p = voidp_add(p, nwritten);
        }
        return voidp_diff(p, buf);
}

static Object *
raw_read(struct rawfile_t *raw, ssize_t size)
{
        void *buf;
        ssize_t nread;

        if (!size)
                return gbl_new_empty_bytes();

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
        if (nread < 0) {
                efree(buf);
                return ErrorVar;
        }
        if (nread != size)
                buf = erealloc(buf, nread);
        return bytesvar_nocopy(buf, nread);
}

static ssize_t
raw_write(struct rawfile_t *raw, Object *bo)
{
        bug_on(!isvar_bytes(bo));
        return raw_write_wrapper(raw->fr_fd,
                                 bytes_get_data(bo),
                                 seqvar_size(bo));
}

static Object *
do_raw_read(Frame *fr)
{
        long long size = -1LL;
        Object *fo;
        struct rawfile_t *raw;

        if (vm_getargs(fr, "<*>[|l!]{!}:read", &fo, &size) == RES_ERROR)
                return ErrorVar;
        bug_on(fo->v_type != &RawFileType);

        raw = (struct rawfile_t *)fo;
        if (raw->fr_fd < 0) {
                filerr_closed("read");
                return ErrorVar;
        }

        if (!raw->fr_readable) {
                filerr_permit("read", 0);
                return ErrorVar;
        }

        return raw_read(raw, (ssize_t)size);
}

static Object *
do_raw_write(Frame *fr)
{
        ssize_t ret;
        Object *bo, *fo;
        struct rawfile_t *raw;

        if (vm_getargs(fr, "<*>[<b>!]{!}:write", &fo, &bo) == RES_ERROR)
                return ErrorVar;
        bug_on(fo->v_type != &RawFileType);

        raw = (struct rawfile_t *)fo;
        if (raw->fr_fd < 0) {
                filerr_closed("write");
                return ErrorVar;
        }

        if (!raw->fr_writable) {
                filerr_permit("write", 1);
                return ErrorVar;
        }
        ret = raw_write(raw, bo);
        if (ret < 0)
                return ErrorVar;
        return intvar_new(ret);
}

static Object *
do_raw_tell(Frame *fr)
{
        Object *fo;
        struct rawfile_t *raw;
        off_t off;

        if (vm_getargs(fr, "<*>[!]{!}:tell", &fo) == RES_ERROR)
                return ErrorVar;

        raw = (struct rawfile_t *)fo;
        if (raw->fr_fd < 0) {
                filerr_closed("tell");
                return ErrorVar;
        }

        off = lseek(raw->fr_fd, (off_t)0, SEEK_CUR);
        if (off == (off_t)-1) {
                err_errno("OS lseek call failed");
                return ErrorVar;
        }
        return intvar_new((long long)off);
}

static Object *
do_raw_fileno(Frame *fr)
{
        Object *fo;
        struct rawfile_t *raw;

        if (vm_getargs(fr, "<*>[!]{!}:fileno", &fo) == RES_ERROR)
                return ErrorVar;

        raw = (struct rawfile_t *)fo;
        if (raw->fr_fd < 0) {
                filerr_closed("fileno");
                return ErrorVar;
        }
        return intvar_new(raw->fr_fd);
}

static Object *
do_raw_seek(Frame *fr)
{
        Object *fo;
        struct rawfile_t *raw;
        long long off;
        int whence;

        whence = SEEK_SET;
        if (vm_getargs(fr, "<*>[l|i!]{!}:seek", &fo, &off, &whence)
            == RES_ERROR) {
                return ErrorVar;
        }
        bug_on(fo->v_type != &RawFileType);

        raw = (struct rawfile_t *)fo;
        if (raw->fr_fd < 0) {
                filerr_closed("seek");
                return ErrorVar;
        }

        off = lseek(raw->fr_fd, (off_t)off, whence);
        if (off == (off_t)-1) {
                err_errno("OS lseek call failed");
                return ErrorVar;
        }

        return intvar_new((long long)off);
}

static Object *
do_raw_close(Frame *fr)
{
        int fd;
        Object *fo;
        struct rawfile_t *raw;

        if (vm_getargs(fr, "<*>[!]{!}:close", &fo) == RES_ERROR)
                return ErrorVar;
        bug_on(fo->v_type != &RawFileType);

        raw = (struct rawfile_t *)fo;

        fd = raw->fr_fd;
        raw->fr_fd = -1;
        if (fd >= 0)
                close(fd);
        return NULL;
}

static void
rawfile_reset(Object *rawfile)
{
        struct rawfile_t *raw;
        int fd;
        char *s;

        raw = (struct rawfile_t *)rawfile;
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
}

static Object *
raw_str(Object *fo)
{
        struct rawfile_t *raw;

        bug_on(fo->v_type != &RawFileType);
        raw = (struct rawfile_t *)fo;
        return stringvar_from_format("<file name='%s' mode='%s'>",
                        raw->fr_name ? raw->fr_name : "!", raw->fr_mode);
}

static Object *
open_raw(int fd, struct fileconfig_t *cfg)
{
        struct rawfile_t *raw;
        Object *instance;

        instance = var_new(&RawFileType);
        raw = (struct rawfile_t *)instance;
        raw->fr_magic    = DICT_MAGIC_FILE;
        raw->fr_type     = cfg->type;
        raw->fr_fd       = fd;
        raw->fr_mode     = cfg->mode;
        raw->fr_name     = cfg->name;
        raw->fr_closefd  = cfg->closefd;
        raw->fr_writable = cfg->writable;
        raw->fr_readable = cfg->readable;
        if (cfg->readable)
                raw->fr_read = raw_read;
        if (cfg->writable)
                raw->fr_write = raw_write;
        return instance;
}

/* **********************************************************************
 *              (Buffered) binary files
 ***********************************************************************/

struct binfile_t {
        Object base;
        Object *fb_raw;
        Object *fb_outbuf;
        size_t fb_outbuf_size;
        Object *fb_inbuf;
        size_t fb_inbuf_pos;
};

#define BINFILE_FILENO(bin_)    (((struct rawfile_t *)(bin_)->fb_raw)->fr_fd)
#define BINFILE_CLOSED(bin_)    (BINFILE_FILENO(bin_) < 0)
#define BINFILE_READABLE(bin_)  (((struct rawfile_t *)(bin_)->fb_raw)->fr_readable)
#define BINFILE_WRITABLE(bin_)  (((struct rawfile_t *)(bin_)->fb_raw)->fr_writable)

WARN_UNUSED_RESULT static int
bin_flush(struct binfile_t *bin)
{
        size_t i, n;
        Object *outbuf, **bufs;
        int res;

        outbuf = bin->fb_outbuf;
        if (!outbuf)
                return 0;
        bug_on(!isvar_array(outbuf));
        if (seqvar_size(outbuf) == 0) {
                bug_on(bin->fb_outbuf_size != 0);
                return 0;
        }

        n = seqvar_size(outbuf);
        bufs = array_get_data(outbuf);
        res = 0;
        for (i = 0; i < n; i++) {
                ssize_t nwritten;
                Object *result;
                size_t tlen = seqvar_size(bufs[i]);

                result = file_call_1arg(bin->fb_raw,
                                        STRCONST_ID(write), bufs[i]);
                if (result == ErrorVar)
                        goto err;
                bug_on(!result || !isvar_int(result));
                nwritten = intvar_toll(result);
                if (nwritten != tlen) {
        err:
                        if (result)
                                VAR_DECR_REF(result);
                        res = -1;
                        break;
                }
        }
        VAR_DECR_REF(bin->fb_outbuf);
        bin->fb_outbuf = 0;
        bin->fb_outbuf_size = 0;
        return res;
}

static Object *
bin_read(struct binfile_t *bin, ssize_t size)
{
        Object *inbuf, *inbuf2, *ret;
        size_t buf2size, needsize;

        inbuf = bin->fb_inbuf;
        buf2size = size;
        if (inbuf) {
                size_t len = seqvar_size(inbuf) - bin->fb_inbuf_pos;
                if (len >= size) {
                        /* We already have the requested data */
                        Object *ret;
                        size_t start = bin->fb_inbuf_pos;
                        size_t stop = start + size;
                        ret = bytes_getslice(inbuf, start, stop, 1);
                        if (stop == seqvar_size(inbuf)) {
                                VAR_DECR_REF(inbuf);
                                bin->fb_inbuf = NULL;
                                bin->fb_inbuf_pos = 0;
                        } else {
                                bin->fb_inbuf_pos = stop;
                        }
                        return ret;
                }
                buf2size -= len;
        }

        /* If still here, we need to read.  First flush write data */
        if (bin_flush(bin) < 0) {
                err_clear();
                filerr_sys("flush (during read)");
        }

        needsize = buf2size;
        if (needsize < IO_BUFFER_SIZE)
                needsize = IO_BUFFER_SIZE;

        inbuf2 = file_call_1arg_int(bin->fb_raw, STRCONST_ID(read), needsize);
        if (inbuf2 == ErrorVar)
                return ErrorVar;

        if (seqvar_size(inbuf2) < buf2size) {
                /* In case we hit EOF */
                size -= (buf2size - seqvar_size(inbuf2));
                buf2size = seqvar_size(inbuf2);
        }

        if (inbuf) {
                ret = bytesvar_new_sg(size,
                                bytes_get_data(inbuf) + bin->fb_inbuf_pos,
                                seqvar_size(inbuf) - bin->fb_inbuf_pos,
                                bytes_get_data(inbuf2), buf2size, NULL);
                VAR_DECR_REF(inbuf);
        } else {
                ret = bytesvar_new(bytes_get_data(inbuf2), buf2size);
        }

        if (seqvar_size(inbuf2) == 0) {
                VAR_DECR_REF(inbuf2);
                bin->fb_inbuf = NULL;
                bin->fb_inbuf_pos = 0;
        } else {
                bin->fb_inbuf = inbuf2;
                bin->fb_inbuf_pos = buf2size;
        }
        return ret;
}

static ssize_t
bin_write(struct binfile_t *bin, Object *bo)
{
        bug_on(!isvar_bytes(bo));

        if (!bin->fb_outbuf) {
                bin->fb_outbuf = arrayvar_new(0);
                bin->fb_outbuf_size = 0;
        }
        array_append(bin->fb_outbuf, bo);
        bin->fb_outbuf_size += seqvar_size(bo);

        if (bin->fb_outbuf_size > IO_BUFFER_SIZE) {
                if (bin_flush(bin) < 0)
                        return -1;
        }
        return seqvar_size(bo);
}

static Object *
do_bin_read(Frame *fr)
{
        long long size = -1LL;
        Object *fo;
        struct binfile_t *bin;

        if (vm_getargs(fr, "<*>[|l!]{!}:read", &fo, &size) == RES_ERROR)
                return ErrorVar;
        bug_on(!fo || fo->v_type != &BinFileType);

        bin = (struct binfile_t *)fo;
        if (BINFILE_CLOSED(bin)) {
                filerr_closed("read");
                return ErrorVar;
        }

        if (!BINFILE_READABLE(bin)) {
                filerr_permit("read", 0);
                return ErrorVar;
        }
        return bin_read(bin, (ssize_t)size);
}

static Object *
do_bin_write(Frame *fr)
{
        ssize_t ret;
        Object *bo, *fo;
        struct binfile_t *bin;

        if (vm_getargs(fr, "<*>[<b>!]{!}:write", &fo, &bo) == RES_ERROR)
                return ErrorVar;
        bug_on(!fo || fo->v_type != &BinFileType);

        bin = (struct binfile_t *)fo;
        if (BINFILE_CLOSED(bin)) {
                filerr_closed("write");
                return ErrorVar;
        }

        if (!BINFILE_WRITABLE(bin)) {
                filerr_permit("write", 1);
                return ErrorVar;
        }
        /* Invalidate read buffer */
        if (bin->fb_inbuf) {
                /* issue #46: rewind raw fd before dropping */
                size_t unread = seqvar_size(bin->fb_inbuf)
                                - bin->fb_inbuf_pos;

                if (unread != 0) {
                        /*
                         * FIXME: don't call lseek() directly.  Use
                         * file_call_stack() instead.
                         */
                        if (lseek(BINFILE_FILENO(bin),
                                  -(off_t)unread, SEEK_CUR) < 0) {
                                err_errno("could not sync for write",
                                          "write");
                                return ErrorVar;
                        }
                }
                VAR_DECR_REF(bin->fb_inbuf);
                bin->fb_inbuf = NULL;
                bin->fb_inbuf_pos = 0;
        }
        ret = bin_write(bin, bo);
        if (ret < 0)
                return ErrorVar;
        return intvar_new(ret);
}

static Object *
do_bin_flush(Frame *fr)
{
        Object *fo;
        struct binfile_t *bin;

        if (vm_getargs(fr, "<*>[!]{!}:flush", &fo) == RES_ERROR)
                return ErrorVar;
        bug_on(!fo || fo->v_type != &BinFileType);

        bin = (struct binfile_t *)fo;
        if (BINFILE_CLOSED(bin)) {
                filerr_closed("flush");
                return ErrorVar;
        }

        if (bin_flush(bin) < 0)
                return ErrorVar;
        return NULL;
}

static Object *
do_bin_tell(Frame *fr)
{
        Object *fo, *offobj;
        long long off;
        struct binfile_t *bin;

        if (vm_getargs(fr, "<*>[!]{!}:tell", &fo) == RES_ERROR)
                return ErrorVar;
        bug_on(!fo || fo->v_type != &BinFileType);

        bin = (struct binfile_t *)fo;
        if (BINFILE_CLOSED(bin)) {
                filerr_closed("tell");
                return ErrorVar;
        }

        offobj = file_call_noarg(bin->fb_raw, STRCONST_ID(tell));
        if (offobj == ErrorVar)
                return ErrorVar;
        if (bin->fb_outbuf_size == 0 && bin->fb_inbuf_pos == 0)
                return offobj;

        off = intvar_toll(offobj);
        VAR_DECR_REF(offobj);

        if (bin->fb_outbuf_size != 0 && bin->fb_inbuf_pos != 0) {
                bool once = false;
                if (!once) {
                        DBUG1("suspected BUG: file read/write buffers are simultaneously valid");
                        once = true;
                }
        }

        if (bin->fb_outbuf_size)
                off += bin->fb_outbuf_size;
        if (bin->fb_inbuf)
                off -= seqvar_size(bin->fb_inbuf) - bin->fb_inbuf_pos;
        return intvar_new(off);
}

static Object *
do_bin_seek(Frame *fr)
{
        Object *fo, *ret;
        Object *argstack[2];
        long long offs;
        struct binfile_t *bin;
        size_t nr_args;

        argstack[1] = NULL;
        if (vm_getargs(fr, "<*>[l|<i>!]{!}:seek", &fo,
                       &offs, &argstack[1]) == RES_ERROR) {
                return ErrorVar;
        }

        bin = (struct binfile_t *)fo;
        if (BINFILE_CLOSED(bin)) {
                filerr_closed("seek");
                return ErrorVar;
        }

        nr_args = argstack[1] ? 2 : 1;
        if (bin_flush(bin) < 0)
                return ErrorVar;
        if (bin->fb_inbuf) {
                /* issue #45: Account for unread buffer bytes on SEEK_CUR */
                if (argstack[1] &&
                    intvar_toll(argstack[1]) == SEEK_CUR) {
                        offs -= (seqvar_size(bin->fb_inbuf)
                                 - bin->fb_inbuf_pos);
                }
                VAR_DECR_REF(bin->fb_inbuf);
                bin->fb_inbuf = NULL;
        }
        bin->fb_inbuf_pos = 0;
        argstack[0] = intvar_new(offs);

        ret = file_call_stack(bin->fb_raw, STRCONST_ID(seek),
                              argstack, nr_args);
        VAR_DECR_REF(argstack[0]);
        return ret;
}

static Object *
do_bin_fileno(Frame *fr)
{
        Object *fo;
        struct binfile_t *bin;
        int fd;

        if (vm_getargs(fr, "<*>[!]{!}:fileno", &fo) == RES_ERROR)
                return ErrorVar;
        bug_on(!fo || fo->v_type != &BinFileType);

        bin = (struct binfile_t *)fo;
        fd = BINFILE_FILENO(bin);
        if (fd < 0) {
                filerr_closed("fileno");
                return ErrorVar;
        }
        return intvar_new(fd);
}

static Object *
do_bin_close(Frame *fr)
{
        /*
         * TODO: Flush write buffer, delete read buffer,
         * then call system close().
         */
        Object *fo;
        struct binfile_t *bin;

        if (vm_getargs(fr, "<*>[!]{!}:close", &fo) == RES_ERROR)
                return ErrorVar;
        bug_on(!fo || fo->v_type != &BinFileType);

        bin = (struct binfile_t *)fo;
        if (BINFILE_CLOSED(bin)) /* not an error */
                return NULL;

        if (bin_flush(bin) < 0) {
                filerr_sys("close[_on_flush]");
                return ErrorVar;
        }
        bug_on(bin->fb_outbuf);
        if (bin->fb_inbuf) {
                VAR_DECR_REF(bin->fb_inbuf);
                bin->fb_inbuf = NULL;
        }
        bin->fb_outbuf_size = 0;
        bin->fb_inbuf_pos = 0;

        return file_call_noarg(bin->fb_raw, STRCONST_ID(close));
}

static void
binfile_reset(Object *binfile)
{
        struct binfile_t *bin = (struct binfile_t *)binfile;
        if (bin->fb_raw)
                VAR_DECR_REF(bin->fb_raw);
        if (bin->fb_inbuf)
                VAR_DECR_REF(bin->fb_inbuf);
        if (bin->fb_outbuf)
                VAR_DECR_REF(bin->fb_outbuf);
}

static Object *
bin_str(Object *fo)
{
        struct binfile_t *bin;
        struct rawfile_t *raw;

        bug_on(!fo || fo->v_type != &BinFileType);

        bin = (struct binfile_t *)fo;
        raw = (struct rawfile_t *)bin->fb_raw;
        if (!raw)
                return VAR_NEW_REF(NullVar);

        return stringvar_from_format("<file name='%s' mode='%s'>",
                                     raw->fr_name ? raw->fr_name : "!",
                                     raw->fr_mode);
}

static Object *
open_binary(int fd, struct fileconfig_t *cfg, Object *raw)
{
        struct binfile_t *bin;
        Object *instance;

        instance = var_new(&BinFileType);
        bin = (struct binfile_t *)instance;
        bin->fb_raw = raw;
        return instance;
}

/* **********************************************************************
 *              (Buffered) text files
 ***********************************************************************/

struct textfile_t {
        Object base;
        Object *ft_raw;
        int ft_codec;
        Object *ft_eol;
        Object *ft_buf;
        size_t ft_bufpos;
        struct utf8_state_t ft_utf8_state;
        off_t ft_fdpos;
        /* XXX: ft_eof confusing if seek is used */
        bool ft_eof;
};

#define TEXTFILE_FILENO(t_)     (((struct rawfile_t *)((t_)->ft_raw))->fr_fd)
#define TEXTFILE_CLOSED(t_)     (TEXTFILE_FILENO(t_) < 0)
#define TEXTFILE_READABLE(t_)   (((struct rawfile_t *)((t_)->ft_raw))->fr_readable)
#define TEXTFILE_WRITABLE(t_)   (((struct rawfile_t *)((t_)->ft_raw))->fr_writable)

static inline void
reset_decode_state(struct textfile_t *txt)
{
        memset(&txt->ft_utf8_state, 0, sizeof(txt->ft_utf8_state));
}

static inline bool
decode_state_reset(struct textfile_t *txt)
{
        return txt->ft_utf8_state.state == UTF8_STATE_ASCII;
}

static inline ssize_t
text_decode(struct textfile_t *txt, struct string_writer_t *wr,
            void *buf, size_t size)
{
        return string_writer_decode(wr, buf, size,
                                    txt->ft_codec, &txt->ft_utf8_state);
}

/* returns bytes (not Unicode points) read, or -1 if error */
static ssize_t
text_readbuf(struct textfile_t *txt, const char *fname)
{
        struct string_writer_t wr;
        Object *buf;
        ssize_t nread, res;

        if (txt->ft_eof)
                return 0;
        buf = file_call_1arg_int(txt->ft_raw, STRCONST_ID(read), IO_BUFFER_SIZE);
        if (buf == ErrorVar) {
                reset_decode_state(txt);
                return -1;
        }

        bug_on(!isvar_bytes(buf));
        nread = seqvar_size(buf);
        if (nread == 0) {
                VAR_DECR_REF(buf);
                txt->ft_eof = true;
                buf = NULL;
        }

        txt->ft_fdpos += nread;
        string_writer_init(&wr, 1);
        if (txt->ft_buf) {
                string_writer_append_strobj(&wr, txt->ft_buf);
                VAR_DECR_REF(txt->ft_buf);
                txt->ft_buf = NULL;
        }
        if (buf) {
                res = text_decode(txt, &wr, bytes_get_data(buf), nread);
                VAR_DECR_REF(buf);
                if (res < 0)
                        goto decode_err;
        }

        if (nread < IO_BUFFER_SIZE) {
                /* end of file on disk, make sure there were no stragglers */
                res = decode_state_reset(txt) ? 0 : -1;
                if (res < 0)
                        goto decode_err;
        }

        txt->ft_buf = stringvar_from_writer(&wr);
        return nread;

decode_err:
        string_writer_destroy(&wr);
        if (!err_occurred())
                filerr(fname, "file decoding error");
        reset_decode_state(txt);
        txt->ft_bufpos = 0;
        return -1;
}

/* size is number of Unicode points, not bytes */
static Object *
text_read(struct textfile_t *txt, ssize_t size)
{
        ssize_t nread;
        Object *ret;

        for (;;) {
                /* check if entire read amount is in buffer */
                if (size > 0 && txt->ft_buf) {
                        size_t n = seqvar_size(txt->ft_buf);
                        size_t pos = txt->ft_bufpos;
                        if (n - pos > size) {
                                txt->ft_bufpos = pos + size;
                                /* check for special rare case */
                                if (txt->ft_bufpos == n && pos == 0)
                                        goto exact_buffer;
                                return stringvar_from_substr(txt->ft_buf,
                                                        pos, pos + size);
                        }
                }

                if ((nread = text_readbuf(txt, "read")) < 0)
                        return ErrorVar;

                if (!nread) {
                        if (txt->ft_buf)
                                goto exact_buffer;
                        else
                                return VAR_NEW_REF(STRCONST_ID(mpty));
                }
        }

exact_buffer:
        ret = txt->ft_buf;
        txt->ft_bufpos = 0;
        txt->ft_buf = NULL;
        return ret;
}

static Object *
do_text_read(Frame *fr)
{
        long long size = -1LL;
        struct textfile_t *txt;
        Object *fo;

        if (vm_getargs(fr, "<*>[|l!]{!}:read", &fo, &size) == RES_ERROR)
                return ErrorVar;
        bug_on(!fo || fo->v_type != &TextFileType);

        txt = (struct textfile_t *)fo;
        if (TEXTFILE_CLOSED(txt)) {
                filerr_closed("read");
                return ErrorVar;
        }
        if (!TEXTFILE_READABLE(txt)) {
                filerr_permit("read", 0);
                return ErrorVar;
        }
        return text_read(txt, (ssize_t)size);
}

static Object *
do_text_readline(Frame *fr)
{
        Object *fo, *ret = NULL;
        ssize_t haveidx = 0;
        size_t seekpos;
        struct textfile_t *txt;

        if (vm_getargs(fr, "<*>[!]{!}:readline", &fo) == RES_ERROR)
                return ErrorVar;
        bug_on(!fo || fo->v_type != &TextFileType);

        txt = (struct textfile_t *)fo;
        if (TEXTFILE_CLOSED(txt)) {
                filerr_closed("readline");
                return ErrorVar;
        }

        if (!TEXTFILE_READABLE(txt)) {
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
                        haveidx = string_search(txt->ft_buf,
                                                txt->ft_eol, seekpos);
                        if (haveidx >= 0) {
                                /* EOL exists in our buffer */
                                haveidx += seqvar_size(txt->ft_eol);
                                bug_on(haveidx > seqvar_size(txt->ft_buf));
                                break;
                        }
                        /* Don't re-seek old misses */
                        seekpos = seqvar_size(txt->ft_buf)
                                  - seqvar_size(txt->ft_eol);
                }

                if (txt->ft_eof) {
                        if (txt->ft_buf) {
                                haveidx = seqvar_size(txt->ft_buf);
                                break;
                        } else {
                                return VAR_NEW_REF(STRCONST_ID(mpty));
                        }
                } else {
                        text_readbuf(txt, "readline");
                }
        }

        /* Return the buffer */
        bug_on(!txt->ft_buf);
        bug_on(haveidx != 0 && haveidx <= txt->ft_bufpos);
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
        ret = stringvar_from_substr(txt->ft_buf,
                                    txt->ft_bufpos, haveidx);
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
text_write(struct textfile_t *txt, Object *so)
{
        /*
         * FIXME: The whole point is to use a buffer to reduce
         * the system write() calls.
         */
        const void *data;
        ssize_t size, nwritten;
        Object *arg, *result;

        switch (txt->ft_codec) {
        case CODEC_ASCII:
                if (!string_isascii(so)) {
                        err_setstr(ValueError,
                          "Cannot write non-ascii string to ascii file");
                        return -1;
                }
                data = string_cstring(so);
                size = string_nbytes(so);
                break;

        case CODEC_UTF8:
                data = string_cstring(so);
                size = string_nbytes(so);
                break;

        case CODEC_LATIN1:
                if (string_width(so) > 1) {
                        err_setstr(ValueError,
                                "Unicode points too big for Latin1");
                        return -1;
                }
                data = string_data(so);
                size = seqvar_size(so);
                break;

        default:
                bug();
                return -1;
        }
        arg = bytesvar_new(data, size);
        result = file_call_1arg(txt->ft_raw, STRCONST_ID(write), arg);
        VAR_DECR_REF(arg);
        if (result == ErrorVar)
                return -1;
        nwritten = intvar_toll(result);
        VAR_DECR_REF(result);
        return nwritten;
}

static Object *
do_text_tell(Frame *fr)
{
        /* TODO: Need byte cookies to make this work. */
        err_setstr(NotImplementedError,
                   "tell not implemented yet for this file type");
        return ErrorVar;
}

static Object *
do_text_seek(Frame *fr)
{
        /* TODO: Need byte cookies to make this work. */
        err_setstr(NotImplementedError,
                   "seek not implemented yet for this file type");
        return ErrorVar;
}

static Object *
do_text_write(Frame *fr)
{
        ssize_t ret;
        Object *so, *fo;
        struct textfile_t *txt;

        if (vm_getargs(fr, "<*>[<s>!]{!}:write", &fo, &so) == RES_ERROR)
                return ErrorVar;
        bug_on(!fo || fo->v_type != &TextFileType);

        txt = (struct textfile_t *)fo;
        if (TEXTFILE_CLOSED(txt)) {
                filerr_closed("write");
                return ErrorVar;
        }
        if (!TEXTFILE_WRITABLE(txt)) {
                filerr_permit("write", 1);
                return ErrorVar;
        }
        ret = text_write(txt, so);
        if (ret < 0)
                return ErrorVar;
        return intvar_new(ret);
}

static Object *
do_text_flush(Frame *fr)
{
        Object *self;
        if (vm_getargs(fr, "<*>[!]{!}:flush", &self) == RES_ERROR)
                return ErrorVar;
        /* see fixme in text_write */
        return NULL;
}

static Object *
do_text_fileno(Frame *fr)
{
        Object *fo;
        struct textfile_t *txt;
        int fd;

        if (vm_getargs(fr, "<*>[!]{!}:fileno", &fo) == RES_ERROR)
                return ErrorVar;
        bug_on(!fo || fo->v_type != &TextFileType);

        txt = (struct textfile_t *)fo;
        fd = TEXTFILE_FILENO(txt);
        if (fd < 0) {
                filerr_closed("fileno");
                return ErrorVar;
        }
        return intvar_new(fd);
}

static Object *
do_text_close(Frame *fr)
{
        struct textfile_t *txt;
        Object *fo;

        if (vm_getargs(fr, "<*>[!]{!}:close", &fo) == RES_ERROR)
                return ErrorVar;
        bug_on(!fo || fo->v_type != &TextFileType);

        txt = (struct textfile_t *)fo;
        return file_call_noarg(txt->ft_raw, STRCONST_ID(close));
}

static Object *
text_str(Object *fo)
{
        struct textfile_t *txt;
        struct rawfile_t *raw;
        char codecbuf[16];

        bug_on(!fo || fo->v_type != &TextFileType);

        txt = (struct textfile_t *)fo;
        raw = (struct rawfile_t *)txt->ft_raw;
        if (!raw)
                return VAR_NEW_REF(NullVar);

        codec_str(txt->ft_codec, codecbuf, sizeof(codecbuf));
        return stringvar_from_format("<file name='%s' mode='%s' enc='%s'>",
                                     raw->fr_name ? raw->fr_name : "!",
                                     raw->fr_mode, codecbuf);
}

static void
textfile_reset(Object *textfile)
{
        struct textfile_t *txt = (struct textfile_t *)textfile;
        if (txt) {
                Object *o;

                if (txt->ft_raw)
                        VAR_DECR_REF(txt->ft_raw);

                o = txt->ft_buf;
                txt->ft_buf = NULL;
                if (o)
                        VAR_DECR_REF(o);

                o = txt->ft_eol;
                txt->ft_eol = NULL;
                if (o)
                        VAR_DECR_REF(o);
        }
}

static Object *
open_text(int fd, struct fileconfig_t *cfg, Object *raw, int codec)
{
        struct textfile_t *txt;
        Object *instance;

        instance = var_new(&TextFileType);
        txt = (struct textfile_t *)instance;

        txt->ft_codec = codec;
        /* FIXME: this should be an argument */
        txt->ft_eol = stringvar_from_ascii("\n");
        txt->ft_raw = raw;
        txt->ft_eof = false;
        txt->ft_fdpos = 0;
        return instance;
}

/* **********************************************************************
 *              Module-level code and API
 ***********************************************************************/

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
        Object *res;
        res = file_call_noarg(fo, STRCONST_ID(read));
        if (!res) {
                filerr("internal_read",
                       "object is not a file or has no 'read' method");
                return ErrorVar;
        }
        return res;
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
        Object *res;
        ssize_t ret;

        res = file_call_1arg(fo, STRCONST_ID(write), data);
        if (!res) {
                filerr("internal_write",
                       "object is not a file or has no 'write' method");
                return -1;
        }
        if (res == ErrorVar)
                return -1;
        if (!isvar_int(res)) {
                err_setstr(RuntimeError,
                        "'%N' returned %s but an integer was expected",
                        STRCONST_ID(write), typestr(res));
                VAR_DECR_REF(res);
                return -1;
        }
        ret = (ssize_t)intvar_toll(res);
        VAR_DECR_REF(res);
        return ret;
}

/**
 * isvar_file - Return true if @o is a file
 */
bool
isvar_file(Object *o)
{
        return var_instanceof(o, gbl_borrow_builtin_class(GBL_CLASS_IOBASE));
}

/*
 * Common to user-code open() and C-code evc_file_open().
 */
static Object *
finish_open(int fd, struct fileconfig_t *cfg, int codec)
{
        Object *raw = open_raw(fd, cfg);
        switch (cfg->type) {
        case FILE_TEXT:
                return open_text(fd, cfg, raw, codec);
        case FILE_BINARY:
                return open_binary(fd, cfg, raw);
        case FILE_RAW:
                return raw;
        default:
                bug();
                return ErrorVar;
        }
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
 *
 * TODO: Add option to limit rw privileges.  stdin, eg. appears as 'w+'
 * because its underlying file is a terminal with read-write permissions.
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

        ret = finish_open(fd, &cfg, codec);
        if (ret == ErrorVar) {
                if (cfg.name)
                        efree(cfg.name);
                efree(cfg.mode);
        }

        return ret;
}

static Object *
do_open(Frame *fr)
{
        const char *mode, *s;
        Object *namearg, *encarg = NULL;
        struct fileconfig_t cfg;
        enum result_t res;
        bool binary = false;
        bool have_rw = false;
        bool have_plus = false;
        int buffering = true;
        int closefd = true;
        int codec = CODEC_OPEN_DEFAULT;
        int flags = 0;
        int fd;
        Object *ret;

        res = vm_getargs(fr, "[<si>s!]{|<s>ii}:open", &namearg, &mode,
                         STRCONST_ID(encoding), &encarg,
                         STRCONST_ID(closefd), &closefd,
                         STRCONST_ID(buffering), &buffering);
        if (res == RES_ERROR)
                return ErrorVar;

        if (encarg) {
                Object *codec_dict = gbl_borrow_mns_dict(MNS_CODEC);
                bug_on(!codec_dict);
                res = vm_getargs_sv(codec_dict, "{i}", encarg, &codec);
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
                err_setstr(ValueError,
                           "cannot use encoding in binary mode");
                return ErrorVar;
        }

        /* TODO: parse codec */
        if (binary) {
                cfg.type = buffering ? FILE_BINARY : FILE_RAW;
        } else if (!buffering) {
                err_setstr(ValueError,
                           "Cannot open in text mode without buffering");
                return ErrorVar;
        } else {
                cfg.type = FILE_TEXT;
        }
        cfg.mode = estrdup(mode);
        cfg.name = NULL;

        if (isvar_string(namearg)) {
                const char *name = string_cstring(namearg);
                if (strlen(name) != string_nbytes(namearg)) {
                        err_setstr(ArgumentError, "embedded null char");
                        goto err;
                }
                cfg.name = estrdup(name);
                fd = open(cfg.name, flags, 0666);
                if (fd < 0) {
                        err_errno("cannot open %s", cfg.name);
                        goto err;
                }
        } else {
                long long ival;
                int oflags;
                bug_on(!isvar_int(namearg));
                ival = intvar_toll(namearg);
                if (ival < 0LL || ival > INT_MAX) {
                        err_setstr(ArgumentError, "fd out of range");
                        goto err;
                }
                fd = (int)ival;
                oflags = fcntl(fd, F_GETFL);
                if (oflags < 0) {
                        err_setstr(ArgumentError, "invalid fd");
                        goto err;
                }
                oflags &= O_RDWR | O_RDONLY | O_WRONLY;
                if (flags == O_RDONLY && cfg.writable) {
                        err_setstr(ArgumentError,
                                "invalid mode: file is not writable");
                        goto err;
                }
                if (flags == O_WRONLY && cfg.readable) {
                        err_setstr(ArgumentError,
                                "invalid mode: file is not readable");
                        goto err;
                }
        }

        ret = finish_open(fd, &cfg, codec);
        if (ret == ErrorVar)
                goto err;

        return ret;

err:
        if (cfg.name)
                efree(cfg.name);
        efree(cfg.mode);
        return ErrorVar;
}

static const struct type_method_t io_inittbl[] = {
        {"open", do_open},
        {NULL, NULL},
};

static Object *
create_io_module(Frame *fr)
{
        Object *dict = dictvar_from_methods(NULL, io_inittbl, false);
        Object *k, *v;

        k = stringvar_new("SEEK_CUR");
        v = intvar_new(SEEK_CUR);
        dict_setitem(dict, k, v);
        VAR_DECR_REF(k);
        VAR_DECR_REF(v);

        k = stringvar_new("SEEK_SET");
        v = intvar_new(SEEK_SET);
        dict_setitem(dict, k, v);
        VAR_DECR_REF(k);
        VAR_DECR_REF(v);

        /* TODO: Add SEEK_END when I can support it */
        return dict;
}

static Object *
iobase_placeholder(Frame *fr)
{
        err_setstr(NotImplementedError, "method not implemented for this file");
        return ErrorVar;
}

static Object *
initialize_one_file_class(Object *base, const struct type_method_t *tbl)
{
        Object *class, *methods;
        methods = dictvar_from_methods(NULL, tbl, true);
        class = typevar_new_intl(base, methods, NULL);
        VAR_DECR_REF(methods);
        return class;
}

static void
initialize_file_classes(void)
{
        static const struct type_method_t iobase_methods[] = {
                {"read", iobase_placeholder},
                {"write", iobase_placeholder},
                {"close", iobase_placeholder},
                {NULL, NULL},
        };
        Object *base, *basetup;

        base = initialize_one_file_class(NULL, iobase_methods);
        gbl_set_builtin_class(GBL_CLASS_IOBASE, base);
        VAR_DECR_REF(base);

        basetup = tuplevar_from_stack(&base, 1, false);
        BinFileType.bases = VAR_NEW_REF(basetup);
        RawFileType.bases = VAR_NEW_REF(basetup);
        TextFileType.bases = VAR_NEW_REF(basetup);
        VAR_DECR_REF(basetup);
}

void
moduleinit_io(void)
{
        Object *k, *o;

        k = stringvar_from_ascii("_io");
        o = var_from_format("<xmM>", create_io_module, 0, 0);
        dict_setitem(GlobalObject, k, o);
        VAR_DECR_REF(k);
        VAR_DECR_REF(o);

        o = var_from_format("<xmMk>", do_open, 3, 3, 2);
        k = stringvar_from_ascii("open");
        vm_add_global(k, o);
        VAR_DECR_REF(k);
        VAR_DECR_REF(o);

        if (!io_classes_initialized())
                initialize_file_classes();
}

static const struct type_method_t textfile_methods[] = {
        {"read",       do_text_read},
        {"readline",   do_text_readline},
        {"flush",      do_text_flush},
        {"write",      do_text_write},
        {"seek",       do_text_seek},
        {"tell",       do_text_tell},
        {"close",      do_text_close},
        {"fileno",     do_text_fileno},
        /*
         * TODO: need seek/tell, it's a lot more complicated
         * than with binary.
         */
        {NULL, NULL},
};

static const struct type_method_t binfile_methods[] = {
        {"read",       do_bin_read},
        {"write",      do_bin_write},
        {"flush",      do_bin_flush},
        {"tell",       do_bin_tell},
        {"seek",       do_bin_seek},
        {"close",      do_bin_close},
        {"fileno",     do_bin_fileno},
        {NULL, NULL},
};

static const struct type_method_t rawfile_methods[] = {
        {"read",       do_raw_read},
        {"write",      do_raw_write},
        {"close",      do_raw_close},
        {"tell",       do_raw_tell},
        {"seek",       do_raw_seek},
        {"fileno",     do_raw_fileno},
        {NULL, NULL},
};

struct type_t BinFileType = {
        .name = "binfile",
        .cbm = binfile_methods,
        .reset = binfile_reset,
        .size = sizeof(struct binfile_t),
        .str = bin_str,
};

struct type_t RawFileType = {
        .name = "rawfile",
        .cbm = rawfile_methods,
        .reset = rawfile_reset,
        .size = sizeof(struct rawfile_t),
        .str = raw_str,
};

struct type_t TextFileType = {
        .name = "textfile",
        .cbm = textfile_methods,
        .reset = textfile_reset,
        .size = sizeof(struct textfile_t),
        .str = text_str,
};

