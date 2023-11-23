/* builtin/io.c - Implementation of __gbl__.io */
#include "builtin.h"
#include <errno.h>
#include <string.h>

/*
 * Io.open(name, mode)          name and mode are strings
 *
 * Return:
 *      string          if error (errno descr. stored in string)
 *      file            if success
 *
 * Check with typeof to determine success or failure
 */
static void
do_open(struct var_t *ret)
{
        struct var_t *vname = getarg(0);
        struct var_t *vmode = getarg(0);
        int errno_save = errno;

        arg_type_check(vname, QSTRING_MAGIC);
        arg_type_check(vmode, QSTRING_MAGIC);
        if (vname->s.s == NULL || vmode->s.s == NULL) {
                errno = EINVAL;
                goto bad;
        }

        if (file_new(ret, vname->s.s, vmode->s.s) == NULL)
                goto bad;
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

