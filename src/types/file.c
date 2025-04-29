/*
 * file.c - Definition for the file class of objects
 */
#include <evilcandy.h>
#include <unistd.h>
#include <errno.h>

#define V2F(v_) ((struct filevar_t *)(v_))

struct filevar_t {
        Object base;
        FILE *f_fp;
        Object *f_name;
        unsigned int f_mode;
        bool f_binary;
        bool f_eof;
};

static int
file_cmp(Object *a, Object *b)
{
        FILE *fpa, *fpb;
        bug_on(!isvar_file(a) || !isvar_file(b));
        fpa = V2F(a)->f_fp;
        fpb = V2F(b)->f_fp;
        return OP_CMP((uintptr_t)fpa, (uintptr_t)fpb);
}

static bool
file_cmpz(Object *v)
{
        return 0;
}

static void
file_reset(Object *v)
{
        struct filevar_t *f = V2F(v);
        bug_on(!isvar_file(v));
        if (f->f_fp)
                fclose(f->f_fp);
        VAR_DECR_REF(f->f_name);
}

static Object *
file_str(Object *v)
{
        struct buffer_t b;
        struct filevar_t *f = V2F(v);
        bug_on(!isvar_file(v));

        buffer_init(&b);
        buffer_printf(&b, "<%s file at %s>",
                     f->f_fp ? "open" : "closed",
                     string_get_cstring(f->f_name));
        return stringvar_from_buffer(&b);
}

#define RETURN_IF_BAD_FILE(f_) do {                       \
        if (arg_type_check(f_, &FileType) == RES_ERROR)   \
                return ErrorVar;                          \
        if (!V2F(f_)->f_fp) {                             \
                err_setstr(RuntimeError, "File closed");  \
                return ErrorVar;                          \
        }                                                 \
} while (0)

static Object *
do_close(Frame *fr)
{
        Object *self;
        struct filevar_t *f;

        self = vm_get_this(fr);
        RETURN_IF_BAD_FILE(self);
        f = V2F(self);

        if (!f->f_binary)
                fflush(f->f_fp);
        fclose(f->f_fp);
        f->f_fp = NULL;
        f->f_eof = false;
        f->f_binary = false;
        return NULL;
}

static Object *
do_clearerr(Frame *fr)
{
        Object *self;
        struct filevar_t *f;

        self = vm_get_this(fr);
        RETURN_IF_BAD_FILE(self);
        f = V2F(self);

        clearerr(f->f_fp);
        if (!feof(f->f_fp))
                f->f_eof = false;
        return NULL;
}

static Object *
do_read(Frame *fr)
{
        Object *self;
        Object *len;
        struct filevar_t *f;

        self = vm_get_this(fr);
        RETURN_IF_BAD_FILE(self);
        f = V2F(self);
        if (!(f->f_mode & FMODE_READ)) {
                err_setstr(RuntimeError, "You may not read in this mode");
                return ErrorVar;
        }
        if (f->f_eof)
                return NULL;

        len = vm_get_arg(fr, 0);

        if (f->f_binary) {
                long long len_i;
                Object *ret;
                unsigned char *buf, *pbuf;
                if (!len) {
                        err_setstr(ArgumentError, "Expected: length");
                        return ErrorVar;
                }
                if (!isvar_int(len)) {
                        err_setstr(TypeError, "Read length must be an integer");
                        return ErrorVar;
                }
                len_i = intvar_toll(len);
                if (len_i < 0 || len_i > INT_MAX) {
                        err_setstr(ValueError, "Invalid read length");
                        return ErrorVar;
                }

                buf = emalloc(len_i);
                pbuf = buf;
                while (len_i) {
                        ssize_t nread = read(fileno(f->f_fp), pbuf, len_i);
                        if (nread <= 0) {
                                if (errno || !feof(f->f_fp)) {
                                        err_errno("Read failed");
                                        ret = ErrorVar;
                                } else {
                                        /* EOF */
                                        ret = NULL;
                                }
                                f->f_eof = true;
                                efree(buf);
                                break;
                        }
                        len_i -= nread;
                        pbuf += nread;

                }
                if (!len_i)
                        ret = bytesvar_nocopy(buf, len_i);
                return ret;
        } else {
                /* TODO: warn ignored if length */
                struct buffer_t b;
                int c;
                buffer_init(&b);
                while ((c = fgetc(f->f_fp)) != EOF) {
                        buffer_putc(&b, c);
                        /* TODO: locale for translating this */
                        if (c == '\n')
                                break;
                }
                if (c == EOF) {
                        f->f_eof = true;
                        if (buffer_size(&b) == 0) {
                                buffer_free(&b);
                                return NULL;
                        }
                        /*
                         * TODO: straggling bytes but EOF before EOL,
                         * should we be treating this like it's OK?
                         */
                }
                return stringvar_from_buffer(&b);
        }
}

