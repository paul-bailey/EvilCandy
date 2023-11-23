/*
 * file.c - code specific to QFILE_MAGIC variables
 *
 * There is no file_from_empty function, since a file cannot be declared
 * in a script except as a return call from Io.open.
 */
#include "egq.h"
#include <stdlib.h>

/**
 * file_handle_decrement - "fh->nref--" and all the free-ey stuff if nref
 *                         is now zero
 * @fh: Handle to decrement, attached to either a QFILE_MAGIC- or
 *      QOBJECT_MAGIC-type struct var_t
 */
void
file_handle_decrement(struct file_handle_t *fh)
{
        fh->nref--;
        if (fh->nref <= 0) {
                bug_on(fh->nref < 0);
                fclose(fh->fp);
                free(fh);
        }
}

/* only called from file_reset() */
void
file_reset(struct var_t *v)
{
        struct file_handle_t *h = v->fp;
        bug_on(v->magic != QFILE_MAGIC);
        file_handle_decrement(h);
        v->fp = NULL;
}

/**
 * file_new - Open a file and create a QFILE_MAGIC-type handle
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
struct var_t *
file_new(struct var_t *v, const char *path, const char *mode)
{
        struct file_handle_t *h;
        FILE *fp;

        bug_on(v->magic != QEMPTY_MAGIC);

        fp = fopen(path, mode);
        if (!fp)
                return NULL;

        v->magic = QFILE_MAGIC;
        h = emalloc(sizeof(*h));
        h->fp = fp;
        h->fd = fileno(fp);
        h->err = 0;
        h->nref = 1;
        v->fp = h;
        return v;
}

