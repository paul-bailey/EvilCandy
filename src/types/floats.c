/*
 * floats.c - A more compact array for use with large amounts of numbers.
 *            Intended for statistics and DSP, where ListType objects
 *            would be too cumbersome.
 */
#include <evilcandy.h>
#include <math.h>

#define V2FLTS(v_)      ((struct floatsvar_t *)(v_))

enum {
        FF_HAVE_SUM     = 0x01,
        FF_HAVE_SUMSQ   = 0x02,
        FF_HAVE_SUMDSQ  = 0x04,
        FF_HAVE_MEAN    = 0x08,
};

struct floatsvar_t {
        struct seqvar_t base;
        double *data;
        unsigned int have_stats;
        double sum;
        double sumsq;
        double sumdsq;
        double mean;
        double t0;
};

static inline void
floats_dirty(Object *v)
{
        V2FLTS(v)->have_stats = 0;
}

static inline double *
floats_get_data(Object *v)
{
        return V2FLTS(v)->data;
}

static inline void
floats_set_data(Object *v, double *data, size_t ndat)
{
        V2FLTS(v)->data = data;
        floats_dirty(v);
        seqvar_set_size(v, ndat);
}

static inline double
floats_get_datum(Object *v, size_t idx)
{
        bug_on(idx >= seqvar_size(v));
        return V2FLTS(v)->data[idx];
}

static double
samplediv(double num, size_t n)
{
        if (n > 0)
                return num / (double)n;
        else if (num > 0.0)
                return INFINITY;
        else if (num < 0.0)
                return -INFINITY;
        else
                return 0.0;
}

static void
floats_update_sum(Object *v)
{
        struct floatsvar_t *fv = V2FLTS(v);
        size_t i, n;
        double *data, sum, sumsq;

        data = floats_get_data(v);
        n = seqvar_size(v);

        sum = 0;
        sumsq = 0;
        for (i = 0; i < n; i++) {
                sum += data[i];
                sumsq += data[i] * data[i];
        }
        fv->sum = sum;
        fv->sumsq = sumsq;
        fv->mean = samplediv(sum, n);
        fv->have_stats |= FF_HAVE_SUM | FF_HAVE_SUMSQ | FF_HAVE_MEAN;
}

static void
floats_update_sumdiff(Object *v)
{
        struct floatsvar_t *fv = V2FLTS(v);
        size_t i, n;
        double *data, sumdsq, mean;

        if (!(fv->have_stats & FF_HAVE_MEAN))
                floats_update_sum(v);

        data = floats_get_data(v);
        n = seqvar_size(v);

        mean = fv->mean;
        sumdsq = 0;
        for (i = 0; i < n; i++) {
                double diff = data[i] - mean;
                sumdsq += diff * diff;
        }
        fv->sumdsq = sumdsq;
        fv->have_stats |= FF_HAVE_SUMDSQ;
}

static Object *
floatsvar_new(double *data, size_t n)
{
        Object *ret = var_new(&FloatsType);
        floats_set_data(ret, data, n);
        V2FLTS(ret)->t0 = 0.0;
        return ret;
}

static Object *
floats_str(Object *self)
{
        char buf[64];
        bug_on(!isvar_floats(self));
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "<floats at %p>", self);
        return stringvar_new(buf);
}

static int
floats_cmp(Object *a, Object *b)
{
        size_t na, nb;
        double *pa, *pb;
        bug_on(!isvar_floats(a) || !isvar_floats(b));
        na = seqvar_size(a);
        nb = seqvar_size(b);
        pa = floats_get_data(a);
        pb = floats_get_data(b);
        if (na < nb)
                return -1;
        else if (nb < na)
                return 1;
        return memcmp(pa, pb, na * sizeof(double));
}

static bool
floats_cmpz(Object *self)
{
        bug_on(!isvar_floats(self));
        return seqvar_size(self) == 0;
}

static void
floats_reset(Object *self)
{
        bug_on(!isvar_floats(self));
        efree(V2FLTS(self)->data);
}

