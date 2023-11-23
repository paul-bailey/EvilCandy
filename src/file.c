/*
 * file.c - code specific to QFILE_MAGIC variables
 *
 * There is no file_from_empty function, since a file cannot be declared
 * in a script except as a return call from Io.open.
 */
#include "egq.h"
/* FIXME: This is for Apple, but I recall it's different for Linux */
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

/* only called from file_reset() */
void
file_reset(struct var_t *v)
{
        struct file_handle_t *h = v->fp;
        bug_on(v->magic != QFILE_MAGIC);
        h->nref--;
        if (h->nref <= 0) {
                bug_on(h->nref < 0);
                close(h->fd);
                free(h);
        }
        v->fp = NULL;
}

struct var_t *
file_new(const char *path, unsigned int flags)
{
        struct var_t *v;
        struct file_handle_t *h;
        int fd;

        if (!!(flags & O_CREAT))
                fd = open(path, flags, 0666);
        else
                fd = open(path, flags);

        /*
         * If fail, don't reset errno.  Calling code is builtin method
         * which returns a failure string to user if this happens.
         */
        if (fd == -1)
                return NULL;

        v = var_new();
        h = emalloc(sizeof(*h));
        h->fd = fd;
        h->flags = flags;
        h->nref = 1;
        v->fp = h;
        return v;
}

