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
        Object *ret = var_new(&RangeType);
        struct rangevar_t *r = V2R(ret);
        r->start = start;
        r->stop = stop;
        r->step = step;

        /* calling code should have checked this */
        bug_on(!step);
        seqvar_set_size(ret, var_slice_size(start, stop, step));
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

static bool slice_cmp_lt(int a, int b) { return a < b; }
static bool slice_cmp_gt(int a, int b) { return a > b; }

/*
 * range_getslice - A little pointless, since range creation involves
 *                  what's essentially a slice, but this makes it so
 *                  var.c code can be agnostic of our type, and assume
 *                  any sequence with a .getitem also has a .getslice.
 */
static Object *
range_getslice(Object *rng, int start, int stop, int step)
{
        Object *ret;
        bool (*cmp)(int, int);

        cmp = (start < stop) ? slice_cmp_lt : slice_cmp_gt;
        ret = arrayvar_new(0);
        while (cmp(start, stop)) {
                Object *val = range_getitem(rng, start);
                array_append(ret, val);
                VAR_DECR_REF(val);
                start += step;
        }
        return ret;
}

static bool
range_hasitem(Object *rng, Object *item)
{
        struct rangevar_t *r;
        long long ival;

        bug_on(!isvar_range(rng));

        r = V2R(rng);

        /* TODO: error for non-integers? */
        if (!isvar_int(item))
                return false;

        ival = intvar_toll(item);

        if (ival < r->start || ival >= r->stop)
                return false;

        /* need to figure out if ival would be stepped over */
        /* XXX arbitrary modulo, is there a faster way? */
        if (((ival - r->start) % r->step) != 0)
                return false;
        return true;
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
range_getprop_length(Object *self)
{
        return intvar_new(RANGE_LEN(self));
}

static const struct type_prop_t range_prop_getsets[] = {
        {
                .name = "length",
                .getprop = range_getprop_length,
                .setprop = NULL,
        }, {
                .name = NULL,
        },
};

static const struct seq_methods_t range_seq_methods = {
        .getitem        = range_getitem,
        .setitem        = NULL,
        .hasitem        = range_hasitem,
        .getslice       = range_getslice,
        .cat            = NULL,
        .sort           = NULL,
};

static const struct type_inittbl_t range_cb_methods[] = {
        V_INITTBL("foreach", var_foreach_generic, 1, 2, -1, -1),
        TBLEND,
};

struct type_t RangeType = {
        .flags  = 0,
        .name   = "range",
        .opm    = NULL,
        .cbm    = range_cb_methods,
        .mpm    = NULL,
        .sqm    = &range_seq_methods,
        .size   = sizeof(struct rangevar_t),
        .str    = range_str,
        .cmp    = range_cmp,
        .reset  = NULL,
        .prop_getsets = range_prop_getsets,
};

