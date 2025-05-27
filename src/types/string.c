/*
 * string.c - Built-in methods for string data types
 *
 *      Creating new string objects:
 *      ----------------------------
 *
 * Strings have two buffers (unless they happen to be 100%-ASCII). The
 * first is the string objects's .s field, a C-string containing only
 * ASCII or UTF-8 encoded characters.  This is '\0'-terminated; EvilCandy
 * strings may not have embedded zeroes, or else this won't work.  The
 * other buffer is an array of Unicode points, whose width is specified
 * by the string's .s_width field.  For speed, the Unicode arrays are
 * operated on the most.  The C string is used for hashing and printing
 * (since most every output takes UTF-8).
 *
 * If any .s field is not properly UTF-8 encoded, then different Objects
 * with the exact same Unicode points could end up with different hashes.
 * So these all MUST have proper encoding.  stringvar_from_source() will
 * take care of this for literal expressions in user code, but all other
 * string-creation functions, which are for internal use, assume that the
 * argument is properly-encoded already.
 *
 * creation for INTERNAL use:
 *         stringvar_new()
 *         stringvar_newn()
 *         stringvar_from_buffer()
 *         stringvar_nocopy()
 *
 * creation from USER literal:
 *         stringvar_from_source()
 */
#include <evilcandy.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h> /* strtol and friends */

/* user argument limits */
enum {
        JUST_MAX = 10000,
        PRECISION_MAX = 30,
        PAD_MAX = JUST_MAX,
};

enum {
        /* flags arg to stringvar_newf, see comments there */
        SF_COPY = 0x0001,

        /* Other common flags to methods' helper functions */
        SF_RIGHT        = 0x0010,       /* from the right (not left) */
        SF_CENTER       = 0x0020,       /* both left and right */
        SF_SUPPRESS     = 0x0040,       /* suppress errors */
};

#define V2STR(v)                ((struct stringvar_t *)(v))
#define V2CSTR(v)               string_cstring(v)
#define STRING_LENGTH(str)      seqvar_size(str)
#define STRING_NBYTES(str)      string_nbytes(str)


/* **********************************************************************
 *                      Common Helpers
 ***********************************************************************/

static long
string_getidx(Object *str, size_t idx)
{
        if (idx >= seqvar_size(str))
                return -1;
        switch (V2STR(str)->s_width) {
        case 1:
                return ((uint8_t *)V2STR(str)->s_unicode)[idx];
        case 2:
                return ((uint16_t *)V2STR(str)->s_unicode)[idx];
        case 4:
                return ((uint32_t *)V2STR(str)->s_unicode)[idx];
        default:
                bug();
                return 0;
        }
}

/*
 * struct string_writer_t - Because wrappers for struct buffer_t would be
 *                          too cumbersome, we'll just do it manually.
 */
struct string_writer_t {
        size_t width;
        union {
                uint8_t *u8;
                uint16_t *u16;
                uint32_t *u32;
                void *p;
        } p;
        unsigned long maxchr;
        size_t pos;
        size_t pos_i;
        size_t n_alloc;
};

static void
string_writer_init(struct string_writer_t *wr, size_t width)
{
        wr->width = width;
        switch (width) {
        case 1:
                wr->maxchr = 0xffu;
                break;
        case 2:
                wr->maxchr = 0xffffu;
                break;
        case 4:
                wr->maxchr = 0x10ffffu;
                break;
        default:
                bug();
        }
        wr->p.p = NULL;
        wr->pos = wr->pos_i = wr->n_alloc = 0;
}

static void
string_writer_append(struct string_writer_t *wr, unsigned long c)
{
        enum { STR_REALLOC_SIZE = 64 };

        if (c > wr->maxchr) {
                /*
                 * Ugh, need to resize.  This should only occur from
                 * string_parse, when we're loading a source file.
                 */
                struct string_writer_t wr2;
                size_t width;
                size_t i;

                bug_on(c > 0x10fffful);

                if (c > 0xfffful) {
                        width = 4;
                } else if (c > 0xfful) {
                        width = 2;
                } else{
                        bug();
                        return;
                }

                string_writer_init(&wr2, width);
                for (i = 0; i < wr->pos_i; i++) {
                        unsigned long oldchar;
                        switch (wr->width) {
                        case 1:
                                oldchar = wr->p.u8[i];
                                break;
                        case 2:
                                oldchar = wr->p.u16[i];
                                break;
                        default:
                                bug();
                                return;
                        }
                        string_writer_append(&wr2, oldchar);
                }
                if (wr->p.p)
                        efree(wr->p.p);
                bug_on(wr->pos_i != wr2.pos_i);
                memcpy(wr, &wr2, sizeof(wr2));
                /* fall through, we still have to write c */
        }

        bug_on(c > wr->maxchr);

        if (wr->pos + wr->width > wr->n_alloc) {
                wr->n_alloc += STR_REALLOC_SIZE * wr->width;
                wr->p.p = erealloc(wr->p.p, wr->n_alloc);
        }

        switch (wr->width) {
        case 1:
                wr->p.u8[wr->pos_i] = c;
                break;
        case 2:
                wr->p.u16[wr->pos_i] = c;
                break;
        case 4:
                wr->p.u32[wr->pos_i] = c;
                break;
        default:
                bug();
        }
        wr->pos_i++;
        wr->pos += wr->width;
}

static void
string_writer_appends(struct string_writer_t *wr, const char *cstr)
{
        while (*cstr != '\0') {
                string_writer_append(wr, (unsigned char)*cstr);
                cstr++;
        }
}

static void
string_writer_append_strobj(struct string_writer_t *wr, Object *str)
{
        size_t i, n;
        n = seqvar_size(str);
        for (i = 0; i < n; i++) {
                long point = string_getidx(str, i);
                bug_on(point < 0L);
                string_writer_append(wr, point);
        }
}


static long
string_writer_getidx(struct string_writer_t *wr, size_t idx)
{
        if (idx >= wr->pos_i)
                return -1;
        switch (wr->width) {
        case 1:
                return wr->p.u8[idx];
        case 2:
                return wr->p.u16[idx];
        case 4:
                return wr->p.u32[idx];
        default:
                bug();
                return 0;
        }
}

/* with string_writer_getidx, used by format2, for swapping pads */
static int
string_writer_setidx(struct string_writer_t *wr,
                     size_t idx, unsigned long point)
{
        if (idx >= wr->pos_i)
                return -1;
        switch (wr->width) {
        case 1:
                wr->p.u8[idx] = point;
                break;
        case 2:
                wr->p.u16[idx] = point;
                break;
        case 4:
                wr->p.u32[idx] = point;
                break;
        default:
                bug();
        }
        return 0;
}

static void *
string_writer_finish(struct string_writer_t *wr, size_t *width, size_t *len)
{
        *len = wr->pos_i;
        *width = wr->width;
        if (wr->p.p == NULL) {
                bug_on(*len != 0);
                return NULL;
        }

        /*
         * XXX: Maybe avoid realloc
         * if wr->n_alloc - wr->pos < some_threshold
         */
        return erealloc(wr->p.p, wr->pos);
}

/*
 * Flags are:
 *      SF_COPY         make a copy of @cstr
 *      0               use @cstr exactly and free on reset
 * There used to be more, but they went obsolete.
 */
static Object *
stringvar_newf(char *cstr, unsigned int flags)
{
        struct stringvar_t *vs;
        Object *ret;

        if (!cstr) {
                cstr = "";
                flags |= SF_COPY;
        }

        ret = var_new(&StringType);
        vs = V2STR(ret);
        if (!!(flags & SF_COPY)) {
                if (cstr[0] == '\0') {
                        cstr = emalloc(1);
                        cstr[0] = '\0';
                        vs->s = cstr;
                } else {
                        vs->s = estrdup(cstr);
                }
        } else {
                vs->s = cstr;
        }
        vs->s_ascii_len = strlen(vs->s);
        /*
         * We only hash the first time it's needed.  If we never need
         * it, we never hash.
         */
        vs->s_hash = 0;
        vs->s_unicode = utf8_decode(vs->s, &vs->s_width, &vs->s_enc_len,
                                    &vs->s_ascii);
        seqvar_set_size(ret, vs->s_enc_len);
        return ret;
}

static Object *
stringvar_from_points(void *points, size_t width,
                      size_t len, unsigned int flags)
{
        Object *ret;
        struct buffer_t b;
        size_t i;
        int ascii;
        struct stringvar_t *vs;
        union {
                uint32_t *p32;
                uint16_t *p16;
                uint8_t *p8;
                void *p;
        } x = { .p = points };

        if (!len) {
                bug_on(!!points);
                return VAR_NEW_REF(STRCONST_ID(mpty));
        }

        bug_on(!points);

        ascii = 1;
        buffer_init(&b);
        for (i = 0; i < len; i++) {
                uint32_t point;
                switch (width) {
                case 4:
                        point = x.p32[i];
                        break;
                case 2:
                        point = x.p16[i];
                        break;
                case 1:
                        point = x.p8[i];
                        break;
                default:
                        bug();
                        return NULL;
                }

                if (point < 128) {
                        buffer_putc(&b, point);
                        continue;
                }

                ascii = 0;

                /* We should have trapped this already */
                bug_on(!utf8_valid_unicode(point));
                utf8_encode(point, &b);
        }
        ret = var_new(&StringType);
        vs = V2STR(ret);

        if (!!(flags & SF_COPY))
                vs->s_unicode = ememdup(points, len * width);
        else
                vs->s_unicode = points;
        vs->s_enc_len   = len;
        vs->s_width     = width;
        vs->s_ascii_len = buffer_size(&b);
        vs->s           = buffer_trim(&b);
        vs->s_hash      = 0;
        vs->s_ascii     = ascii;
        seqvar_set_size(ret, len);
        return ret;
}