static bool
floats_hasitem(Object *self, Object *fval)
{
        size_t i, n;
        double d;
        double *data;
        bug_on(!isvar_floats(self));
        if (isvar_float(fval)) {
                d = floatvar_tod(fval);
        } else if (isvar_int(fval)) {
                d = (double)intvar_toll(fval);
        } else {
                return false;
        }
        n = seqvar_size(self);
        data = floats_get_data(self);
        for (i = 0; i < n; i++) {
                if (data[i] == d)
                        return true;
        }
        return false;
}

static bool slice_cmp_lt(int a, int b) { return a < b; }
static bool slice_cmp_gt(int a, int b) { return a > b; }

static Object *
floats_getslice(Object *flts, int start, int stop, int step)
{
        struct buffer_t b;
        bool (*cmp)(int, int);
        double *data;
        size_t newsize;

        bug_on(!isvar_floats(flts));

        if (start == stop)
                return floatsvar_new(NULL, 0);

        data = floats_get_data(flts);
        buffer_init(&b);
        cmp = (start < stop) ? slice_cmp_lt : slice_cmp_gt;

        while (cmp(start, stop)) {
                bug_on(start >= seqvar_size(flts));
                buffer_putd(&b, &data[start], sizeof(double));
                start += step;
        }
        newsize = buffer_size(&b) / sizeof(double);
        return floatsvar_new(buffer_trim(&b), newsize);
}

/* ...start <= deletion zone < stop... */
static void
floats_delete_chunk(Object *flts, size_t start, size_t stop)
{
        double *dat = floats_get_data(flts);
        size_t n = seqvar_size(flts);
        size_t newlen = n - (stop - start);
        bug_on(start >= n);
        if (stop == start)
                return;
        if (stop < n)
                memmove(&dat[start], &dat[stop], (n - stop) * sizeof(double));
        dat = erealloc(dat, newlen * sizeof(double));
        floats_set_data(flts, dat, newlen);
}

static enum result_t
floats_setslice(Object *flts, int start, int stop, int step, Object *val)
{
        bug_on(!isvar_floats(flts));
        if (!val) {
                /* delete slice */
                err_setstr(NotImplementedError,
                           "Floats slice deletion not yet supported");
                return RES_ERROR;
        } else {
                /* insert/add slice */
                Object **osrc = NULL;
                double *fsrc = NULL;
                double *dst;
                size_t i, n;
                bool (*cmp)(int, int);

                bug_on(!isvar_seq(val));
                if (isvar_array(val)) {
                        osrc = array_get_data(val);
                } else if (isvar_tuple(val)) {
                        osrc = tuple_get_data(val);
                } else if (isvar_floats(val)) {
                        fsrc = floats_get_data(val);
                } else {
                        err_setstr(TypeError,
                                   "Cannot set floats slice from type %s",
                                   typestr(val));
                        return RES_ERROR;
                }

                n = (stop - start) / step;
                bug_on((int)n < 0);
                if (n < seqvar_size(val) && stop > start && step != 1) {
                        err_setstr(ValueError, "Cannot extend list for step > 1");
                        return RES_ERROR;
                }

                if (osrc) {
                        /* Validate this */
                        for (i = 0; i < seqvar_size(val); i++) {
                                if (!isvar_float(osrc[i])) {
                                        err_setstr(TypeError,
                                                "Expect float in sequence but found %s",
                                                typestr(osrc[i]));
                                        return RES_ERROR;
                                }
                        }
                }

                cmp = (start < stop) ? slice_cmp_lt : slice_cmp_gt;
                dst = floats_get_data(flts);
                i = 0;
                n = seqvar_size(val);
                if (cmp(start, stop))
                        floats_dirty(flts);
                while (cmp(start, stop)) {
                        double d;
                        bug_on(start >= seqvar_size(flts));
                        if (i >= n) {
                                /* end of src, delete rest */
                                if (step < 0)
                                        break;
                                floats_delete_chunk(flts, start, seqvar_size(flts));
                                return RES_OK;
                        }
                        if (osrc)
                                d = floatvar_tod(osrc[i]);
                        else
                                d = fsrc[i];
                        dst[start] = d;
                        start += step;
                        i++;
                }

                if (step == 1 && i < n) {
                        size_t j = seqvar_size(flts);
                        size_t newsize = j + n - i;
                        dst = erealloc(dst, newsize * sizeof(double));
                        while (i < n) {
                                double d;
                                if (osrc)
                                        d = floatvar_tod(osrc[i]);
                                else
                                        d = fsrc[i];
                                dst[j] = d;
                                i++;
                                j++;
                        }
                        efree(fsrc);
                        floats_set_data(flts, dst, newsize);
                }
        }
        return RES_OK;
}

