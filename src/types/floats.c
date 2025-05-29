/*
 * floats.c - A more compact array for use with large amounts of numbers.
 *            Intended for statistics and DSP, where ListType objects
 *            would be too cumbersome.
 */
#include <evilcandy.h>
#include <errno.h>
#include <stdlib.h>

#define V2FLTS(v_)      ((struct floatsvar_t *)(v_))

static Object *
floatsvar_new(double *data, size_t n)
{
        Object *ret = var_new(&FloatsType);
        seqvar_set_size(ret, n);
        V2FLTS(ret)->data = data;
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
        return OP_CMP((uintptr_t)a, (uintptr_t)b);
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
        data = V2FLTS(self)->data;
        for (i = 0; i < n; i++) {
                if (data[i] == d)
                        return true;
        }
        return false;
}

static Object *
floats_getitem(Object *self, int idx)
{
        bug_on(!isvar_floats(self));
        bug_on(idx < 0 || idx >= seqvar_size(self));
        return floatvar_new(V2FLTS(self)->data[idx]);
}

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
        adat = V2FLTS(a)->data;
        bdat = V2FLTS(b)->data;

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

static const struct seq_fastiter_t floats_fast_iter = {
        .max    = floats_max,
        .min    = floats_min,
        .any    = floats_any,
        .all    = floats_all,
};

static const struct seq_methods_t floats_seq_methods = {
        .getitem        = floats_getitem,
        .setitem        = NULL, /* XXX: make immutable? */
        .hasitem        = floats_hasitem,
        .cat            = floats_cat,
        .sort           = NULL,
        .fast_iter      = &floats_fast_iter,
};

struct type_t FloatsType = {
        .name   = "floats",
        .opm    = NULL,
        .cbm    = NULL,
        .mpm    = NULL,
        .sqm    = &floats_seq_methods,
        .size   = sizeof(struct floatsvar_t),
        .str    = floats_str,
        .cmp    = floats_cmp,
        .cmpz   = floats_cmpz,
        .reset  = floats_reset,
        .prop_getsets = NULL,
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

/* Helper to floatsvar_from_text - slide ws or sep */
static char *
slidesep(const char *src, const char *sep)
{
        int c;
        while ((c = *src) != '\0' && isspace(c)
                        && (!sep || strchr(sep, c) != NULL)) {
                src++;
        }
        return (char *)src;
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
floatsvar_from_text(Object *str, const char *sep)
{
        struct buffer_t b;
        const char *src;
        size_t count;

        bug_on(!isvar_string(str));
        src = string_cstring(str);
        errno = 0;
        buffer_init(&b);
        count = 0;
        src = slidesep(src, sep);
        while (*src) {
                double d;
                char *endptr;

                d = strtod(src, &endptr);
                if (!!errno || endptr == src) {
                        err_setstr(ValueError,
                                   "floats string contains invalid characters");
                        buffer_free(&b);
                        return ErrorVar;
                }
                buffer_putd(&b, &d, sizeof(d));
                src = slidesep(endptr, sep);
                count++;
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