static Object *
stringvar_from_writer(struct string_writer_t *wr)
{
        size_t width, len;
        void *buf = string_writer_finish(wr, &width, &len);
        return stringvar_from_points(buf, width, len, 0);
}

/* Quicker verion of slice - substr is old[start:stop] */
static Object *
stringvar_from_substr(Object *old, size_t start, size_t stop)
{
        size_t width, len;
        void *buf;

        bug_on(start >= seqvar_size(old));
        bug_on(stop > seqvar_size(old));
        bug_on(stop < start);

        width = V2STR(old)->s_width;
        len  = stop - start;
        buf = V2STR(old)->s_unicode + start * width;

        return stringvar_from_points(buf, width, len, SF_COPY);
}

/*
 * helper to stringvar_from_source -
 *      interpolate a string's backslash escapes
 */
enum result_t
string_parse(const char *src, void **buf, size_t *width, size_t *len)
{
        unsigned int c, q;
        const unsigned char *s = (unsigned char *)src;
        struct string_writer_t wr;
        enum { BKSL = '\\', SQ = '\'', DQ = '"' };

        string_writer_init(&wr, 1);

        q = *s++;
        bug_on(q != SQ && q != DQ);

again:
        while ((c = *s++) != q) {
                /* should have been trapped already */
                bug_on(c == 0);
                if (c == BKSL) {
                        c = *s++;
                        if (c == q) {
                                string_writer_append(&wr, c);
                                continue;
                        } else if (c == 'n') {
                                /*
                                 * could be in switch,
                                 * but it's our 99% scenario.
                                 */
                                string_writer_append(&wr, '\n');
                                continue;
                        }

                        switch (c) {
                        case 'a': /* bell - but why? */
                                string_writer_append(&wr, '\a');
                                continue;
                        case 'b':
                                string_writer_append(&wr, '\b');
                                continue;
                        case 'e':
                                string_writer_append(&wr, '\033');
                                continue;
                        case 'f':
                                string_writer_append(&wr, '\f');
                                continue;
                        case 'v':
                                string_writer_append(&wr, '\v');
                                continue;
                        case 'r':
                                string_writer_append(&wr, '\r');
                                continue;
                        case 't':
                                string_writer_append(&wr, '\t');
                                continue;
                        case BKSL:
                                string_writer_append(&wr, BKSL);
                                continue;
                        default:
                                break;
                        }

                        /*
                         * XXX REVISIT: This doesn't match documenation!
                         * Even if adjacent octal or hex escape sequences
                         * are equivalent to valid UTF-8 sequences, they
                         * will **EACH** be encoded into separate UTF-8
                         * sequences.  Either fix that here or (easier)
                         * in the Documenation.  Python seems to do the
                         * same thing, so maybe change documentation.
                         */
                        if (isodigit(c)) {
                                unsigned int i, v;
                                --s;
                                for (i = 0, v = 0; i < 3; i++, s++) {
                                        if (!isodigit(*s))
                                                break;
                                        /* '0' & 7 happens to be 0 */
                                        v = (v << 3) + (*s & 7);
                                }
                                if (v == 0 || v >= 256)
                                        goto err;
                                string_writer_append(&wr, v);
                                continue;
                        }

                        if (c == 'x' || c == 'X') {
                                unsigned int v;
                                if (!isxdigit(s[0]) || !isxdigit(s[1]))
                                        goto err;
                                v = x2bin(s[0]) * 16 + x2bin(s[1]);
                                if (v == 0)
                                        goto err;

                                s += 2;
                                string_writer_append(&wr, v);
                                continue;
                        }

                        if (c == 'u' || c == 'U') {
                                unsigned long point = 0;
                                int i, amt = c == 'u' ? 4 : 8;

                                for (i = 0; i < amt; i++) {
                                        if (!isxdigit((int)(s[i])))
                                                goto err;
                                        point <<= 4;
                                        point |= x2bin(s[i]);
                                }

                                if (point == 0)
                                        goto err;
                                /* out-of-range for Unicode */
                                if (!utf8_valid_unicode(point))
                                        goto err;

                                s += amt;
                                string_writer_append(&wr, point);
                                continue;
                        }

                        /* wrapping code would have caught this */
                        bug_on(c == '\0');

                        /* unsupported escape */
                        goto err;
                } else if (c > 127) {
                        long point;
                        unsigned char *endptr;
                        point = utf8_decode_one(s-1, &endptr);
                        if (point >= 0L) {
                                string_writer_append(&wr, point);
                                s = endptr;
                        } else {
                                string_writer_append(&wr, c);
                        }
                } else {
                        string_writer_append(&wr, c);
                }
        }

        /* wrapping code should have caught this earlier */
        bug_on(c != q);
        c = *s++;
        if (c != '\0') {
                /* also should have been checked */
                bug_on(!isquote(c));

                /* in case a weirdo wrote "string1" 'string2' */
                q = c;
                goto again;
        }

        *buf = string_writer_finish(&wr, width, len);
        return RES_OK;

err:
        if (wr.p.p)
                efree(wr.p.p);
        return RES_ERROR;
}

static void *
widen_buffer(Object *str, size_t width)
{
        void *tbuf, *src, *dst, *end;
        size_t n, old_width;

        old_width = V2STR(str)->s_width;
        bug_on(old_width >= width);

        n = seqvar_size(str);
        tbuf = emalloc(n * width);

        src = V2STR(str)->s_unicode;
        dst = tbuf;
        /* src end, not dst end */
        end = src + n * old_width;
        while (src < end) {
                if (width == 2) {
                        bug_on(old_width != 1);
                        *(uint16_t *)dst = *(uint8_t *)src;
                } else {
                        bug_on(width != 4);
                        if (old_width == 1) {
                                *(uint32_t *)dst = *(uint8_t *)src;
                        } else {
                                bug_on(old_width != 2);
                                *(uint32_t *)dst = *(uint16_t *)src;
                        }
                }
                src += old_width;
                dst += width;
        }
        return tbuf;
}

static ssize_t
find_idx(Object *haystack, Object *needle, unsigned int flags)
{
        ssize_t hwid, nwid, hlen, nlen, idx;

        bug_on(!isvar_string(haystack));
        bug_on(!isvar_string(needle));

        hlen = seqvar_size(haystack);
        nlen = seqvar_size(needle);
        hwid = V2STR(haystack)->s_width;
        nwid = V2STR(needle)->s_width;

        if (hwid < nwid || hlen < nlen) {
                idx = -1;
        } else {
                void *found;
                void *hsrc = V2STR(haystack)->s_unicode;
                void *nsrc = V2STR(needle)->s_unicode;
                if (hwid != nwid)
                        nsrc = widen_buffer(needle, hwid);
                if (!!(flags & SF_RIGHT))
                        found = memrmem(hsrc, hlen * hwid, nsrc, nlen * hwid);
                else
                        found = memmem(hsrc, hlen * hwid, nsrc, nlen * hwid);
                if (!found)
                        idx = -1;
                else
                        idx = (ssize_t)(found - hsrc) / hwid;
                if (nsrc != V2STR(needle)->s_unicode)
                        efree(nsrc);
        }
        bug_on(idx >= (ssize_t)seqvar_size(haystack));
        return idx;
}


/* **********************************************************************
 *                      format2 and helpers
 ***********************************************************************/

/*
 * FIXME: It would be great to reduce these ~500 lines of code to just:
 *
 *      ssize_t need_len = snprintf(NULL, 0, msg, ap);
 *      if (need_len > 0) {
 *              new_ptr = realloc(buf->s, buf->size + need_len + 1);
 *              sprintf(new_ptr + buf->size, msg, ap);
 *              buf->s = new_ptr;
 *              buf->size += need_len;
 *      }
 *
 * The double-call is a lot of overhead, but my hand-coded implementation
 * below is probably not much faster, and not nearly as well debugged.
 * Using asprintf() would have only the overhead of realloc and strcat,
 * but it seems to be Gnu-only.  The hard part would be figuring out how
 * to build ap, since it's not called from a variadic function.
 *
 * Is this snprintf behavior (return proper count if size=0) standard? or
 * is it specific to just POSIX?
 */

static void
padwrite(struct string_writer_t *wr, int padc, size_t padlen)
{
        while (padlen--)
                string_writer_append(wr, padc);
}

/*
 * XXX: This looks redundant, but the alternative--writing to a
 * temporary buffer before deciding whether to right justify or not--
 * is maybe slower.  Needs testing, temporary buffers don't have the
 * overhead of buffer_putc.
 */
static void
swap_pad(struct string_writer_t *wr, size_t count, size_t padlen)
{
        size_t right = wr->pos_i - 1;
        size_t left = right - padlen;
        if (!wr->pos_i)
                return;

        while (count--) {
                long rpoint = string_writer_getidx(wr, right);
                long lpoint = string_writer_getidx(wr, left);
                bug_on(lpoint < 0 || rpoint < 0);
                string_writer_setidx(wr, right, lpoint);
                string_writer_setidx(wr, left, rpoint);
                left--;
                right--;
        }
}

static void
format2_i_helper(struct string_writer_t *wr,
                 unsigned long long ival, int base, int xchar)
{
        long long v;
        if (!ival)
                return;

        if (ival >= base)
                format2_i_helper(wr, ival / base, base, xchar);

        v = ival % base;
        if (v >= 10)
                v += xchar;
        else
                v += '0';

        string_writer_append(wr, (long)v);
}

