/*
 * builtin/io.c - Implementation of the __gbl__.Io built-in object
 *
 * f = Io.open(name, mode) returns a file handle, an object with
 * methods documented in the file below.  This is sort of like a fil
 */
#include "builtin.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/**
 * struct file_handle_t - Handle to a file, private data to an
 *                        object_handle_t struct in the case of files
 * @magic:      Way to guarantee
 * @nref:       Number of variables with access to this same open file
 * @fp:         File pointer
 * @err:        errno value that was set on last error
 */
struct file_handle_t {
        enum { FILE_HANDLE_MAGIC = 0x8716245 } magic;
        int nref;
        FILE *fp;
        int err;
};

static struct file_handle_t *
getfh(void)
{
        struct var_t *self = get_this();
        struct file_handle_t *fh;
        bug_on(self->magic != QOBJECT_MAGIC);
        fh = self->o.h->priv;
        bug_on(fh->magic != FILE_HANDLE_MAGIC);
        return fh;
}

/*
 * eof()        (no args)
 *
 * Return integer, 1 if file is at eof, 0 if not
 */
static void
do_eof(struct var_t *ret)
{
        struct file_handle_t *fh = getfh();
        qop_assign_int(ret, !!feof(fh->fp));
}

/*
 * clearerr()   (no args)
 *
 * No return value.  Clears error flags and file's errno.
 */
static void
do_clearerr(struct var_t *ret)
{
        struct file_handle_t *fh = getfh();
        fh->err = 0;
        clearerr(fh->fp);
}

/*
 * errno()   (no args)
 *
 * Return integer, errno of last error on file
 */
static void
do_errno(struct var_t *ret)
{
        struct file_handle_t *fh = getfh();
        qop_assign_int(ret, (long long)fh->err);
}

/*
 * readstr()    (no args)
 *
 * Return string to next newline.  The newline itself will not be
 * placed.  We never heard of '\r', it must be from Canada.
 *
 * If eof, string will be ""; empty lines can be distinguished from
 * EOF with the eof() built-in method.
 */
static void
do_readstr(struct var_t *ret)
{
        int c, errno_save = errno;
        struct file_handle_t *fh = getfh();
        FILE *fp = fh->fp;

        errno = 0;
        qop_assign_cstring(ret, "");
        while ((c = getc(fp)) != '\n' && c != EOF)
                buffer_putc(&ret->s, c);
        if (errno)
                fh->err = errno;
        errno = errno_save;
}

/*
 * writestr(str)        str is a string type
 *
 * Return integer, 0 if success, -1 if failure
 *
 * This will write all of str, including any newline found.
 */
static void
do_writestr(struct var_t *ret)
{
        FILE *fp;
        char *s;
        struct file_handle_t *fh = getfh();
        struct var_t *vs = getarg(0);
        int res = 0;
        int errno_save = errno;

        bug_on(vs == NULL);

        arg_type_check(vs, QSTRING_MAGIC);
        if (!vs->s.s)
                goto done;

        errno = 0;
        fp = fh->fp;
        for (s = vs->s.s; *s != '\0'; s++) {
                if (putc((int)*s, fp) == EOF) {
                        res = -1;
                        break;
                }
        }
done:
        if (errno)
                fh->err = errno;
        errno = errno_save;
        qop_assign_int(ret, res);
}

/*
 * tell()       (no args)
 *
 * Return integer, offset of file or -1 if error, possibly
 * set the file's errno
 */
static void
do_tell(struct var_t *ret)
{
        struct file_handle_t *fh = getfh();
        int errno_save = errno;
        long off;

        errno = 0;
        off = ftell(fh->fp);
        if (errno)
                fh->err = errno;
        errno = errno_save;
        qop_assign_int(ret, off);
}

/*
 * rewind()     (no args)
 *
 * Return nothing, rewind the file, possibly set file's errno
 */
static void
do_rewind(struct var_t *ret)
{
        int errno_save = errno;
        struct file_handle_t *fh = getfh();
        errno = 0;
        rewind(fh->fp);
        if (errno)
                fh->err = errno;
        errno = errno_save;
}

static const struct inittbl_t file_methods[] = {
        TOFTBL("eof",      do_eof, 0, 0),
        TOFTBL("clearerr", do_clearerr, 0, 0),
        TOFTBL("errno",    do_errno, 0, 0),
        TOFTBL("readstr",  do_readstr, 0, 0),
        TOFTBL("writestr", do_writestr, 1, 1),
        TOFTBL("tell",     do_tell, 0, 0),
        TOFTBL("rewind",   do_rewind, 0, 0),
        TBLEND,
};

static void
file_reset(struct object_handle_t *oh, void *data)
{
        struct file_handle_t *fh = data;
        bug_on(!fh);
        bug_on(fh->magic != FILE_HANDLE_MAGIC);
        fclose(fh->fp);
        free(fh);
}

/**
 * file_new - Open a file and create a QOBJECT_MAGIC-type handle
 * @v:          empty struct var_t to store the result
 * @path:       Path of the file to open, relative to the current working
 *              directory--the irl one, *not* the one relative to the
 *              script being executed (ie. we don't do the path
 *              translation that load_file() does).  Path may also be
 *              absolute.
 * @flags:      Same as "mode" arg to fopen (3)
 *
 * Return: @v if success, NULL if not
 */
static struct file_handle_t *
file_new(const char *path, const char *mode)
{
        struct file_handle_t *h;
        FILE *fp = fopen(path, mode);
        if (!fp)
                return NULL;

        h = emalloc(sizeof(*h));
        h->magic = FILE_HANDLE_MAGIC;
        h->fp = fp;
        h->err = 0;
        return h;
}

/*
 * Io.open(name, mode)          name and mode are strings
 *
 * Return:
 *      string          if error (errno descr. stored in string)
 *      object          if success
 *
 * Check with typeof to determine success or failure
 */
static void
do_open(struct var_t *ret)
{
        struct var_t *vname = getarg(0);
        struct var_t *vmode = getarg(1);
        struct file_handle_t *fh;
        int errno_save = errno;

        arg_type_check(vname, QSTRING_MAGIC);
        arg_type_check(vmode, QSTRING_MAGIC);
        if (vname->s.s == NULL || vmode->s.s == NULL) {
                errno = EINVAL;
                goto bad;
        }

        if ((fh = file_new(vname->s.s, vmode->s.s)) == NULL)
                goto bad;

        object_from_empty(ret);
        ret->o.h->priv = fh;
        ret->o.h->priv_cleanup = file_reset;
        bi_build_internal_object__(ret, file_methods);
        return;

bad:
        qop_assign_cstring(ret, "");
        buffer_puts(&ret->s, strerror(errno));
        errno = errno_save;
}

const struct inittbl_t bi_io_inittbl__[] = {
        TOFTBL("open", do_open, 2, 2),
        TBLEND,
};