static Object *
floats_getitem(Object *self, int idx)
{
        bug_on(!isvar_floats(self));
        bug_on(idx < 0 || idx >= seqvar_size(self));
        return floatvar_new(floats_get_datum(self, idx));
}

static enum result_t
floats_setitem(Object *self, int i, Object *child)
{
        bug_on(!isvar_floats(self));
        bug_on(i >= seqvar_size(self));
        if (child) {
                double d, *data;

                if (isvar_int(child)) {
                        d = (double)intvar_toll(child);
                } else if (isvar_float(child)) {
                        d = floatvar_tod(child);
                } else {
                        err_setstr(TypeError, "Expected: real number");
                        return RES_ERROR;
                }

                data = floats_get_data(self);
                data[i] = d;
                /* TODO; mark changed */
        } else {
                floats_delete_chunk(self, i, i + 1);
        }
        floats_dirty(self);
        return RES_OK;
}

/*
 * XXX: This means I can't use '+' for offset or '*' for gain.
 *      Is that what I really want?
 */
static Object *
floats_cat(Object *a, Object *b)
{
        /* # of floats, not bytes */
        size_t alen, blen, clen;
        double *adat, *bdat, *cdat;

        bug_on(!isvar_floats(a));
        bug_on(!isvar_floats(b));

        alen = seqvar_size(a);
        blen = seqvar_size(b);
        adat = floats_get_data(a);
        bdat = floats_get_data(b);

        clen = alen + blen;
        cdat = emalloc(clen * sizeof(double));
        memcpy(cdat, adat, alen * sizeof(double));
        memcpy(&cdat[alen], bdat, blen * sizeof(double));

        return floatsvar_new(cdat, clen);
}

static Object *
floats_minmax(Object *self, int ismin)
{
        double resd, *data;
        size_t i, n;
        bug_on(!isvar_floats(self));

        n = seqvar_size(self);
        if (n == 0) {
                err_setstr(ValueError, "Size is zero");
                return ErrorVar;
        }
        data = floats_get_data(self);
        resd = data[0];
        for (i = 1; i < n; i++) {
                if (ismin && data[i] < resd)
                        resd = data[i];
                else if (!ismin && data[i] > resd)
                        resd = data[i];
        }
        return floatvar_new(resd);
}

static bool
floats_allany(Object *self, int isall)
{
        bool res;
        size_t i, n;
        double *data;
        bug_on(!isvar_floats(self));
        n = seqvar_size(self);
        /*
         * Be like Python when size is zero:
         * false for any(), true for all()
         */
        if (n == 0)
                return isall;

        data = floats_get_data(self);
        res = false;
        for (i = 0; i < n; i++) {
                res = data[i] != 0.0;
                if (isall && !res)
                        break;
                if (!isall && res)
                        break;
        }
        return res;
}

static Object *
floats_min(Object *self)
{
        return floats_minmax(self, true);
}

static Object *
floats_max(Object *self)
{
        return floats_minmax(self, false);
}

static bool
floats_all(Object *self)
{
        return floats_allany(self, true);
}

static bool
floats_any(Object *self)
{
        return floats_allany(self, false);
}

static enum result_t
obj2double(Object *obj, double *d)
{
        if (isvar_int(obj)) {
                *d = (double)intvar_toll(obj);
        } else if (isvar_float(obj)) {
                *d = floatvar_tod(obj);
        } else {
                err_setstr(TypeError, "Expected real number but got '%s'",
                           typestr(obj));
                return RES_ERROR;
        }
        return RES_OK;
}