static void
format2_i(struct string_writer_t *wr, Object *arg,
          int conv, bool rjust, int padc, size_t padlen, int precision)
{
        int base;
        size_t count;
        int xchar = 'A' - 10;
        long long ival = realvar_toint(arg);

        /* overrule '0' if left-justified */
        if (!rjust)
                padc = ' ';

        switch (conv) {
        case 'd':
        case 'u':
                base = 10;
                break;
        case 'x':
                xchar = 'a' - 10;
                /* fall through */
        case 'X':
                base = 16;
                break;
        default:
                bug();
        }

        count = wr->pos_i;
        if (!ival) {
                string_writer_append(wr, '0');
        } else {
                unsigned long long uval;
                if (conv == 'd' && ival < 0) {
                        string_writer_append(wr, '-');
                        uval = -ival;
                } else {
                        uval = (unsigned long long)ival;
                }
                format2_i_helper(wr, uval, base, xchar);
        }

        count = wr->pos_i - count;
        if (count < padlen) {
                padlen -= count;
                padwrite(wr, padc, padlen);
                if (rjust) {
                        swap_pad(wr, count, padlen);
                }
        }
}

/* helper to format2_e - print exponent */
static void
format2_e_exp(struct string_writer_t *wr, int exp)
{
        if (exp == 0)
                return;
        if (exp > 0)
                format2_e_exp(wr, exp / 10);
        string_writer_append(wr, (exp % 10) + '0');
}

/* FIXME: subtle difference from above, try to eliminate one of these */
static void
format2_f_ihelper(struct string_writer_t *wr, unsigned int v)
{
        if (v >= 10)
                format2_f_ihelper(wr, v / 10);
        string_writer_append(wr, (v % 10) + '0');
}

static void
format2_e(struct string_writer_t *wr, Object *arg,
          int conv, bool rjust, int padc, size_t padlen, int precision)
{
        int exp = 0;
        int sigfig = 0;
        double ival;
        /* checked before this call */
        double v = realvar_tod(arg);
        double dv = v;

        size_t count = wr->pos_i;

        if (dv < 0.0) {
                string_writer_append(wr, '-');
                dv = -dv;
        }

        while (dv > 10.0) {
                dv /= 10.0;
                exp++;
        }

        while (isnormal(dv) && dv < 1.0) {
                dv *= 10.0;
                exp--;
        }

        {
                /* precision rounding */
                double adj = 5.0;
                int pr = precision;
                while (pr--)
                        adj *= 0.1;
                dv += adj;
        }

        /* In case precision rounding carried all the way to the top */
        if (dv > 10.0) {
                dv /= 10.0;
                exp++;
        }

        dv = modf(dv, &ival);
        string_writer_append(wr, (int)ival + '0');
        ++sigfig;

        string_writer_append(wr, '.');
        while (sigfig < precision) {
                dv *= 10.0;
                dv = modf(dv, &ival);
                string_writer_append(wr, (int)ival + '0');
                sigfig++;
        }

        /* print exponent */
        bug_on(conv != 'e' && conv != 'E');
        string_writer_append(wr, conv);
        if (exp < 0) {
                string_writer_append(wr, '-');
                exp = -exp;
        } else {
                string_writer_append(wr, '+');
        }
        /* %e requires exponent to be at least two digits */
        if (exp < 10)
                string_writer_append(wr, '0');

        if (exp == 0)
                string_writer_append(wr, '0');
        else
                format2_e_exp(wr, exp);

        if (!rjust)
                padc = ' ';
        count = wr->pos_i - count;
        if (count < padlen) {
                padlen -= count;
                padwrite(wr, padc, padlen);
                if (rjust) {
                        swap_pad(wr, count, padlen);
                }
        }
}

static void
format2_f(struct string_writer_t *wr, Object *arg,
          int conv, bool rjust, int padc, size_t padlen, int precision)
{
        double v = realvar_tod(arg);
        bool have_dot = false;
        size_t count = wr->pos_i;

        if (!isfinite(v)) {
                if (isnan(v)) {
                        string_writer_appends(wr, "nan");
                } else {
                        if (v == -INFINITY)
                                string_writer_append(wr, '-');
                        string_writer_appends(wr, "inf");
                }
        } else {
                double iptr, rem, scale;
                int i;

                if (v < 0.0) {
                        string_writer_append(wr, '-');
                        v = -v;
                }
                for (scale = 1.0, i = 0; i < precision; i++)
                        scale *= 0.1;
                v += scale * 0.5;
                rem = modf(v, &iptr);

                format2_f_ihelper(wr, (unsigned int)iptr);

                if (precision > 0) {
                        have_dot = true;
                        string_writer_append(wr, '.');
                        while (precision--) {
                                rem *= 10.0;
                                string_writer_append(wr, (int)rem + '0');
                                rem = modf(rem, &iptr);
                        }
                }
        }

        if (!rjust && !have_dot)
                padc = ' ';
        count = wr->pos_i - count;
        if (count < padlen) {
                padlen -= count;
                padwrite(wr, padc, padlen);
                if (rjust) {
                        swap_pad(wr, count, padlen);
                }
        }
}

static void
format2_s(struct string_writer_t *wr, Object *arg,
          int conv, bool rjust, int padc, size_t padlen, int precision)
{
        /* count = #chars, not #bytes */
        size_t count = seqvar_size(arg);
        string_writer_append_strobj(wr, arg);

        if (count < padlen) {
                padlen -= count;
                padwrite(wr, padc, padlen);
                if (rjust)
                        swap_pad(wr, count, padlen);
        }
}

static size_t
format2_helper(Object *arg, struct string_writer_t *wr,
               Object *self, size_t self_i)
{
        size_t idx_save = self_i;
        bool rjust = true;
        int padc = ' ';
        size_t padlen = 0;
        int precision = 6;
        unsigned long point;
        size_t self_n = seqvar_size(self);

        /* get flags.  @cbuf already filled with next char */
        for (;;) {
                switch (string_getidx(self, self_i)) {
                case '-':
                        rjust = false;
                        self_i++;
                        continue;
                case '0':
                        padc = '0';
                        self_i++;
                        continue;
                }
                break;
        }
        point = string_getidx(self, self_i);
        if (point < 128 && isdigit(point)) {
                while (point < 128 && isdigit(point)) {
                        padlen = 10 * padlen + (point - '0');
                        if (self_i++ >= self_n)
                                return 0;
                        point = string_getidx(self, self_i);
                }
        }
        if (point == '.') {
                if (self_i++ >= self_n)
                        return 0;
                point = string_getidx(self, self_i);
                if (point < 128 && isdigit(point)) {
                        precision = 0;
                        while (point < 128 && isdigit(point)) {
                                precision *= 10;
                                precision += point - '0';
                                if (self_i++ >= self_n)
                                        return 0;
                                point = string_getidx(self, self_i);
                        }
                }
        }
        if (padlen >= PAD_MAX)
                padlen = PAD_MAX;
        if (precision >= PRECISION_MAX)
                precision = PRECISION_MAX;

        point = string_getidx(self, self_i);
        self_i++;
        switch (point) {
        case 'x':
        case 'X':
        case 'd':
        case 'u':
                if (!isvar_real(arg))
                        return 0;
                format2_i(wr, arg, point, rjust, padc, padlen, precision);
                break;
        case 'f':
                if (!isvar_real(arg))
                        return 0;
                format2_f(wr, arg, point, rjust, padc, padlen, precision);
                break;
        case 'e':
        case 'E':
                if (!isvar_real(arg))
                        return 0;
                format2_e(wr, arg, point, rjust, padc, padlen, precision);
                break;
        case 's':
                if (!isvar_string(arg))
                        return 0;
                format2_s(wr, arg, point, rjust, padc, padlen, precision);
                break;
        default:
        case '\0':
                /* don't try to format this */
                return 0;
        }

        return self_i - idx_save;
}

/* Common to string_format2 and string_modulo */
static Object *
string_printf(Object *self, Object *args, Object *kwargs)
{
        struct string_writer_t wr;
        size_t i, n, argi;

        n = seqvar_size(self);
        if (n == 0)
                return VAR_NEW_REF(self);

        argi = 0;
        string_writer_init(&wr, V2STR(self)->s_width);
        for (i = 0; i < n; i++) {
                unsigned long point = string_getidx(self, i);
                if (point == '%') {
                        Object *arg;
                        size_t ti;

                        if (i++ >= n)
                                break;

                        point = string_getidx(self, i);
                        if (point == '%') {
                                string_writer_append(&wr, '%');
                                continue;
                        }

                        if (point == '(') {
                                size_t start, stop;
                                Object *key;

                                if (!kwargs) {
                                        --i;
                                        continue;
                                }

                                if (i++ >= n)
                                        break;

                                start = i++;
                                while (i < n) {
                                        point = string_getidx(self, i);
                                        bug_on(point < 0);
                                        if (point == ')')
                                                break;
                                        i++;
                                }
                                /* XXX: maybe error instead */
                                if (i >= n)
                                        break;

                                stop = i;
                                key = stringvar_from_substr(self, start, stop);
                                arg = dict_getitem(kwargs, key);
                                VAR_DECR_REF(key);
                                if (!arg)
                                        continue;
                                /* skip closing ')' */
                                i++;
                        } else {
                                /* Numbered arg */
                                if (!args) {
                                        --i;
                                        continue;
                                }
                                arg = seqvar_getitem(args, argi++);
                                if (!arg)
                                        continue;
                        }

                        ti = format2_helper(arg, &wr, self, i);
                        if (!ti)
                                ti++;
                        /* minus one because of 'for' iterator */
                        i += ti - 1;
                        VAR_DECR_REF(arg);
                } else {
                        string_writer_append(&wr, point);
                }
        }

        return stringvar_from_writer(&wr);
}

