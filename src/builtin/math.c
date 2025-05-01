/*
 * builtin/math.c - Implementation of the __gbl__.Math built-in object
 */
#include "builtin.h"
#include <math.h>

/* XXX easier to just return NAN for bad? */
static double
get_floatarg(Frame *fr, int argno, int *status)
{
        Object *x = frame_get_arg(fr, argno);
        bug_on(!x);
        if (!isnumvar(x)) {
                *status = -1;
                return 0.;
        } else {
                *status = 0;
                return numvar_tod(x);
        }
}

static Object *
do_pow(Frame *fr)
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

static Object *
do_sqrt(Frame *fr)
{
        int status;
        double x = get_floatarg(fr, 0, &status);
        if (status)
                return ErrorVar;

        return floatvar_new(sqrt(x));
}

const struct type_inittbl_t bi_math_inittbl__[] = {
        V_INITTBL("sqrt",  do_sqrt, 1, 1, -1, -1),
        V_INITTBL("pow",   do_pow,  2, 2, -1, -1),
        TBLEND,
};


