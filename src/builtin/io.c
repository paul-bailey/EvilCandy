/*
 * builtin/io.c - Implementation of the __gbl__.Io built-in object
 *
 * f = Io.open(name, mode)
 * If fail,
 *      return a string describing the failure
 *
 * If success,
 *      return a file handle, an object with the following
 *      methods:
 *
 * f.readline()    Read a line from f to the next '\n' and return
 *                 it as a string, or as "" if f is at EOF.
 * f.writeline(txt)
 *                 Write txt to f.  Do not interpolate characters
 *                 or add a newline at the end.
 * f.eof()         Return 1 if f is at EOF, 0 if not
 * f.clearerr()    Clear error flags in f
 * f.errno()       Get the last error number pertaining to f
 * f.tell()        Return the current offset into f
 * f.rewind()      Return to the start of the file
 *
 * TODO: Binary file operations, need better array class than list.
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
        bug_on(self->magic != TYPE_DICT);
        fh = object_get_priv(self);
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
        integer_init(ret, !!feof(fh->fp));
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
        integer_init(ret, (long long)fh->err);
}

/*
 * readline()    (no args)
 *
 * Return string to next newline.  The newline itself will not be
 * placed.  We never heard of '\r', it must be from Canada.
 *
 * If eof, string will be ""; empty lines can be distinguished from
 * EOF with the eof() built-in method.
 */
static void
do_readline(struct var_t *ret)
{
        int errno_save = errno;
        struct file_handle_t *fh = getfh();
        FILE *fp = fh->fp;

        bug_on(ret->magic != TYPE_EMPTY);

        errno = 0;
        string_init_from_file(ret, fp, '\n', false);
        if (errno)
                fh->err = errno;
        errno = errno_save;
}

/*
 * writeline(str)        str is a string type
 *
 * Return integer, 0 if success, -1 if failure
 *
 * This will write all of str, including any newline found.
 * It will not add an additional newline.
 */
static void
do_writeline(struct var_t *ret)
{
        FILE *fp;
        char *s;
        struct file_handle_t *fh = getfh();
        struct var_t *vs = frame_get_arg(0);
        int res = 0;
        int errno_save = errno;

        bug_on(vs == NULL);

        arg_type_check(vs, TYPE_STRING);
        s = string_get_cstring(vs);
        if (!s)
                goto done;

        errno = 0;
        fp = fh->fp;
        for ( ; *s != '\0'; s++) {
                if (putc((int)*s, fp) == EOF) {
                        res = -1;
                        break;
                }
        }
        fflush(fp);
done:
        if (errno)
                fh->err = errno;
        errno = errno_save;
        integer_init(ret, res);
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
        integer_init(ret, off);
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
        TOFTBL("readline",  do_readline, 0, 0),
        TOFTBL("writeline", do_writeline, 1, 1),
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
 * file_new - Open a file and create a TYPE_DICT-type handle
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
        struct var_t *vname = frame_get_arg(0);
        struct var_t *vmode = frame_get_arg(1);
        char *name, *mode;
        struct file_handle_t *fh;
        int errno_save = errno;

        arg_type_check(vname, TYPE_STRING);
        arg_type_check(vmode, TYPE_STRING);
        name = string_get_cstring(vname);
        mode = string_get_cstring(vmode);
        if (name == NULL || mode == NULL) {
                errno = EINVAL;
                goto bad;
        }

        if ((fh = file_new(name, mode)) == NULL)
                goto bad;

        object_init(ret);
        object_set_priv(ret, fh, file_reset);
        bi_build_internal_object__(ret, file_methods);
        return;

bad:
        string_init(ret, strerror(errno));
        errno = errno_save;
}

const struct inittbl_t bi_io_inittbl__[] = {
        TOFTBL("open", do_open, 2, 2),
        TBLEND,
};