/*
 * format2(...)         var args
 *
 * Lightweight printf-like alternative to format()
 *
 * Accepts %[(kwname){flags}{pad}.{precision}]{conversion}
 *      kwname: Surrounded by parentheses.  If used, next value will
 *              be from the keyword dictionary whose name is kwname,
 *              otherwise next value will be the next argument on the
 *              stack.
 *      flags:  - left-justify instead of default right-justify
 *              0 zero pad instead of default space pad, if permitted
 *                for conversion specifier & justification
 *      pad:    Base-10 number of characters to justify with
 *      precision:
 *              Base-10 number of significant figures, 6 by default
 *      conversion:
 *          if arg is TYPE_INT (or TYPE_FLOAT, converted to TYPE_INT)
 *              x Hexadecimal, lowercase
 *              X Hexadecimal, uppercase
 *              d Integer, signed
 *              u Integer, unsigned
 *          if arg is TYPE_FLOAT (or TYPE_INT, converted to TYPE_FLOAT)
 *              f [-]ddd.dddd notation
 *              e Exponential notation with lower-case e
 *              E Exponential notation with upper-case E
 *          if arg is TYPE_STRING
 *              s Insert arg string here.  pad is for Unicode characters,
 *                not necessarily bytes.
 */
static Object *
string_format2(Frame *fr)
{
        Object *args, *kwargs, *self;

        self = vm_get_this(fr);
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        args = vm_get_arg(fr, 0);
        bug_on(!args || !isvar_array(args));

        kwargs = vm_get_arg(fr, 1);
        bug_on(!kwargs || !isvar_dict(kwargs));

        return string_printf(self, args, kwargs);
}

/* **********************************************************************
 *           Built-in type props and methods (not format2)
 * *********************************************************************/

static Object *
string_getprop_length(Object *self)
{
        bug_on(!isvar_string(self));
        return intvar_new(STRING_LENGTH(self));
}

static Object *
string_getprop_nbytes(Object *self)
{
        bug_on(!isvar_string(self));
        return intvar_new(STRING_NBYTES(self));
}

static Object *
string_getprop_width(Object *self)
{
        bug_on(!isvar_string(self));
        return intvar_new(V2STR(self)->s_width);
}

static bool
string_format_helper(Object **args, const char **src,
                     struct buffer_t *t, int *lastarg,
                     size_t argc)
{
        const char *s = *src;
        int la = *lastarg;
        Object *q = NULL;
        ++s;
        if (*s == '}') {
                int i = la++;
                if (i < argc)
                        q = args[i];
        } else if (isdigit(*s)) {
                char *endptr;
                int i = strtoul(s, &endptr, 10);
                if (*endptr == '}') {
                        if (i < argc)
                                q = args[i];
                        la = i + 1;
                        s = endptr;
                }
        }
        if (!q)
                return false;

        if (isvar_string(q)) {
                buffer_puts(t, V2CSTR(q));
        } else {
                /* not a string, so we'll just use q's .str method. */
                Object *xpr = var_str(q);
                buffer_puts(t, V2CSTR(xpr));
                VAR_DECR_REF(xpr);
        }

        *lastarg = la;
        *src = s;
        return true;
}

/*
 * format(...)
 * returns type string
 */
static Object *
string_format(Frame *fr)
{
        struct buffer_t t;
        Object *self = vm_get_this(fr);
        Object *list = vm_get_arg(fr, 0);
        Object **args;
        int lastarg = 0;
        size_t list_size;
        const char *s, *self_s;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        bug_on(!isvar_array(list));
        list_size = seqvar_size(list);
        args = array_get_data(list);

        self_s = V2CSTR(self);
        if (!self_s)
                return stringvar_newf("", 0);

        buffer_init(&t);
        for (s = self_s; *s != '\0'; s++) {
                if (*s == '{' &&
                    string_format_helper(args, &s, &t, &lastarg, list_size)) {
                        continue;
                }
                buffer_putc(&t, *s);
        }

        Object *ret = stringvar_newf(buffer_trim(&t), 0);
        return ret;
}

#define STRIP_HELPER(X_, Type) \
static size_t \
strip_##X_(Type *src, size_t srclen, Type *skip,  \
           size_t skiplen, size_t width, unsigned int flags, \
           size_t *new_end) \
{ \
        size_t new_start = 0; \
        if (!(flags & SF_RIGHT)) { \
                size_t i, j; \
                for (i = 0; i < srclen; i++) { \
                        for (j = 0; j < skiplen; j++) { \
                                if (src[i] == skip[j]) \
                                        break; \
                        } \
                        if (j == skiplen) \
                                break; \
                } \
                new_start = i; \
        } \
        *new_end = srclen; \
        if (!!(flags & (SF_CENTER|SF_RIGHT))) { \
                size_t i, j; \
                for (i = srclen - 1; (ssize_t)i >= new_start; i--) { \
                        for (j = 0; j < skiplen; j++) { \
                                if (src[i] == skip[j]) \
                                        break; \
                        } \
                        if (j == skiplen) \
                                break; \
                } \
                *new_end = i + 1; \
        } \
        return new_start; \
}

STRIP_HELPER(8, uint8_t)
STRIP_HELPER(16, uint16_t)
STRIP_HELPER(32, uint32_t)

static Object *
string_lrstrip(Frame *fr, unsigned int flags)
{
        Object *self, *arg, *ret;
        void *src, *skip;
        size_t srclen, skiplen, width, src_newend, src_newstart;
        struct stringvar_t *vsrc, *vskip;

        self = vm_get_this(fr);
        arg = vm_get_arg(fr, 0);

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        if (arg && arg_type_check(arg, &StringType) == RES_ERROR)
                return ErrorVar;

        /* no need to produce a reference, we're just borrowing */
        if (!arg)
                arg = STRCONST_ID(wtspc);

        vsrc = V2STR(self);
        vskip = V2STR(arg);

        if (vskip->s_width < vsrc->s_width) {
                skip = widen_buffer(arg, vsrc->s_width);
                src = vsrc->s_unicode;;
                width = vsrc->s_width;
        } else if (vskip->s_width > vsrc->s_width) {
                skip = vskip->s_unicode;
                src = widen_buffer(self, vskip->s_width);
                width = vskip->s_width;
        } else {
                skip = vskip->s_unicode;
                src = vsrc->s_unicode;
                width = vsrc->s_width;
        }
        srclen = seqvar_size(self);
        skiplen = seqvar_size(arg);

        if (width == 4) {
                src_newstart = strip_32(src, srclen, skip, skiplen,
                                        width, flags, &src_newend);
        } else if (width == 2) {
                src_newstart = strip_16(src, srclen, skip, skiplen,
                                        width, flags, &src_newend);
        } else {
                bug_on(width != 1);
                src_newstart = strip_8(src, srclen, skip, skiplen,
                                       width, flags, &src_newend);
        }
        bug_on(src_newstart > src_newend);
        if (src_newstart == src_newend) {
                ret = VAR_NEW_REF(STRCONST_ID(mpty));
        } else {
                /* Use original buffers, in case we had widened them. */
                void *newp = vsrc->s_unicode + vsrc->s_width * src_newstart;
                ret = stringvar_from_points(newp, vsrc->s_width,
                                            src_newend - src_newstart, SF_COPY);
        }
        if (src != vsrc->s_unicode)
                efree(src);
        if (skip != vskip->s_unicode)
                efree(skip);
        return ret;
}

/*
 * lstrip()             no args implies whitespace
 * lstrip(charset)      charset is string
 */
static Object *
string_lstrip(Frame *fr)
{
        return string_lrstrip(fr, 0);
}

/*
 * rstrip()             no args implies whitespace
 * rstrip(charset)      charset is string
 */
static Object *
string_rstrip(Frame *fr)
{
        return string_lrstrip(fr, SF_RIGHT);
}

/*
 *  strip()             no args implies whitespace
 *  strip(charset)      charset is string
 */
static Object *
string_strip(Frame *fr)
{
        return string_lrstrip(fr, SF_CENTER);
}

static Object *
string_replace(Frame *fr)
{
        struct buffer_t b;
        Object *self    = get_this(fr);
        Object *vneedle = frame_get_arg(fr, 0);
        Object *vrepl   = frame_get_arg(fr, 1);
        const char *haystack, *needle, *end;
        size_t needle_len;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(vneedle, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(vrepl, &StringType) == RES_ERROR)
                return ErrorVar;

        buffer_init(&b);

        /* end not technically needed, but in case of match() bugs */
        haystack = V2CSTR(self);
        end = haystack + STRING_LENGTH(self);
        needle = V2CSTR(vneedle);
        needle_len = STRING_LENGTH(vneedle);

        if (!haystack || end == haystack) {
                buffer_putc(&b, '\0');
                goto done;
        }

        if (!needle || !needle_len) {
                buffer_puts(&b, V2CSTR(self));
                goto done;
        }

        while (*haystack && haystack < end) {
                ssize_t size = match(needle, haystack);
                if (size == -1)
                         break;
                buffer_nputs(&b, haystack, size);
                buffer_puts(&b, V2CSTR(vrepl));
                haystack += size + needle_len;
        }
        bug_on(haystack > end);
        if (*haystack != '\0')
                buffer_puts(&b, haystack);
done:
        return stringvar_newf(buffer_trim(&b), 0);
}