/* Get a real-number arg and convert to double if necessary */
static enum result_t
arg2double(Frame *fr, int argno, double *d)
{
        Object *arg = vm_get_arg(fr, argno);
        bug_on(!arg);
        return obj2double(arg, d);
}

static Object *
floats_getprop_length(Object *self)
{
        bug_on(!isvar_floats(self));
        return intvar_new(seqvar_size(self));
}

static Object *
floats_getprop_t0(Object *self)
{
        bug_on(!isvar_floats(self));
        return floatvar_new(V2FLTS(self)->t0);
}

static enum result_t
floats_setprop_t0(Object *self, Object *val)
{
        double d;
        bug_on(!isvar_floats(self));
        if (obj2double(val, &d) == RES_ERROR)
                return RES_ERROR;
        V2FLTS(self)->t0 = d;
        return RES_OK;
}

/*
 * TODO: add kwarg shave=true to remove convolution tail, normalize=true
 * FIXME: What about T0 for f and g?
 * XXX: Separate calls for new signal, or in-place signal.
 */
static Object *
do_floats_convolve(Frame *fr)
{
        /* self=f, arg=g, using familiar convolution f * g convention */
        Object *self, *arg;
        double *pf, *pg, *pc;
        size_t i, nf, ng, nc;

        self = vm_get_this(fr);
        if (arg_type_check(self, &FloatsType) == RES_ERROR)
                return ErrorVar;
        arg = vm_get_arg(fr, 0);
        if (arg_type_check(arg, &FloatsType) == RES_ERROR)
                return ErrorVar;

        nf = seqvar_size(self);
        ng = seqvar_size(arg);
        pf = floats_get_data(self);
        pg = floats_get_data(arg);

        nc = nf + ng - 1;
        pc = emalloc(nc * sizeof(double));

        for (i = 0; i < nc; i++) {
                /* Sum all products of overlaps, g is reversed */
                ssize_t foffs; /* signed for <0 test */
                double sum = 0.0;
                size_t j = ng - 1;
                while ((foffs = (i - (ng - 1 - j))) >= 0) {
                        if (foffs < nf)
                                sum += pf[foffs] * pg[j];
                        /*
                         * foffs >= nf means we're near the end of algo.
                         * Continue loop until foffs < nf, final overlaps.
                         */
                        j--;
                }
                pc[i] = sum;
        }
        efree(pf);
        floats_set_data(self, pc, nc);
        V2FLTS(self)->t0 += V2FLTS(arg)->t0;
        return NULL;
}

static Object *
do_floats_gain(Frame *fr)
{
        Object *self;
        double arg, *data;
        size_t i, n;

        self = vm_get_this(fr);
        if (arg_type_check(self, &FloatsType) == RES_ERROR)
                return ErrorVar;
        if (arg2double(fr, 0, &arg) == RES_ERROR)
                return ErrorVar;

        n = seqvar_size(self);
        data = floats_get_data(self);
        for (i = 0; i < n; i++)
                data[i] *= arg;

        if (n)
                floats_dirty(self);
        return NULL;
}

static Object *
do_floats_offset(Frame *fr)
{
        Object *self;
        double arg, *data;
        size_t i, n;

        self = vm_get_this(fr);
        if (arg_type_check(self, &FloatsType) == RES_ERROR)
                return ErrorVar;
        if (arg2double(fr, 0, &arg) == RES_ERROR)
                return ErrorVar;

        n = seqvar_size(self);
        data = floats_get_data(self);
        for (i = 0; i < n; i++)
                data[i] += arg;

        if (n)
                floats_dirty(self);
        return NULL;
}

static Object *
do_floats_mean(Frame *fr)
{
        struct floatsvar_t *fv;
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &FloatsType) == RES_ERROR)
                return ErrorVar;
        fv = V2FLTS(self);
        if (!(fv->have_stats & FF_HAVE_MEAN))
                floats_update_sum(self);
        return floatvar_new(fv->mean);
}

