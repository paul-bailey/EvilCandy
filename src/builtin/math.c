/*
 * builtin/math.c - Implementation of the __gbl__.Math built-in object
 */
#include "builtin.h"
#include <typedefs.h>
#include <math.h>

/* XXX easier to just return NAN for bad? */
static double
get_floatarg(struct vmframe_t *fr, int argno, int *status)
{
        struct var_t *x = frame_get_arg(fr, argno);
        bug_on(!x);
        if (isvar_int(x)) {
                *status = 0;
                return (double)x->i;
        } else if (isvar_float(x)) {
                *status = 0;
                return x->f;
        } else {
                *status = -1;
        }
        return 0.;
}

static struct var_t *
do_pow(struct vmframe_t *fr)
{
        double x, y;
        int status;

        x = get_floatarg(fr, 0, &status);
        if (status)
                goto bad;
        y = get_floatarg(fr, 1, &status);
        if (status)
                goto bad;

        return floatvar_new(pow(x, y));

bad:
        return ErrorVar;
}

static struct var_t *
do_sqrt(struct vmframe_t *fr)
{
        int status;
        double x = get_floatarg(fr, 0, &status);
        if (status)
                return ErrorVar;

        return floatvar_new(sqrt(x));
}

/*
 * hmm... I need there to be consts too,
 * but I have no way of supporting it safely.
 */
const struct inittbl_t bi_math_inittbl__[] = {
        TOFTBL("sqrt",  do_sqrt,        1, 1),
        TOFTBL("pow",   do_pow,         2, 2),
        TBLEND,
};