static Object *
string_lrjust(Frame *fr, unsigned int flags)
{
        Object *self, *arg;
        struct string_writer_t wr;
        ssize_t newlen, selflen, padlen;

        bug_on((flags & (SF_CENTER|SF_RIGHT)) == (SF_CENTER|SF_RIGHT));

        self = vm_get_this(fr);
        arg = vm_get_arg(fr, 0);
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        arg = vm_get_arg(fr, 0);
        if (arg_type_check(arg, &IntType) == RES_ERROR)
                return ErrorVar;

        newlen = intvar_toi(arg);
        if (err_occurred())
                return ErrorVar;

        selflen = seqvar_size(self);
        if (newlen < selflen)
                newlen = selflen;
        padlen = newlen - selflen;
        if (!!(flags & SF_CENTER))
                padlen /= 2;

        if (!newlen)
                return VAR_NEW_REF(STRCONST_ID(mpty));

        if (newlen == selflen)
                return VAR_NEW_REF(self);

        string_writer_init(&wr, V2STR(self)->s_width);
        if (!!(flags & (SF_CENTER | SF_RIGHT))) {
                while (padlen-- > 0)
                        string_writer_append(&wr, ' ');
        }
        string_writer_append_strobj(&wr, self);
        bug_on(wr.pos_i < newlen && !!(flags & SF_RIGHT));
        while (wr.pos_i < newlen) {
                string_writer_append(&wr, ' ');
        }
        return stringvar_from_writer(&wr);
}

/* rjust(amt)   integer arg */
static Object *
string_rjust(Frame *fr)
{
        return string_lrjust(fr, SF_RIGHT);
}

/* rjust(amt)    integer arg */
static Object *
string_ljust(Frame *fr)
{
        return string_lrjust(fr, 0);
}

static Object *
string_join(Frame *fr)
{
        struct string_writer_t wr;
        Object *self = get_this(fr);
        Object *arg = vm_get_arg(fr, 0);
        size_t i, n, width;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        if (!arg || !isvar_seq_readable(arg)) {
                err_setstr(ArgumentError, "Expected: sequential object");
                return ErrorVar;
        }

        if ((n = seqvar_size(arg)) == 0)
                return VAR_NEW_REF(STRCONST_ID(mpty));

        if (n == 1)
                return seqvar_getitem(arg, 0);

        width = V2STR(self)->s_width;
        if (!isvar_string(arg)) {
                bool have_joinstr = seqvar_size(self) > 0;
                for (i = 0; i < n; i++) {
                        Object *elem = seqvar_getitem(arg, i);
                        size_t twid;
                        bug_on(!elem);
                        if (!isvar_string(elem)) {
                                VAR_DECR_REF(elem);
                                err_setstr(TypeError,
                                        "Expected string type in sequence but found %s",
                                        typestr(elem));
                                return ErrorVar;
                        }
                        twid = V2STR(elem)->s_width;
                        if (width < twid)
                                width = twid;
                        VAR_DECR_REF(elem);
                }
                string_writer_init(&wr, width);
                for (i = 0; i < n; i++) {
                        Object *elem = seqvar_getitem(arg, i);
                        bug_on(!elem || !isvar_string(elem));
                        if (i > 0 && have_joinstr)
                                string_writer_append_strobj(&wr, self);
                        string_writer_append_strobj(&wr, elem);
                        VAR_DECR_REF(elem);
                }
        } else {
                /*
                 * For strings, the above method would add the overhead
                 * of creating/destroying a string object for each
                 * seqvar_getitem() call, so do a manual version here.
                 */

                /* Result is arg with nothing between its letters */
                if (seqvar_size(self) == 0)
                        return VAR_NEW_REF(arg);

                if (width < V2STR(arg)->s_width)
                        width = V2STR(arg)->s_width;
                string_writer_init(&wr, width);
                for (i = 0; i < n; i++) {
                        long point = string_getidx(arg, i);
                        bug_on(point < 0L);
                        if (i > 0)
                                string_writer_append_strobj(&wr, self);
                        string_writer_append(&wr, point);
                }
        }

        return stringvar_from_writer(&wr);
}

static Object *
string_capitalize(Frame *fr)
{
        Object *self = vm_get_this(fr);
        const char *src;
        char *dst;
        char *newbuf;
        int c;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        src = string_cstring(self);
        newbuf = dst = emalloc(STRING_NBYTES(self) + 1);

        if ((c = *src++) != '\0')
                *dst++ = isascii(c) ? toupper(c) : c;

        while ((c = *src++) != '\0')
                *dst++ = isascii(c) ? tolower(c) : c;

        *dst = '\0';
        return stringvar_newf(newbuf, SF_COPY);
}

static Object *
string_center(Frame *fr)
{
        return string_lrjust(fr, SF_CENTER);
}

static Object *
string_count(Frame *fr)
{
        int count;
        Object *self = vm_get_this(fr);
        Object *arg = vm_get_arg(fr, 0);

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(arg, &StringType) == RES_ERROR)
                return ErrorVar;

        /* XXX: Consistent to do with encoded C strings? */
        count = memcount(string_cstring(self), STRING_NBYTES(self),
                         string_cstring(arg), STRING_NBYTES(arg));

        return count ? intvar_new(count) : VAR_NEW_REF(gbl.zero);
}

static Object *
string_starts_or_ends_with(Frame *fr, unsigned int flags)
{
        Object *self, *arg, *ret;

        /* TODO: optional start, stop args */
        const char *needle, *haystack;
        int hasat, nlen, hlen;

        self = vm_get_this(fr);
        arg = vm_get_arg(fr, 0);
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(arg, &StringType) == RES_ERROR)
                return ErrorVar;

        needle = string_cstring(arg);
        haystack = string_cstring(self);
        nlen = STRING_NBYTES(arg);
        hlen = STRING_NBYTES(self);

        if (nlen > hlen) {
                hasat = 0;
        } else {
                int idx = !!(flags & SF_RIGHT) ? hlen - nlen : 0;
                hasat = (strncmp(&haystack[idx], needle, nlen) == 0);
        }

        ret = hasat ? gbl.one : gbl.zero;
        VAR_INCR_REF(ret);
        return ret;
}

static Object *
string_endswith(Frame *fr)
{
        return string_starts_or_ends_with(fr, SF_RIGHT);
}

static Object *
string_startswith(Frame *fr)
{
        return string_starts_or_ends_with(fr, 0);
}

static Object *
string_expandtabs(Frame *fr)
{
        Object *self, *kw, *tabarg;
        size_t tabsize, col, nextstop;
        struct string_writer_t wr;
        size_t i, n;

        self = vm_get_this(fr);
        kw = vm_get_arg(fr, 0);
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        bug_on(!kw || !isvar_dict(kw));
        dict_unpack(kw, STRCONST_ID(tabsize), &tabarg, gbl.eight, NULL);
        if (arg_type_check(tabarg, &IntType) == RES_ERROR) {
                VAR_DECR_REF(tabarg);
                return ErrorVar;
        }
        tabsize = intvar_toi(tabarg);
        VAR_DECR_REF(tabarg);

        if (err_occurred())
                return ErrorVar;

        if (tabsize < 0)
                tabsize = 0;

        string_writer_init(&wr, V2STR(self)->s_width);
        col = 0;
        nextstop = tabsize;

        n = seqvar_size(self);
        for (i = 0; i < n; i++) {
                long c = string_getidx(self, i);
                bug_on(c < 0L);
                if (c == '\n') {
                        col = 0;
                        nextstop = tabsize;
                        string_writer_append(&wr, c);
                } else if (c == '\t') {
                        if (col == nextstop)
                                nextstop += tabsize;
                        while (col < nextstop) {
                                string_writer_append(&wr, ' ');
                                col++;
                        }
                        nextstop += tabsize;
                } else {
                        if (col == nextstop)
                                nextstop += tabsize;
                        string_writer_append(&wr, c);
                        col++;
                }
        }
        return stringvar_from_writer(&wr);
}

static Object *
string_index_or_find(Frame *fr, unsigned int flags)
{
        Object *self = vm_get_this(fr);
        Object *arg = vm_get_arg(fr, 0);
        ssize_t res;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(arg, &StringType) == RES_ERROR)
                return ErrorVar;

        res = find_idx(self, arg, flags);
        if (res < 0) {
                if (!(flags & SF_SUPPRESS)) {
                        err_setstr(ValueError, "substring not found");
                        return ErrorVar;
                }
                return VAR_NEW_REF(gbl.neg_one);
        }
        return res ? intvar_new(res) : VAR_NEW_REF(gbl.zero);
}

static Object *
string_find(Frame *fr)
{
        return string_index_or_find(fr, SF_SUPPRESS);
}

static Object *
string_index(Frame *fr)
{
        return string_index_or_find(fr, 0);
}

static Object *
string_rfind(Frame *fr)
{
        return string_index_or_find(fr, SF_SUPPRESS | SF_RIGHT);
}

static Object *
string_rindex(Frame *fr)
{
        return string_index_or_find(fr, SF_RIGHT);
}

static Object *
string_lrpartition(Frame *fr, unsigned int flags)
{
        Object *self, *arg, *tup, **td;
        ssize_t idx;

        self = vm_get_this(fr);
        arg = vm_get_arg(fr, 0);

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(arg, &StringType) == RES_ERROR)
                return ErrorVar;

        if (seqvar_size(arg) == 0) {
                err_setstr(ValueError, "Separator may not be empty");
                return ErrorVar;
        }

        idx = find_idx(self, arg, flags);

        tup = tuplevar_new(3);
        td = tuple_get_data(tup);
        VAR_DECR_REF(td[0]);
        VAR_DECR_REF(td[1]);
        VAR_DECR_REF(td[2]);
        if (idx < 0) {
                td[0] = VAR_NEW_REF(self);
                td[1] = VAR_NEW_REF(STRCONST_ID(mpty));
                td[2] = VAR_NEW_REF(STRCONST_ID(mpty));
        } else {
                size_t wid = V2STR(self)->s_width;
                void *points = V2STR(self)->s_unicode;
                if (idx == 0) {
                        td[0] = VAR_NEW_REF(STRCONST_ID(mpty));
                } else {
                        td[0] = stringvar_from_points(points, wid,
                                                      idx, SF_COPY);
                }

                td[1] = VAR_NEW_REF(arg);

                idx += seqvar_size(arg);
                if (idx == seqvar_size(self)) {
                        td[2] = VAR_NEW_REF(STRCONST_ID(mpty));
                } else {
                        size_t len = seqvar_size(self) - idx;
                        points += idx * wid;
                        td[2] = stringvar_from_points(points, wid,
                                                      len, SF_COPY);
                }
        }
        return tup;
}