/* TODO: 'bessel=true' keyword arg for N-1 instead of N */
static Object *
do_floats_stddev(Frame *fr)
{
        double d;
        size_t n;
        struct floatsvar_t *fv;
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &FloatsType) == RES_ERROR)
                return ErrorVar;
        fv = V2FLTS(self);
        n = seqvar_size(self);
        if (!(fv->have_stats & FF_HAVE_SUMDSQ))
                floats_update_sumdiff(self);
        d = samplediv(fv->sumdsq, n);
        if (isfinite(d))
                d = sqrt(d);
        return floatvar_new(d);
}

static Object *
do_floats_sum(Frame *fr)
{
        struct floatsvar_t *fv;
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &FloatsType) == RES_ERROR)
                return ErrorVar;
        fv = V2FLTS(self);
        if (!(fv->have_stats & FF_HAVE_SUM))
                floats_update_sum(self);
        return floatvar_new(fv->sum);
}

static const struct seq_fastiter_t floats_fast_iter = {
        .max    = floats_max,
        .min    = floats_min,
        .any    = floats_any,
        .all    = floats_all,
};

static const struct type_prop_t floats_prop_getsets[] = {
        {
                .name = "length",
                .getprop = floats_getprop_length,
                .setprop = NULL,
        }, {
                .name = "t0",
                .getprop = floats_getprop_t0,
                .setprop = floats_setprop_t0,
        }, {
                .name = NULL,
        },
};

static const struct type_inittbl_t floats_cb_methods[] = {
        V_INITTBL("convolve",   do_floats_convolve,     1, 1, -1, -1),
        V_INITTBL("gain",       do_floats_gain,         1, 1, -1, -1),
        V_INITTBL("offset",     do_floats_offset,       1, 1, -1, -1),
        V_INITTBL("mean",       do_floats_mean,         0, 0, -1, -1),
        V_INITTBL("stddev",     do_floats_stddev,       0, 0, -1, -1),
        V_INITTBL("sum",        do_floats_sum,          0, 0, -1, -1),
        TBLEND,
};

static const struct seq_methods_t floats_seq_methods = {
        .getitem        = floats_getitem,
        .setitem        = floats_setitem,
        .hasitem        = floats_hasitem,
        .getslice       = floats_getslice,
        .setslice       = floats_setslice,
        .cat            = floats_cat,
        .sort           = NULL,
        .fast_iter      = &floats_fast_iter,
};

struct type_t FloatsType = {
        .flags  = 0,
        .name   = "floats",
        .opm    = NULL,
        .cbm    = floats_cb_methods,
        .mpm    = NULL,
        .sqm    = &floats_seq_methods,
        .size   = sizeof(struct floatsvar_t),
        .str    = floats_str,
        .cmp    = floats_cmp,
        .cmpz   = floats_cmpz,
        .reset  = floats_reset,
        .prop_getsets = floats_prop_getsets,
};

/**
 * floatsvar_from_array - Build a floats object from an array
 *                        of float-type objects.
 * @src: Array of objects.  These will be checked for correct type.
 *       Valid types are floats or integers.
 * @n:   Array size of @src.
 *
 * Return:  A new floats object, or ErrorVar if @src contains
 * invalid types.
 */
Object *
floatsvar_from_array(Object **src, size_t n)
{
        Object **end;
        double *new_data, *dst;

        new_data = emalloc(sizeof(*dst) * n);
        end = src + n;
        if (n == 0 || !src) {
                err_setstr(ValueError, "List is empty");
                return ErrorVar;
        }
        dst = new_data;
        while (src < end) {
                double d;
                if (isvar_float(*src)) {
                        d = floatvar_tod(*src);
                } else if (isvar_int(*src)) {
                        d = (double)intvar_toll(*src);
                } else {
                        err_setstr(TypeError,
                                "Input must be float or integer");
                        efree(new_data);
                        return ErrorVar;
                }

                *dst = d;
                dst++;
                src++;
        }
        return floatsvar_new(new_data, n);
}

/**
 * floatsvar_from_text - Build a floats object from a string
 * @v: Text containing floating-point values
 * @sep: If NULL, expect only whitespace to delimit values, otherwise,
 *      sep is a set of all non-whitespace characters to skip.
 *
 * Return: New floats object, or ErrorVar if @str is malformed.
 */