static Object *
do_write(Frame *fr)
{
        Object *self;
        Object *data;
        struct filevar_t *f;
        const unsigned char *s;
        size_t size;

        self = vm_get_this(fr);
        RETURN_IF_BAD_FILE(self);
        f = V2F(self);

        if (!(f->f_mode & FMODE_WRITE)) {
                err_setstr(RuntimeError, "You may not write in this mode");
                return ErrorVar;
        }
        data = vm_get_arg(fr, 0);
        if (!data) {
                err_setstr(ArgumentError, "Expected: data to write");
                return ErrorVar;
        }

        if (!isvar_seq(data))
                goto etype;

        if (f->f_binary && !isvar_bytes(data))
                goto etype;

        if (isvar_bytes(data)) {
                s = bytes_getbuf(data);
                size = seqvar_size(data);
                fwrite(data, 1, size, f->f_fp);
        } else if (isvar_string(data)) {
                s = (unsigned char *)string_get_cstring(data);
                size = strlen((char *)s);
        } else {
                goto etype;
        }

        if (f->f_binary) {
                while (size) {
                        ssize_t nwritten = write(fileno(f->f_fp), s, size);
                        if (nwritten <= 0) {
                                /* XXX error should be if < 0 */
                                f->f_eof = true;
                                err_errno("Write failed");
                                return ErrorVar;
                        }
                        size -= nwritten;
                        s += nwritten;
                }
        } else {
                /* TODO: clear definition what to do about eol */
                /* TODO: error handling, encoding, etc. */
                /*
                 * putc instead of puts because I'm not sure it's standard
                 * what happens if EOL before '\0'
                 */
                int c;
                while ((c = *s++) != '\0')
                        fputc(c, f->f_fp);
                fflush(f->f_fp);
        }
        return NULL;

etype:
        err_setstr(TypeError, "Cannot write '%s' type to %s stream",
                   typestr(data), f->f_binary ? "binary" : "text");
        return ErrorVar;
}

static Object *
file_geteof(Object *file)
{
        struct filevar_t *f = V2F(file);
        int res;

        bug_on(!isvar_file(file));
        res = f->f_eof || !f->f_fp || (f->f_fp && feof(f->f_fp));
        return intvar_new(res);
}

static Object *
file_getclosed(Object *file)
{
        struct filevar_t *f = V2F(file);
        int res;

        bug_on(!isvar_file(file));
        res = f->f_fp == NULL;
        return intvar_new(res);
}

static const struct type_prop_t file_prop_getsets[] = {
        { .name = "eof", .getprop = file_geteof, .setprop = NULL },
        { .name = "closed", .getprop = file_getclosed, .setprop = NULL },
        { .name = NULL },
};

static const struct type_inittbl_t file_cb_methods[] = {
        V_INITTBL("clearerr",   do_clearerr,    0, 0),
        V_INITTBL("read",       do_read,        0, 1),
        V_INITTBL("write",      do_write,       1, 1),
        V_INITTBL("close",      do_close,       0, 0),
        TBLEND,
};

struct type_t FileType = {
        .name   = "file",
        .opm    = NULL,
        .cbm    = file_cb_methods,
        .mpm    = NULL,
        .sqm    = NULL,
        .size   = sizeof(struct filevar_t),
        .str    = file_str,
        .cmp    = file_cmp,
        .cmpz   = file_cmpz,
        .reset  = file_reset,
        .prop_getsets = file_prop_getsets,
};

Object *
filevar_new(FILE *fp, Object *name, unsigned int mode)
{
        Object *v = var_new(&FileType);
        struct filevar_t *f = V2F(v);
        bug_on(!isvar_string(name));
        VAR_INCR_REF(name);
        f->f_fp = fp;
        f->f_name = name;
        f->f_binary = !!(mode & FMODE_BINARY);
        f->f_eof = false;
        f->f_mode = mode;
        return v;
}