static Object *
string_partition(Frame *fr)
{
        return string_lrpartition(fr, 0);
}

static Object *
string_rpartition(Frame *fr)
{
        return string_lrpartition(fr, SF_RIGHT);
}

static Object *
string_removelr(Frame *fr, unsigned int flags)
{
        Object *self, *arg;
        const char *haystack, *needle;
        unsigned int idx, nlen, hlen;
        char *newbuf;

        self = vm_get_this(fr);
        arg = vm_get_arg(fr, 0);

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(arg, &StringType) == RES_ERROR)
                return ErrorVar;

        nlen = STRING_NBYTES(arg);
        hlen = STRING_NBYTES(self);
        haystack = string_cstring(self);
        needle = string_cstring(arg);

        if (nlen > hlen)
                goto return_self;

        idx = !!(flags & SF_RIGHT) ? hlen - nlen : 0;
        if (strncmp(&haystack[idx], needle, nlen) != 0)
                goto return_self;

        newbuf = emalloc(hlen + 1 - nlen);
        if (!!(flags & SF_RIGHT))
                memcpy(newbuf, haystack, hlen - nlen);
        else
                memcpy(newbuf, &haystack[nlen], hlen - nlen);
        newbuf[hlen - nlen] = '\0';
        return stringvar_newf(newbuf, 0);

return_self:
        VAR_INCR_REF(self);
        return self;
}

static Object *
string_removeprefix(Frame *fr)
{
        return string_removelr(fr, 0);
}

static Object *
string_removesuffix(Frame *fr)
{
        return string_removelr(fr, SF_RIGHT);
}

/* helper to string_lrsplit */
static void
append_str_or_empty(Object *arr, const char *s)
{
        if (*s) {
                Object *tmp = stringvar_newf((char *)s, SF_COPY);
                array_append(arr, tmp);
                VAR_DECR_REF(tmp);
        } else {
                array_append(arr, STRCONST_ID(mpty));
        }
}

static Object *
string_lrsplit(Frame *fr, unsigned int flags)
{
        enum { LRSPLIT_STACK_SIZE = 64, };
        Object *self = vm_get_this(fr);
        Object *kw = vm_get_arg(fr, 0);
        Object *separg, *maxarg;
        Object *ret;
        char newsrc_stack[LRSPLIT_STACK_SIZE];
        const char *sep;
        char *newsrc;
        int maxsplit, seplen;
        bool combine = false;

        bug_on(!self || !isvar_string(self));
        bug_on(!kw || !isvar_dict(kw));

        dict_unpack(kw,
                    STRCONST_ID(sep), &separg, NullVar,
                    STRCONST_ID(maxsplit), &maxarg, gbl.neg_one,
                    NULL);

        if (separg == NullVar) {
                combine = true;
                VAR_DECR_REF(separg);
                separg = STRCONST_ID(spc);
                VAR_INCR_REF(separg);
        }

        if (arg_type_check(separg, &StringType) == RES_ERROR) {
                ret = ErrorVar;
                goto out;
        }
        if (seqvar_size(separg) == 0) {
                ret = ErrorVar;
                err_setstr(ValueError, "Separator may not be empty");
                goto out;
        }
        maxsplit = intvar_toi(maxarg);
        if (err_occurred()) {
                ret = ErrorVar;
                goto out;
        }
        sep = string_cstring(separg);
        seplen = STRING_NBYTES(separg);
        /*
         * FIXME: Since we have stringvar_newn now,
         * we don't need to do this anymore.
         */
        if (STRING_NBYTES(self) + 1 >= LRSPLIT_STACK_SIZE) {
                newsrc = estrdup(string_cstring(self));
        } else {
                newsrc = newsrc_stack;
                strcpy(newsrc, string_cstring(self));
        }
        ret = arrayvar_new(0);
        if (!!(flags & SF_RIGHT)) {
                while (maxsplit != 0) {
                        char *s;
                        maxsplit--;

                        s = strrstr(newsrc, sep);
                        if (!s)
                                break;

                        memset(s, 0, seplen);
                        append_str_or_empty(ret, s + seplen);

                        while (combine && ((int)(s - newsrc) >= seplen)
                               && memcmp(s - seplen, sep, seplen) == 0) {
                                s -= seplen;
                                memset(s, 0, seplen);
                        }
                }
                append_str_or_empty(ret, newsrc);
                array_reverse(ret);
        } else {
                char *tsrc = newsrc;
                while (maxsplit != 0) {
                        char *s;
                        maxsplit--;

                        s = strstr(tsrc, sep);
                        if (!s)
                                break;

                        memset(s, 0, seplen);
                        append_str_or_empty(ret, tsrc);
                        tsrc = s + seplen;
                        while (combine && memcmp(tsrc, sep, seplen) == 0)
                                tsrc += seplen;
                }

                append_str_or_empty(ret, tsrc);
        }
        if (newsrc != newsrc_stack)
                efree(newsrc);

out:
        VAR_DECR_REF(separg);
        VAR_DECR_REF(maxarg);
        return ret;
}

static Object *
string_rsplit(Frame *fr)
{
        return string_lrsplit(fr, SF_RIGHT);
}

static Object *
string_split(Frame *fr)
{
        return string_lrsplit(fr, 0);
}

#define EOLCHARSET "\r\n"

static Object *
string_splitlines(Frame *fr)
{
        Object *self, *kw, *keeparg, *ret;
        int keepends;
        const char *src;

        self = vm_get_this(fr);
        kw = vm_get_arg(fr, 0);
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        bug_on(!kw || !isvar_dict(kw));
        dict_unpack(kw, STRCONST_ID(keepends), &keeparg, gbl.zero, NULL);
        if (arg_type_check(keeparg, &IntType) == RES_ERROR) {
                ret = ErrorVar;
                goto out;
        }
        keepends = intvar_toll(keeparg);
        src = string_cstring(self);
        ret = arrayvar_new(0);
        while (*src != '\0') {
                size_t n;
                size_t nexti, nli;
                nli = strcspn(src, EOLCHARSET);
                if (src[nli] != '\0') {
                        const char *eol = src + nli;
                        switch (*eol) {
                        case '\r':
                                eol++;
                                if (*eol == '\n')
                                        eol++;
                                break;
                        case '\n':
                                eol++;
                                break;
                        default:
                                bug();
                        }
                        nexti = eol - src;
                } else {
                        nexti = nli;
                }
                n = keepends ? nexti : nli;
                if (n) {
                        Object *tmp = stringvar_newn(src, n);
                        array_append(ret, tmp);
                        VAR_DECR_REF(tmp);
                } else {
                        array_append(ret, STRCONST_ID(mpty));
                }
                src += nexti;
        }

out:
        VAR_DECR_REF(keeparg);
        return ret;
}

#undef EOLCHARSET

static Object *
string_zfill(Frame *fr)
{
        Object *self, *arg;
        const char *src;
        int nz;
        struct buffer_t b;

        self = vm_get_this(fr);
        arg = vm_get_arg(fr, 0);

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(arg, &IntType) == RES_ERROR)
                return ErrorVar;

        nz = intvar_toi(arg);
        if (err_occurred())
                return ErrorVar;
        nz -= seqvar_size(self);

        src = string_cstring(self);
        buffer_init(&b);
        if (*src == '-' || *src == '+') {
                buffer_putc(&b, *src);
                src++;
        }

        while (nz-- > 0)
                buffer_putc(&b, '0');
        buffer_puts(&b, src);

        return stringvar_newf(buffer_trim(&b), 0);
}


/*
 *      str.isXXXX() functions and helpers
 */

static Object *
string_is1(Frame *fr, bool (*cb)(Object *))
{
        Object *ret;
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        if (seqvar_size(self) == 0) {
                ret = gbl.zero;
        } else {
                ret = cb(self) ? gbl.one : gbl.zero;
        }
        VAR_INCR_REF(ret);
        return ret;
}

static Object *
string_is2(Frame *fr, bool (*tst)(unsigned int c))
{
        Object *ret;
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        /* To be overwritten if false */
        ret = gbl.one;
        if (seqvar_size(self) == 0) {
                ret = gbl.zero;
        } else {
                size_t i, n = seqvar_size(self);
                for (i = 0; i < n; i++) {
                        long point = string_getidx(self, i);
                        if (!tst(point)) {
                                ret = gbl.zero;
                                break;
                        }
                }
        }
        VAR_INCR_REF(ret);
        return ret;
}

static bool is_alnum(unsigned int c) { return c < 128 && isalnum(c); }
static bool is_alpha(unsigned int c) { return c < 128 && isalpha(c); }
static bool is_digit(unsigned int c) { return c < 128 && isdigit(c); }
static bool is_printable(unsigned int c) { return c < 128 && isprint(c); }
static bool is_space(unsigned int c) { return c < 128 && isspace(c); }
static bool is_upper(unsigned int c) { return c < 128 && isupper(c); }
static bool is_lower(unsigned int c) { return c < 128 && islower(c); }