Object *
floatsvar_from_text(Object *str, Object *sep)
{
        struct buffer_t b;
        size_t count, pos, len;

        bug_on(!isvar_string(str));
        bug_on(sep != NULL && sep != NullVar && !isvar_string(sep));

        buffer_init(&b);
        count = 0;
        len = seqvar_size(str);
        pos = string_slide(str, sep, 0);
        while (pos < len) {
                double d;
                if (string_tod(str, &pos, &d) == RES_ERROR) {
                        err_setstr(ValueError,
                                   "floats string contains invalid characters");
                        buffer_free(&b);
                        return ErrorVar;
                }
                buffer_putd(&b, &d, sizeof(d));
                count++;
                pos = string_slide(str, sep, pos);
        }
        return floatsvar_new(buffer_trim(&b), count);
}

static uint64_t
unpack64(const unsigned char *data, int le)
{
        int incr, n;
        const unsigned char *p;
        uint64_t res;
        if (le) {
                p = &data[7];
                incr = -1;
        } else {
                p = data;
                incr = 1;
        }

        n = 8;
        res = 0;
        while (n-- > 0) {
                res <<= 8;
                res += *p;
                p += incr;
        }

        return res;
}

static uint32_t
unpack32(const unsigned char *data, int le)
{
        if (le)
                return data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24;
        else
                return data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
}

Object *
floatsvar_from_bytes(Object *v, enum floats_enc_t enc, int le)
{
        const unsigned char *data, *src, *end;
        double *new_data, *dst;
        size_t n, srcwid, n_dst;

        bug_on(!isvar_bytes(v));

        data = bytes_get_data(v);
        n = seqvar_size(v);

        /*
         * I can't really figure out what to do if a computer
         * was manufactured in Obscuresylvania.
         */
        bug_on(sizeof(float) != 4);
        bug_on(sizeof(double) != 8);

        switch (enc) {
        case FLOATS_BINARY64:
                srcwid = 8;
                n_dst = n / 8;
                if ((n % 8) != 0)
                        goto esize;
                goto floatsrc;
        case FLOATS_BINARY32:
                srcwid = 4;
                n_dst = n / 4;
                if ((n % 4) != 0)
                        goto esize;
                goto floatsrc;
        case FLOATS_UINT64:
                srcwid = 8;
                n_dst = n / 8;
                if ((n % 8) != 0)
                        goto esize;
                goto uintsrc;
        case FLOATS_UINT32:
                srcwid = 4;
                n_dst = n / 4;
                if ((n % 4) != 0)
                        goto esize;
                goto uintsrc;
        case FLOATS_INT64:
        case FLOATS_INT32:
        case FLOATS_INT16:
        case FLOATS_INT8:
        case FLOATS_UINT16:
        case FLOATS_UINT8:
                err_setstr(NotImplementedError,
                           "floats() initialization in this way not yet supported");
                return ErrorVar;
        default:
                bug();
        }

floatsrc:
        new_data = emalloc(n_dst * sizeof(double));
        src = data;
        end = data + n;
        dst = new_data;
        while (src < end) {
                if (srcwid == 8) {
                        union {
                                uint64_t i;
                                double d;
                        } x = { .i = unpack64(src, le) };
                        *dst = x.d;
                } else {
                        bug_on(srcwid != 4);
                        union {
                                uint32_t i;
                                float f;
                        } x = { .i = unpack32(src, le) };
                        *dst = (double)x.f;
                }

                src += srcwid;
                dst++;
        }
        return floatsvar_new(new_data, n_dst);

uintsrc:
        new_data = emalloc(n_dst * sizeof(double));
        src = data;
        end = data + n;
        dst = new_data;
        while (src < end) {
                if (srcwid == 8) {
                        uint64_t i = unpack64(src, le);
                        *dst = (double)i;
                } else {
                        bug_on(srcwid != 4);
                        uint32_t i = unpack32(src, le);
                        *dst = (double)i;
                }

                src += srcwid;
                dst++;
        }
        return floatsvar_new(new_data, n_dst);

esize:
        err_setstr(ValueError,
                   "bytes size must be an exact multiple of the size specified");
        return ErrorVar;
}

