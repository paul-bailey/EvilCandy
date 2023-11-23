#include "builtin.h"

static void
do_open(struct var_t *ret)
{
        syntax("Io.open not yet supported");
}

const struct inittbl_t bi_io_inittbl__[] = {
        TOFTBL("open", do_open, 2, 2),
        TBLEND,
};