static bool
is_ident(Object *str)
{
        size_t i, n;
        long point;
        bug_on(!isvar_string(str));

        n = seqvar_size(str);
        bug_on(n == 0);

        point = string_getidx(str, 0);
        if (point != '_' && !is_alpha(point))
                return false;

        for (i = 1; i < n; i++) {
                point = string_getidx(str, i);
                if (!is_alnum(point) && point != '_')
                        return false;
        }
        return true;
}

static bool
is_title(Object *str)
{
        bool first = true;
        size_t i, n = seqvar_size(str);
        bug_on(n == 0);
        for (i = 0; i < n; i++) {
                long point = string_getidx(str, i);
                if (!is_alpha(point)) {
                        first = true;
                } else if (first) {
                        if (is_lower(point))
                                return false;
                        first = false;
                }
        }
        return true;
}

static Object *string_isident(Frame *fr)
        { return string_is1(fr, is_ident); }

static Object *string_istitle(Frame *fr)
        { return string_is1(fr, is_title); }

static Object *string_isalnum(Frame *fr)
        { return string_is2(fr, is_alnum); }

static Object *string_isalpha(Frame *fr)
        { return string_is2(fr, is_alpha); }

/* FIXME: Skip the scan, just return V2STR(self)->s_ascii. */
/* named funny, because string_isascii is an API func */
static Object *
string_isascii_mthd(Frame *fr)
{
        Object *self = vm_get_this(fr);
        bool ascii;
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        ascii = V2STR(self)->s_ascii;
        return ascii ? VAR_NEW_REF(gbl.one) : VAR_NEW_REF(gbl.zero);
}

static Object *string_isdigit(Frame *fr)
        { return string_is2(fr, is_digit); }

static Object *string_isprintable(Frame *fr)
        { return string_is2(fr, is_printable); }

static Object *string_isspace(Frame *fr)
        { return string_is2(fr, is_space); }

static Object *string_isupper(Frame *fr)
        { return string_is2(fr, is_upper); }

/*
 * string case-swapping & helpers
 */
static Object *
string_title(Frame *fr)
{
        Object *self;
        const char *src;
        char *dst, *newbuf;
        unsigned int c;
        bool first;

        self = vm_get_this(fr);
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        src = string_cstring(self);
        dst = newbuf = emalloc(STRING_NBYTES(self) + 1);
        first = true;
        while ((c = *src++) != '\0') {
                if (c < 128 && isalpha(c)) {
                        if (first) {
                                c = toupper(c);
                                first = false;
                        } else {
                                c = tolower(c);
                        }
                } else {
                        first = true;
                }
                *dst++ = c;
        }

        *dst = '\0';
        return stringvar_newf(newbuf, 0);
}

static Object *
string_to(Frame *fr, int (*cb)(unsigned int))
{
        Object *self;
        const char *src;
        char *dst, *newbuf;
        unsigned int c;

        self = vm_get_this(fr);
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        src = string_cstring(self);
        dst = newbuf = emalloc(STRING_NBYTES(self) + 1);
        while ((c = *src++) != '\0')
                *dst++ = cb(c);

        *dst = '\0';
        return stringvar_newf(newbuf, 0);
}

static int to_lower(unsigned int c)
        { return c < 128 ? tolower(c) : c; }
static int to_upper(unsigned int c)
        { return c < 128 ? toupper(c) : c; }
static int
to_swap(unsigned int c)
{
        if (c < 128) {
                if (isupper(c))
                        c = tolower(c);
                else if (islower(c))
                        c = toupper(c);
        }
        return c;
}

static Object *
string_lower(Frame *fr)
{
        return string_to(fr, to_lower);
}

static Object *
string_swapcase(Frame *fr)
{
        return string_to(fr, to_swap);
}

static Object *
string_upper(Frame *fr)
{
        return string_to(fr, to_upper);
}

static struct type_inittbl_t string_methods[] = {
        V_INITTBL("capitalize",   string_capitalize,   0, 0, -1, -1),
        V_INITTBL("center",       string_center,       1, 1, -1, -1),
        V_INITTBL("count",        string_count,        1, 1, -1, -1),
        V_INITTBL("endswith",     string_endswith,     1, 1, -1, -1),
        V_INITTBL("expandtabs",   string_expandtabs,   1, 1, -1,  0),
        V_INITTBL("find",         string_find,         1, 1, -1, -1),
        V_INITTBL("format",       string_format,       1, 1,  0, -1),
        V_INITTBL("format2",      string_format2,      2, 2,  0,  1),
        V_INITTBL("index",        string_index,        1, 1, -1, -1),
        V_INITTBL("isalnum",      string_isalnum,      0, 0, -1, -1),
        V_INITTBL("isalpha",      string_isalpha,      0, 0, -1, -1),
        V_INITTBL("isascii",      string_isascii_mthd, 0, 0, -1, -1),
        V_INITTBL("isdigit",      string_isdigit,      0, 0, -1, -1),
        V_INITTBL("isident",      string_isident,      0, 0, -1, -1),
        V_INITTBL("isprintable",  string_isprintable,  0, 0, -1, -1),
        V_INITTBL("isspace",      string_isspace,      0, 0, -1, -1),
        V_INITTBL("istitle",      string_istitle,      0, 0, -1, -1),
        V_INITTBL("isupper",      string_isupper,      0, 0, -1, -1),
        V_INITTBL("join",         string_join,         1, 1, -1, -1),
        V_INITTBL("ljust",        string_ljust,        1, 1, -1, -1),
        V_INITTBL("lower",        string_lower,        0, 0, -1, -1),
        V_INITTBL("lstrip",       string_lstrip,       0, 1, -1, -1),
        V_INITTBL("partition",    string_partition,    1, 1, -1, -1),
        V_INITTBL("removeprefix", string_removeprefix, 1, 1, -1, -1),
        V_INITTBL("removesuffix", string_removesuffix, 1, 1, -1, -1),
        V_INITTBL("replace",      string_replace,      2, 2, -1, -1),
        V_INITTBL("rfind",        string_rfind,        1, 1, -1, -1),
        V_INITTBL("rindex",       string_rindex,       1, 1, -1, -1),
        V_INITTBL("rjust",        string_rjust,        1, 1, -1, -1),
        V_INITTBL("rpartition",   string_rpartition,   1, 1, -1, -1),
        V_INITTBL("rsplit",       string_rsplit,       1, 1, -1,  0),
        V_INITTBL("rstrip",       string_rstrip,       0, 1, -1, -1),
        V_INITTBL("split",        string_split,        1, 1, -1,  0),
        V_INITTBL("splitlines",   string_splitlines,   1, 1, -1,  0),
        V_INITTBL("startswith",   string_startswith,   1, 1, -1, -1),
        V_INITTBL("strip",        string_strip,        0, 1, -1, -1),
        V_INITTBL("swapcase",     string_swapcase,     0, 0, -1, -1),
        V_INITTBL("title",        string_title,        0, 0, -1, -1),
        V_INITTBL("upper",        string_upper,        0, 0, -1, -1),
        V_INITTBL("zfill",        string_zfill,        1, 1, -1, -1),

        TBLEND,
};

/* **********************************************************************
 *                      Operator Methods
 * *********************************************************************/

/*
 * FIXME: crud, this is a DRY violation with print_escapestr in helpers.c,
 * but we could only use that one if we use mktemp or something crazy
 * like that.
 */
static Object *
string_str(Object *v)
{
        struct buffer_t b;
        size_t i, n;
        enum { Q = '\'', BKSL = '\\' };

        bug_on(!isvar_string(v));

        buffer_init(&b);

        buffer_putc(&b, Q);
        n = seqvar_size(v);
        for (i = 0; i < n; i++) {
                long c = string_getidx(v, i);
                bug_on(c < 0L);
                if (c == Q) {
                        buffer_putc(&b, BKSL);
                        buffer_putc(&b, Q);
                } else if (c == BKSL) {
                        buffer_putc(&b, BKSL);
                        buffer_putc(&b, BKSL);
                } else if (c < 128 && isspace(c)) {
                        switch (c) {
                        case ' ': /* this one's ok */
                                buffer_putc(&b, c);
                                continue;
                        case '\n':
                                c = 'n';
                                break;
                        case '\t':
                                c = 't';
                                break;
                        case '\v':
                                c = 'v';
                                break;
                        case '\f':
                                c = 'f';
                                break;
                        case '\r':
                                c = 'r';
                                break;
                        }
                        buffer_putc(&b, BKSL);
                        buffer_putc(&b, c);
                } else if (c < 128 && !isgraph(c)) {
                        buffer_putc(&b, BKSL);
                        buffer_putc(&b, ((c >> 6) & 0x03) + '0');
                        buffer_putc(&b, ((c >> 3) & 0x07) + '0');
                        buffer_putc(&b, (c & 0x07) + '0');
                } else if (c > 128) {
                        char buf[10];
                        buffer_putc(&b, BKSL);
                        if (c > 0xffffu) {
                                bug_on(!utf8_valid_unicode(c));
                                buffer_putc(&b, 'U');
                                sprintf(buf, "%08x", (int)c);
                                buffer_puts(&b, buf);
                        } else if (c > 0xffu) {
                                buffer_putc(&b, 'u');
                                sprintf(buf, "%04x", (int)c);
                                buffer_puts(&b, buf);
                        } else {
                                buffer_putc(&b, ((c >> 6) & 0x03) + '0');
                                buffer_putc(&b, ((c >> 3) & 0x07) + '0');
                                buffer_putc(&b, (c & 0x07) + '0');
                        }
                } else {
                        buffer_putc(&b, c);
                }
        }
        buffer_putc(&b, Q);
        return stringvar_newf(buffer_trim(&b), 0);
}

