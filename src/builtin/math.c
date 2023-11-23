/*
 * builtin/math.c - Implementation of the __gbl__.Math built-in object
 */
#include "builtin.h"
#include <math.h>

static double
get_floatarg(int argno)
{
        struct var_t *x = getarg(argno);
        bug_on(!x);
        if (x->magic == QINT_MAGIC)
                return (double)x->i;
        else if (x->magic == QFLOAT_MAGIC)
                return x->f;
        else
                syntax("Expected: float or int");
        return 0.;
}

static void
do_pow(struct var_t *ret)
{
        double x, y;
        x = get_floatarg(0);
        y = get_floatarg(1);
        qop_assign_float(ret, pow(x, y));
}

static void
do_sqrt(struct var_t *ret)
{
        double x = get_floatarg(0);
        qop_assign_float(ret, sqrt(x));
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


