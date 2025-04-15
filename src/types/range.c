/*
 * range.c - Iterable data type
 */

#include "types_priv.h"
#include <limits.h>

struct rangevar_t {
        struct seqvar_t base;
        long long start;
        long long stop;
        long long step;
};

#define V2R(v_)                 ((struct rangevar_t *)(v_))
#define V2SQ(v_)                ((struct seqvar_t *)(v_))
#define RANGE_LEN(v_)           (V2SQ(v_)->v_size)
#define RANGE_SETLEN(v_, n_)    do { V2SQ(v_)->v_size = (n_); } while (0)

struct var_t *
rangevar_new(long long start, long long stop, long long step)
{
        ssize_t len;
        struct var_t *ret = var_new(&RangeType);
        struct rangevar_t *r = V2R(ret);
        r->start = start;
        r->stop = stop;
        r->step = step;

        /* calling code should have checked this */
        bug_on(!step);

        /* calculate length */
        if (stop > start && step > 0) {
                len = (stop - start) / step;
        } else if (stop < start && step < 0) {
                len = -((stop - start) / step);
        } else {
                len = 0;
        }
        if (len < 0)
                len = -len;
        RANGE_SETLEN(ret, len);
        return ret;
}

static struct var_t *
range_getitem(struct var_t *rng, int idx)
{
        long long resi;
        struct rangevar_t *r = V2R(rng);

        /* these are bugs, not input errors, because calling code
         * should have trapped this in between.
         */
        bug_on(idx >= RANGE_LEN(rng));

        resi = r->start + r->step * (long long)idx;

        bug_on(r->start > r->stop && resi < r->stop);
        bug_on(r->start < r->stop && resi > r->stop);
        return intvar_new(resi);
}

static struct var_t *
range_cp(struct var_t *rng)
{
        /*
         * technically this should be by-val, but since it's read-only,
         * producing a reference is fine.
         */
        VAR_INCR_REF(rng);
        return rng;
}

static int
range_cmp(struct var_t *a, struct var_t *b)
{
        /* calling code already took care of the obvious
         * a==b or typeof(a) != typeof(b)
         */
        struct rangevar_t *ra = V2R(a), *rb = V2R(b);
        return ra->start == rb->start
                && ra->stop == rb->stop
                && ra->step == rb->step;
}

static struct var_t *
range_foreach(struct vmframe_t *fr)
{
        struct var_t *self, *func, *priv;
        size_t n;
        int i, status = RES_OK;

        self = vm_get_this(fr);
        bug_on(!isvar_range(self));
        n = RANGE_LEN(self);

        func = frame_get_arg(fr, 0);
        if (!isvar_function(func)) {
                err_argtype("function");
                return ErrorVar;
        }

        /* nothing to iterate over, return early */
        if (n == 0)
                return NULL;

        priv = frame_get_arg(fr, 1);
        if (!priv)
                priv = NullVar;

        bug_on(n > INT_MAX);
        for (i = 0; i < n; i++) {
                struct var_t *argv[3];
                struct var_t *retval;
                argv[0] = intvar_new(i);
                argv[1] = range_getitem(self, i);
                argv[2] = priv;

                retval = vm_exec_func(fr, func, NULL, 3, argv);
                VAR_DECR_REF(argv[0]);
                VAR_DECR_REF(argv[1]);
                if (retval == ErrorVar) {
                        status = RES_ERROR;
                        break;
                }
                /* foreach throws away retval */
                if (retval)
                        VAR_DECR_REF(retval);
        }
        return status == RES_OK ? NULL : ErrorVar;
}

static struct var_t *
range_len(struct vmframe_t *fr)
{
        struct var_t *self = vm_get_this(fr);
        bug_on(!isvar_range(self));
        return intvar_new(RANGE_LEN(self));
}

static const struct seq_methods_t range_seq_methods = {
        .getitem        = range_getitem,
        .setitem        = NULL,
        .cat            = NULL,
        .sort           = NULL,
};

static const struct type_inittbl_t range_cb_methods[] = {
        V_INITTBL("len",     range_len,     0, 0),
        V_INITTBL("foreach", range_foreach, 0, 0),
        TBLEND,
};

struct type_t RangeType = {
        .name   = "range",
        .opm    = NULL,
        .cbm    = range_cb_methods,
        .mpm    = NULL,
        .sqm    = &range_seq_methods,
        .size   = sizeof(struct rangevar_t),
        .cmp    = range_cmp,
        .cp     = range_cp,
        .reset  = NULL,
};