static void
string_reset(Object *str)
{
        struct stringvar_t *vs = V2STR(str);
        if (vs->s_unicode != vs->s && vs->s_unicode != NULL)
                efree(vs->s_unicode);
        efree(vs->s);
}

static Object *
string_cat(Object *a, Object *b)
{
        char *catstr;
        const char *lval, *rval;
        size_t rlen, llen;

        if (!b)
                return stringvar_new("");

        if (!isvar_string(b)) {
                err_setstr(TypeError,
                           "Mismatched types for + operation");
                return NULL;
        }

        /*
         * XXX REVISIT: Way less verification to do if we concatenate
         * Unicode arrays instead of the C-strings, probably faster.
         */
        lval = V2CSTR(a);
        llen = STRING_NBYTES(a);

        rval = V2CSTR(b);
        rlen = STRING_NBYTES(b);

        catstr = emalloc(llen + rlen + 1);
        memcpy(catstr, lval, llen);
        memcpy(catstr + llen, rval, rlen + 1);
        return stringvar_newf(catstr, 0);
}

static int
string_cmp(Object *a, Object *b)
{
        const char *sa, *sb;
        /*
         * Compare the C strings, not the Unicode buffers.  Some corner
         * cases exist where a string produced from a built-in method
         * will result in a new string whose width is wider than it needs
         * to be, therefore the memcmp on the Unicode buffers could fail
         * even for strings with all-matching Unicode points.
         */
        bug_on(!isvar_string(a) || !isvar_string(b));
        sa = string_cstring(a);
        sb = string_cstring(b);
        if (!sa || !sb)
                return sa != sb;
        return strcmp(sa, sb);
}

static bool
string_cmpz(Object *a)
{
        const char *s = V2CSTR(a);
        /* treat "" same as NULL in comparisons */
        return s ? s[0] == '\0' : true;
}

static Object *
string_modulo(Object *str, Object *arg)
{
        bug_on(!isvar_string(str));
        if (isvar_dict(arg)) {
                return string_printf(str, NULL, arg);
        } else if (isvar_tuple(arg) || isvar_array(arg)) {
                return string_printf(str, arg, NULL);
        } else {
                err_setstr(TypeError,
                           "'x' in str %% x must be a list, tuple, or dictionary");
                return ErrorVar;
        }
}

/* comparisons, helpers to string_getslice */
static bool slice_cmp_lt(int a, int b) { return a < b; }
static bool slice_cmp_gt(int a, int b) { return a > b; }

static Object *
string_getslice(Object *str, int start, int stop, int step)
{
        struct string_writer_t wr;
        bool (*cmp)(int, int);

        if (start == stop)
                return stringvar_new("");

        /*
         * XXX REVISIT: This assumes it's better to start with width=1,
         * even if V2STR(str)->s_width > 1, because the >1 non-ASCII
         * chars are rare enough that we'll likely miss them in a slice,
         * therefore the RAM saved outweighs the overhead of an an
         * occasional "oops, we need to resize."
         */
        string_writer_init(&wr, 1);
        cmp = (start < stop) ? slice_cmp_lt : slice_cmp_gt;

        while (cmp(start, stop)) {
                long point = string_getidx(str, start);
                bug_on(point < 0);
                string_writer_append(&wr, point);
                start += step;
        }
        return stringvar_from_writer(&wr);
}

/* .getitem sequence method for string  */
static Object *
string_getitem(Object *str, int idx)
{
        long point;

        bug_on(!isvar_string(str));
        bug_on(idx >= STRING_LENGTH(str));

        if (idx == 0 && seqvar_size(str) == 1)
                return VAR_NEW_REF(str);

        point = string_getidx(str, idx);
        bug_on(point < 0L || !utf8_valid_unicode(point));

        if (point > 0xffff) {
                uint32_t pts = point;
                return stringvar_from_points(&pts, 4, 1, SF_COPY);
        } else if (point > 0xff) {
                uint16_t pts = point;
                return stringvar_from_points(&pts, 2, 1, SF_COPY);
        } else {
                uint8_t pts = point;
                return stringvar_from_points(&pts, 1, 1, SF_COPY);
        }
}

static bool
string_hasitem(Object *str, Object *substr)
{
        ssize_t idx;

        bug_on(!isvar_string(str));
        /* XXX policy, throw error instead? */
        if (!isvar_string(substr))
                return false;

        idx = find_idx(str, substr, SF_SUPPRESS);
        return idx >= 0;
}


/* **********************************************************************
 *                           API functions
 * *********************************************************************/

/**
 * stringvar_new - Get a string var
 * @cstr: C-string to set string to, or NULL to do that later
 *
 * Return: new string var containing a copy of @cstr.
 */
Object *
stringvar_new(const char *cstr)
{
        return stringvar_newf((char *)cstr, SF_COPY);
}

/**
 * stringvar_newn - Get a string var from a subset of @cstr
 * @cstr: C-string to set string to
 * @n: Max length of new string.
 *
 * Return: new string var containing a copy of up to @n characters
 *         from @cstr
 */
Object *
stringvar_newn(const char *cstr, size_t n)
{
        char *new;
        size_t len = strlen(cstr);
        if (n > len)
                n = len;
        new = ememdup(cstr, n + 1);
        new[n] = '\0';
        return stringvar_newf(new, 0);
}

/**
 * stringvar_nocopy - like stringvar_new, but don't make a copy, just
 *                    take the pointer.
 *
 * Calling function is 'handing over' the pointer; it must have been
 * allocated on the heap.
 */
Object *
stringvar_nocopy(const char *cstr)
{
        return stringvar_newf((char *)cstr, 0);
}

/**
 * stringvar_from_buffer - Create a StringType variable using a buffer.
 * @b: Buffer which at the very minimum had been initialized with
 *      buffer_init.  This will be reinitialized upon return (see
 *      buffer_trim in buffer.c).
 *
 * Return: The newly created string variable.
 */
Object *
stringvar_from_buffer(struct buffer_t *b)
{
        char *s = buffer_trim(b);
        return stringvar_newf(s, 0);
}

/**
 * stringvar_from_source - Get a stringvar from an unparsed token.
 * @tokenstr: C-string as written in a source file, possibly containing
 *              backslash escape sequences which still need interpreting.
 *              Contains wrapping quotes.  If it was concatenated from
 *              two adjacent string tokens, the end quote of one token
 *              should be followed immediately by the starting quote of
 *              the next token; the different tokens need all not be
 *              wrapped by the same type of quote, though only jerk
 *              programmers mix and match these.
 *
 * Return: Variable from interpreted @tokenstr or ErrorVar.  Unicode
 *      escape sequences (ie. '\uNNNN') will be encoded into utf-8, even
 *      if the resulting string contains characters that do not all
 *      encode into utf-8.
 *
 * An error may be one of two kinds:
 *      1. A unicode escape sequence is out of bounds, ie > 0x10FFFF
 *      2. A null-char was inserted with a backslash-zero escape, not
 *         permitted for string data types.  (Users should use bytes
 *         instead.)
 */
Object *
stringvar_from_source(const char *tokenstr, bool imm)
{
        size_t width, len;
        enum result_t status;
        void *buf;

        status = string_parse(tokenstr, &buf, &width, &len);
        if (status != RES_OK)
                return ErrorVar;
        return stringvar_from_points(buf, width, len, 0);
}

/**
 * string_ord - Get the ordinal value of @str at index @idx
 */
long
string_ord(Object *str, size_t idx)
{
        /* These should have been checked before calling us */
        bug_on(!isvar_string(str));
        bug_on(idx >= seqvar_size(str));

        return string_getidx(str, idx);
}

/**
 * string_update_hash - Update string var with hash calculation.
 *
 * This doesn't truly affect the string, so it's not considered a
 * violation of immutability.  The only reason it doesn't happen at
 * stringvar_new() time is because we don't know yet if we're going to
 * need it.  It could be something getting added to .rodata, in which
 * calculating hash right at startup should be no big deal.  But it
 * could also be some rando stack variable that gets created and
 * destroyed every time a certain function is called and returns,
 * and which is never used in a way that requires the hash.  So we
 * let the calling code decide whether to update the hash or not.
 */
hash_t
string_update_hash(Object *v)
{
        struct stringvar_t *vs = (struct stringvar_t *)v;
        if (vs->s_hash == (hash_t)0)
                vs->s_hash = calc_string_hash(v);
        return vs->s_hash;
}

static const struct type_prop_t string_prop_getsets[] = {
        { .name = "length", .getprop = string_getprop_length, .setprop = NULL },
        { .name = "nbytes", .getprop = string_getprop_nbytes, .setprop = NULL },
        { .name = "width",  .getprop = string_getprop_width,  .setprop = NULL },
        { .name = NULL },
};

struct seq_methods_t string_seq_methods = {
        .getitem        = string_getitem,
        .setitem        = NULL,
        .hasitem        = string_hasitem,
        .getslice       = string_getslice,
        .cat            = string_cat,
        .sort           = NULL,
};

struct operator_methods_t string_opm = {
        .mod    = string_modulo,
};

struct type_t StringType = {
        .name   = "string",
        .opm    = &string_opm,
        .cbm    = string_methods,
        .mpm    = NULL,
        .sqm    = &string_seq_methods,
        .size   = sizeof(struct stringvar_t),
        .str    = string_str,
        .cmp    = string_cmp,
        .cmpz   = string_cmpz,
        .reset  = string_reset,
        .prop_getsets = string_prop_getsets,
};


