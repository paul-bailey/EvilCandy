/* builtin/file.c - Built-in methods for QFILE_MAGIC type */
#include "builtin.h"
#include <errno.h>

/*
 * eof()        (no args)
 *
 * Return integer, 1 if file is at eof, 0 if not
 */
static void
do_eof(struct var_t *ret)
{
        struct var_t *self = get_this();
        bug_on(self->magic != QFILE_MAGIC);
        qop_assign_int(ret, !!feof(self->fp->fp));
}

/*
 * clearerr()   (no args)
 *
 * No return value.  Clears error flags and file's errno.
 */
static void
do_clearerr(struct var_t *ret)
{
        struct var_t *self = get_this();
        bug_on(self->magic != QFILE_MAGIC);
        self->fp->err = 0;
        clearerr(self->fp->fp);
}

/*
 * errno()   (no args)
 *
 * Return integer, errno of last error on file
 */
static void
do_errno(struct var_t *ret)
{
        struct var_t *self = get_this();
        bug_on(self->magic != QFILE_MAGIC);
        qop_assign_int(ret, (long long)self->fp->err);
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
        FILE *fp;
        int c, errno_save = errno;
        struct var_t *self = get_this();
        bug_on(self->magic != QFILE_MAGIC);
        fp = self->fp->fp;

        errno = 0;
        qop_assign_cstring(ret, "");
        while ((c = getc(fp)) != '\n' && c != EOF)
                buffer_putc(&ret->s, c);
        if (errno)
                self->fp->err = errno;
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
        struct var_t *self = get_this();
        struct var_t *vs = getarg(0);
        int res = 0;
        int errno_save = errno;
        bug_on(self->magic != QFILE_MAGIC);
        bug_on(vs == NULL);

        arg_type_check(vs, QSTRING_MAGIC);
        if (!vs->s.s)
                goto done;

        errno = 0;
        fp = self->fp->fp;
        for (s = vs->s.s; *s != '\0'; s++) {
                if (putc((int)*s, fp) == EOF) {
                        res = -1;
                        break;
                }
        }
done:
        if (errno)
                self->fp->err = errno;
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
        int errno_save = errno;
        long off;
        struct var_t *self = get_this();
        bug_on(self->magic != QFILE_MAGIC);
        errno = 0;
        off = ftell(self->fp->fp);
        if (errno)
                self->fp->err = errno;
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
        struct var_t *self = get_this();
        bug_on(self->magic != QFILE_MAGIC);
        errno = 0;
        rewind(self->fp->fp);
        if (errno)
                self->fp->err = errno;
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

void
bi_moduleinit_file__(void)
{
        bi_init_type_methods__(file_methods, QFILE_MAGIC);
}
