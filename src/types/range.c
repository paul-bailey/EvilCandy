/*
 * range.c - Iterable data type
 */

#include <evilcandy.h>

struct rangevar_t {
        struct seqvar_t base;
        long long start;
        long long stop;
        long long step;
};

#define V2R(v_)                 ((struct rangevar_t *)(v_))
#define V2SQ(v_)                ((struct seqvar_t *)(v_))
#define RANGE_LEN(v_)           seqvar_size(v_)
#define RANGE_SETLEN(v_, n_)    do { seqvar_set_size(v_, (n_)); } while (0)

Object *
rangevar_new(long long start, long long stop, long long step)
{
        ssize_t len;
        Object *ret = var_new(&RangeType);
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

static Object *
range_getitem(Object *rng, int idx)
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

static int
range_cmp(Object *a, Object *b)
{
        /* calling code already took care of the obvious
         * a==b or typeof(a) != typeof(b)
         */
        struct rangevar_t *ra = V2R(a), *rb = V2R(b);
        return ra->start == rb->start
                && ra->stop == rb->stop
                && ra->step == rb->step;
}

static Object *
range_str(Object *v)
{
        char buf[128];
        struct rangevar_t *r = V2R(v);
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "range(%lld, %lld, %lld)",
                 r->start, r->stop, r->step);
        return stringvar_new(buf);
}

static Object *
range_foreach(Frame *fr)
{
        Object *self, *func, *priv;
        size_t n;
        int i, status = RES_OK;

        self = vm_get_this(fr);
        if (arg_type_check(self, &RangeType) == RES_ERROR)
                return ErrorVar;

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
                Object *argv[3];
                Object *retval;
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

static Object *
range_len(Frame *fr)
{
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &RangeType) == RES_ERROR)
                return ErrorVar;
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
        .str    = range_str,
        .cmp    = range_cmp,
        .reset  = NULL,
};

