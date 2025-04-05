/*
 * builtin/math.c - Implementation of the __gbl__.Math built-in object
 */
#include "builtin.h"
#include <math.h>

/* XXX easier to just return NAN for bad? */
static double
get_floatarg(int argno, int *status)
{
        struct var_t *x = frame_get_arg(argno);
        bug_on(!x);
        if (x->magic == TYPE_INT) {
                *status = 0;
                return (double)x->i;
        } else if (x->magic == TYPE_FLOAT) {
                *status = 0;
                return x->f;
        } else {
                *status = -1;
        }
        return 0.;
}

static int
do_pow(struct var_t *ret)
{
        double x, y;
        int status;
        x = get_floatarg(0, &status);
        if (status)
                goto bad;
        y = get_floatarg(1, &status);
        if (status)
                goto bad;
        float_init(ret, pow(x, y));
        return 0;

bad:
        return -1;
}

static int
do_sqrt(struct var_t *ret)
{
        int status;
        double x = get_floatarg(0, &status);
        if (status)
                goto bad;
        float_init(ret, sqrt(x));
        return 0;

bad:
        return -1;
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


