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

static inline bool
isdigit_ascii(long pt)
{
        return pt >= '0' && pt <= '9';
}

static inline size_t
string_width(Object *str)
{
        return V2STR(str)->s_width;
}

static inline void *
string_data(Object *str)
{
        return V2STR(str)->s_unicode;
}

static long
string_getidx_raw(size_t width, const void *unicode, size_t idx)
{
        /*
         * Repurpose this function, since the string_reader_t struct
         * is not required for it.
         */
        return string_reader_getc__(width, unicode, idx);
}

/* Only used by stringvar_from_points, otherwise violates immutability */
static void
string_setidx_raw(size_t width, void *unicode,
                  size_t idx, unsigned long point)
{
        if (width == 1) {
                ((uint8_t *)unicode)[idx] = point;
        } else if (width == 2) {
                ((uint16_t *)unicode)[idx] = point;
        } else {
                bug_on(width != 4);
                ((uint32_t *)unicode)[idx] = point;
        }
}

static long
string_getidx(Object *str, size_t idx)
{
        bug_on(idx >= seqvar_size(str));
        return string_getidx_raw(string_width(str), string_data(str), idx);
}

static void
string_writer_append_strobj(struct string_writer_t *wr, Object *str)
{
        string_writer_appendb(wr, string_data(str),
                              string_width(str), seqvar_size(str));
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
        vs->s_unicode = utf8_decode(vs->s, &vs->s_width,
                                    &vs->s_enc_len, &vs->s_ascii);
        seqvar_set_size(ret, vs->s_enc_len);
        return ret;
}

static size_t
maxchr_to_width(unsigned long maxchr)
{
        if (maxchr > 0xffff)
                return 4;
        if (maxchr > 0xff)
                return 2;
        return 1;
}

static Object *
stringvar_from_points(void *points, size_t width,
                      size_t len, unsigned int flags)
{
        Object *ret;
        struct buffer_t b;
        size_t i;
        long maxchr;
        int ascii;
        struct stringvar_t *vs;

        bug_on((!!len && !points) || (!len && !!points));
        if (!len)
                return VAR_NEW_REF(STRCONST_ID(mpty));

        maxchr = 0;
        ascii = 1;
        buffer_init(&b);
        for (i = 0; i < len; i++) {
                uint32_t point = string_getidx_raw(width, points, i);
                if (point > maxchr)
                        maxchr = point;

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

        vs->s_enc_len   = len;
        vs->s_width     = width;
        vs->s_ascii_len = buffer_size(&b);
        vs->s           = buffer_trim(&b);
        vs->s_hash      = 0;
        vs->s_ascii     = ascii;
        seqvar_set_size(ret, len);
        if (ascii) {
                if (!(flags & SF_COPY))
                        efree(points);
                vs->s_unicode = vs->s;
        } else {
                if (!!(flags & SF_COPY)) {
                        /*
                         * We could be here to create a string from a
                         * source's substring, in which case our width
                         * may no longer be correct.  Check for that and
                         * shrink as necessary, otherwise some of our
                         * find algorithms could return false negatives.
                         */
                        size_t correct_width = maxchr_to_width(maxchr);
                        bug_on(correct_width > width);
                        if (correct_width == width) {
                                vs->s_unicode = ememdup(points, len * width);
                        } else {
                                /* D'oh! We need to downsize */
                                vs->s_unicode = emalloc(len * correct_width);
                                for (i = 0; i < len; i++) {
                                        long point;
                                        point = string_getidx_raw(width,
                                                        points, i);
                                        string_setidx_raw(correct_width,
                                                        vs->s_unicode, i, point);
                                }
                                vs->s_width = correct_width;
                        }
                } else {
                        /*
                         * If not SF_COPY, then we got this from
                         * either parse or a struct string_writer_t.
                         * In both cases, we should have not over-
                         * estimated the width, so this is a bug.
                         */
                        bug_on(maxchr_to_width(maxchr) != width);
                        vs->s_unicode = points;
                }
        }
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

        width = string_width(old);
        len  = stop - start;
        buf = string_data(old) + start * width;

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
        string_writer_destroy(&wr);
        return RES_ERROR;
}

static void *
widen_buffer(Object *str, size_t width)
{
        void *tbuf, *src, *dst, *end;
        size_t n, old_width;

        old_width = string_width(str);
        bug_on(old_width >= width);

        n = seqvar_size(str);
        tbuf = emalloc(n * width);

        src = string_data(str);
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
find_idx_substr(Object *haystack, Object *needle,
                unsigned int flags, size_t startpos, size_t endpos)
{
        ssize_t hwid, nwid, hlen, nlen, idx;

        bug_on(!isvar_string(haystack));
        bug_on(!isvar_string(needle));

        hlen = endpos - startpos;
        nlen = seqvar_size(needle);
        hwid = string_width(haystack);
        nwid = string_width(needle);

        if (hwid < nwid || hlen < nlen) {
                idx = -1;
        } else {
                void *found;
                void *hsrc = string_data(haystack) + startpos * hwid;
                void *nsrc = string_data(needle);
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
                if (nsrc != string_data(needle))
                        efree(nsrc);
        }
        bug_on(idx >= (ssize_t)seqvar_size(haystack));
        return idx;
}

static inline ssize_t
find_idx(Object *haystack, Object *needle, unsigned int flags)
{
        return find_idx_substr(haystack, needle,
                               flags, 0, seqvar_size(haystack));
}

/*
 * Starting with @startpoint (known to be between '0' and '9' inclusive),
 * parse @str from @pos, to complete the number, and return the number.
 *
 * Return: Positive base-10 integer value, or -1 if number too big to fit
 *         into a signed int.
 */
static int
str_finish_digit(Object *str, size_t *pos, long startpoint)
{
        size_t len = seqvar_size(str);
        size_t tpos = *pos;
        int res = startpoint - '0';
        bug_on(!isdigit_ascii(startpoint));
        while (tpos < len) {
                long point = string_getidx(str, tpos);
                if (!isdigit_ascii(point))
                        break;
                if (res > INT_MAX / 10) {
                        err_setstr(RangeError, "Number to high");
                        return -1;
                }
                res = res * 10 + (point - '0');
                tpos++;
        }
        *pos = tpos;
        return res;
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

struct fmt_args_t {
        int conv;
        bool rjust;
        int padc;
        size_t padlen;
        int precision;
};

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
        size_t right = string_writer_size(wr) - 1;
        size_t left = right - padlen;
        if ((ssize_t)right < 0)
                return;

        while (count--) {
                string_writer_swapchars(wr, left, right);
                bug_on((ssize_t)left < 0);
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
format2_i(struct string_writer_t *wr, Object *arg, struct fmt_args_t *fa)
{
        int base;
        size_t count;
        int xchar = 'A' - 10;
        long long ival = realvar_toint(arg);

        /* overrule '0' if left-justified */
        if (!fa->rjust)
                fa->padc = ' ';

        switch (fa->conv) {
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

        count = string_writer_size(wr);
        if (!ival) {
                string_writer_append(wr, '0');
        } else {
                unsigned long long uval;
                if (fa->conv == 'd' && ival < 0) {
                        string_writer_append(wr, '-');
                        uval = -ival;
                } else {
                        uval = (unsigned long long)ival;
                }
                format2_i_helper(wr, uval, base, xchar);
        }

        count = string_writer_size(wr) - count;
        if (count < fa->padlen) {
                fa->padlen -= count;
                padwrite(wr, fa->padc, fa->padlen);
                if (fa->rjust) {
                        swap_pad(wr, count, fa->padlen);
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
format2_e(struct string_writer_t *wr, Object *arg, struct fmt_args_t *fa)
{
        int exp = 0;
        int sigfig = 0;
        double ival;
        /* checked before this call */
        double v = realvar_tod(arg);
        double dv = v;

        size_t count = string_writer_size(wr);

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
                int pr = fa->precision;
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
        while (sigfig < fa->precision) {
                dv *= 10.0;
                dv = modf(dv, &ival);
                string_writer_append(wr, (int)ival + '0');
                sigfig++;
        }

        /* print exponent */
        bug_on(fa->conv != 'e' && fa->conv != 'E');
        string_writer_append(wr, fa->conv);
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

        if (!fa->rjust)
                fa->padc = ' ';
        count = string_writer_size(wr) - count;
        if (count < fa->padlen) {
                fa->padlen -= count;
                padwrite(wr, fa->padc, fa->padlen);
                if (fa->rjust) {
                        swap_pad(wr, count, fa->padlen);
                }
        }
}

static void
format2_f(struct string_writer_t *wr, Object *arg, struct fmt_args_t *fa)
{
        double v = realvar_tod(arg);
        bool have_dot = false;
        size_t count = string_writer_size(wr);

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
                for (scale = 1.0, i = 0; i < fa->precision; i++)
                        scale *= 0.1;
                v += scale * 0.5;
                rem = modf(v, &iptr);

                format2_f_ihelper(wr, (unsigned int)iptr);

                if (fa->precision > 0) {
                        have_dot = true;
                        string_writer_append(wr, '.');
                        while (fa->precision--) {
                                rem *= 10.0;
                                string_writer_append(wr, (int)rem + '0');
                                rem = modf(rem, &iptr);
                        }
                }
        }

        if (!fa->rjust && !have_dot)
                fa->padc = ' ';
        count = string_writer_size(wr) - count;
        if (count < fa->padlen) {
                fa->padlen -= count;
                padwrite(wr, fa->padc, fa->padlen);
                if (fa->rjust) {
                        swap_pad(wr, count, fa->padlen);
                }
        }
}

/*
 * allows us to call vsnprintf instead of snprintf, to avoid
 * annoying compiler warnings about non-literal format args.
 */
static void
format2_g__(char *buf, size_t n, const char *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);
        memset(buf, 0, n);
        vsnprintf(buf, n - 1, fmt, ap);
        va_end(ap);
}

static void
format2_g(struct string_writer_t *wr, Object *arg, struct fmt_args_t *fa)
{
        double v = realvar_tod(arg);
        char *buf;
        char fmtbuf[64];

        /* '%g' hurts my brain, so I'll just call the library function */
        memset(fmtbuf, 0, sizeof(fmtbuf));
        snprintf(fmtbuf, sizeof(fmtbuf)-1, "%%%s%s%d.%d%c",
                 fa->rjust ? "" : "-",
                 fa->padc == '0' ? "0" : "",
                 (int)fa->padlen, (int)fa->precision, fa->conv);

        buf = emalloc(32 + fa->precision);
        format2_g__(buf, sizeof(buf), fmtbuf, v);
        string_writer_appends(wr, buf);
        efree(buf);
}

static void
format2_s(struct string_writer_t *wr, Object *arg, struct fmt_args_t *fa)
{
        /* count = #chars, not #bytes */
        size_t count = seqvar_size(arg);
        string_writer_append_strobj(wr, arg);

        if (count < fa->padlen) {
                fa->padlen -= count;
                padwrite(wr, fa->padc, fa->padlen);
                if (fa->rjust)
                        swap_pad(wr, count, fa->padlen);
        }
}

static void
default_fmt_args(struct fmt_args_t *args)
{
        args->conv = '\0';
        args->rjust = true;
        args->padc = ' ';
        args->padlen = 0;
        args->precision = 6;
}

/*
 * Lightweight printf-like alternative to format()
 *
 * keyword-name, opening '%' or '{' have already been processed.
 * @endchr is used to determine whether we need to parse closing
 * '}', in case this is an f-string or .format() function (as opposed
 * to a string modulo operation).
 *
 * Parses
 *
 *         [{flags}{pad}.{precition}{conversion}]
 *
 * along with closing '}' if @endchr is '}'.  If no specifiers are found,
 * then defaults will be used.  The default for conversion is nullchar;
 * calling code must decide what to do if no conversion is found.
 *
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
 *
 * Note, this does not permit length specifiers (h, hh, l, etc.)
 *
 * Return: New position in string, or -1 if an error occurred.
 */
static ssize_t
parse_fmt_args(Object *fmt, struct fmt_args_t *args, size_t pos, int endchr)
{
        unsigned long point;
        size_t n = seqvar_size(fmt);

        /* Shouldn't have called us if this is true */
        bug_on(pos >= n);

        default_fmt_args(args);

        /* get flags */
        for (;;) {
                bug_on(pos > n);
                if (pos == n)
                        goto eos;
                point = string_getidx(fmt, pos++);
                if (point == '-') {
                        args->rjust = false;
                } else if (point == '0') {
                        args->padc = '0';
                } else {
                        break;
                }
        }

        if (isdigit_ascii(point)) {
                args->padlen = str_finish_digit(fmt, &pos, point);
                if ((int)args->padlen < 0)
                        return -1;
                if (pos == n)
                        goto eos;
                point = string_getidx(fmt, pos++);
        }

        if (point == '.') {
                args->precision = 0;
                if (pos == n)
                        goto eos;
                point = string_getidx(fmt, pos++);
                if (isdigit_ascii(point)) {
                        args->precision = str_finish_digit(fmt, &pos, point);
                        if ((int)args->precision < 0)
                                return -1;
                        if (pos == n)
                                goto eos;
                        point = string_getidx(fmt, pos++);
                }
        }

        if (args->padlen >= PAD_MAX)
                args->padlen = PAD_MAX;
        if (args->precision >= PRECISION_MAX)
                args->precision = PRECISION_MAX;

        if (point < 128 && strchr("xXdufeEsgG", point)) {
                args->conv = point;
                if (endchr) {
                        if (point == n)
                                return -1;
                        point = string_getidx(fmt, pos++);
                        if (point != endchr)
                                return -1;
                }
                return pos;
        } else if (point == endchr) {
                return pos;
        } else if (!endchr) {
                return pos - 1;
        }
        return -1;

eos:
        if (args->padlen >= PAD_MAX)
                args->padlen = PAD_MAX;
        if (args->precision >= PRECISION_MAX)
                args->precision = PRECISION_MAX;

        return endchr ? -1 : pos;
}

static void
format2_output(struct string_writer_t *wr,
               Object *val, struct fmt_args_t *fa)
{
        if (!fa->conv) {
                if (isvar_int(val))
                        fa->conv = 'd';
                else if (isvar_float(val))
                        fa->conv = 'e';
                else
                        fa->conv = 's';
        }

        switch (fa->conv) {
        case 'x':
        case 'X':
        case 'd':
        case 'u':
                if (!isvar_real(val))
                        return;
                format2_i(wr, val, fa);
                break;
        case 'f':
                if (!isvar_real(val))
                        return;
                format2_f(wr, val, fa);
                break;
        case 'e':
        case 'E':
                if (!isvar_real(val))
                        return;
                format2_e(wr, val, fa);
                break;
        case 'g':
        case 'G':
                if (!isvar_real(val))
                        return;
                format2_g(wr, val, fa);
                break;
        case 's':
                if (!isvar_string(val)) {
                        Object *strval = var_str(val);
                        format2_s(wr, strval, fa);
                        VAR_DECR_REF(strval);
                } else {
                        format2_s(wr, val, fa);
                }
                break;
        default:
                bug();
        }
}

static ssize_t
format2_helper(Object *arg, struct string_writer_t *wr,
               Object *self, size_t self_i)
{
        struct fmt_args_t fa;
        ssize_t newpos = parse_fmt_args(self, &fa, self_i, '\0');

        /* incr by one on error, else caller will loop forever */
        if (newpos < 0)
                return self_i + 1;

        format2_output(wr, arg, &fa);
        return newpos;
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
        i = 0;
        string_writer_init(&wr, string_width(self));
        while (i < n) {
                unsigned long point = string_getidx(self, i++);
                if (point == '%') {
                        Object *arg;

                        if (i >= n)
                                break;
                        point = string_getidx(self, i++);
                        if (point == '%') {
                                string_writer_append(&wr, '%');
                                continue;
                        }

                        if (point == '(') {
                                size_t start, stop;
                                Object *key;

                                if (!kwargs)
                                        continue;

                                start = i++;
                                while (i < n) {
                                        point = string_getidx(self, i++);
                                        bug_on(point < 0);
                                        if (point == ')')
                                                break;
                                }
                                stop = i;
                                key = stringvar_from_substr(self, start, stop);
                                arg = dict_getitem(kwargs, key);
                                VAR_DECR_REF(key);
                                if (!arg)
                                        continue;
                        } else {
                                --i;

                                /* Numbered arg */
                                if (!args)
                                        continue;

                                arg = seqvar_getitem(args, argi++);
                                if (!arg)
                                        continue;
                        }

                        /* minus one because of 'for' iterator */
                        i = format2_helper(arg, &wr, self, i);
                        VAR_DECR_REF(arg);
                } else {
                        string_writer_append(&wr, point);
                }
        }

        return stringvar_from_writer(&wr);
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
        return intvar_new(string_width(self));
}

/*
 * format(...)
 * returns type string
 */
static Object *
string_format_mthd(Frame *fr)
{
        Object *self = vm_get_this(fr);
        Object *list = vm_get_arg(fr, 0);

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        bug_on(!isvar_array(list));

        return string_format(self, list);
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
        struct string_writer_t wr;
        Object *haystack, *needle, *repl;
        size_t hlen, nlen, hwid, nwid, start;
        void *hsrc, *nsrc;
        size_t wr_wid;

        haystack = vm_get_this(fr);
        needle = vm_get_arg(fr, 0);
        repl = vm_get_arg(fr, 1);

        if (arg_type_check(haystack, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(needle, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(repl, &StringType) == RES_ERROR)
                return ErrorVar;

        nwid = string_width(needle);
        hwid = string_width(haystack);
        nlen = seqvar_size(needle);
        hlen = seqvar_size(haystack);

        if (hlen < nlen || hwid < nwid || nlen == 0)
                return VAR_NEW_REF(haystack);

        hsrc = string_data(haystack);
        if (nwid != hwid)
                nsrc = widen_buffer(needle, hwid);
        else
                nsrc = string_data(needle);

        /*
         * We don't know if repl will remove widest chars in haystack,
         * nor do we know if repl, which could have larger chars, is
         * even going to be inserted, so assume the smaller of the
         * two sizes and correct ourselves later.
         */
        wr_wid = string_width(repl);
        if (wr_wid > hwid)
                wr_wid = hwid;
        string_writer_init(&wr, wr_wid);

        start = 0;
        while (start < hlen) {
                size_t idx;
                void *found;
                void *th = hsrc + start * hwid;

                found = memmem(th, hlen - start, nsrc, nlen);
                if (!found) {
                        if (start == 0)
                                goto return_self;
                        string_writer_appendb(&wr, th, hwid, hlen - start);
                        break;
                }

                idx = (size_t)(found - hsrc) / hwid;
                bug_on(idx >= hlen);
                if (idx != start)
                        string_writer_appendb(&wr, th, hwid, idx - start);
                string_writer_append_strobj(&wr, repl);
                start = idx + nlen;
        }
        if (nsrc != string_data(needle))
                efree(nsrc);

        return stringvar_from_writer(&wr);

return_self:
        string_writer_destroy(&wr);
        if (nsrc != string_data(needle))
                efree(nsrc);
        return VAR_NEW_REF(haystack);
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

        string_writer_init(&wr, string_width(self));
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

        if (n > 2)
                width = string_width(self);
        else
                width = 1;
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
                        twid = string_width(elem);
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

                if (width < string_width(arg))
                        width = string_width(arg);
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
        Object *self;
        size_t i, n;
        long point;
        struct string_writer_t wr;

        self = vm_get_this(fr);
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        n = seqvar_size(self);
        if (!n)
                return VAR_NEW_REF(self);

        point = string_getidx(self, 0);
        bug_on(point < 0);
        if (n == 1 && evc_isupper(point))
                return VAR_NEW_REF(self);

        string_writer_init(&wr, string_width(self));
        string_writer_append(&wr, evc_toupper(point));
        for (i = 1; i < n; i++) {
                point = string_getidx(self, i);
                string_writer_append(&wr, evc_tolower(point));
        }
        return stringvar_from_writer(&wr);
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
        Object *haystack, *needle;
        size_t hlen, nlen, hwid, nwid;
        void *hsrc, *nsrc;

        haystack = vm_get_this(fr);
        needle = vm_get_arg(fr, 0);
        if (arg_type_check(haystack, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(needle, &StringType) == RES_ERROR)
                return ErrorVar;

        hlen = seqvar_size(haystack);
        nlen = seqvar_size(needle);
        hwid = string_width(haystack);
        nwid = string_width(needle);
        if (hlen < nlen || hwid < nwid || hlen == 0 || nlen == 0) {
                count = 0;
                goto done;
        }
        hsrc = string_data(haystack);
        if (nwid != hwid)
                nsrc = widen_buffer(needle, hwid);
        else
                nsrc = string_data(needle);

        size_t i = 0;
        count = 0;
        while (i + nlen <= hlen) {
                if (!memcmp(hsrc + i * hwid, nsrc, nlen * hwid)) {
                        count++;
                        i += nlen;
                } else {
                        i++;
                }
        }
        if (nsrc != string_data(needle))
                efree(nsrc);

done:
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

        string_writer_init(&wr, string_width(self));
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
                size_t wid = string_width(self);
                void *points = string_data(self);
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

static Object *
string_lrsplit(Frame *fr, unsigned int flags)
{
        enum { LRSPLIT_STACK_SIZE = 64, };
        Object *self = vm_get_this(fr);
        Object *kw = vm_get_arg(fr, 0);
        Object *separg, *maxarg;
        Object *ret;
        int maxsplit;
        size_t hwid, hlen, nwid, nlen;
        void *hsrc, *nsrc;
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

        hwid = string_width(self);
        hlen = seqvar_size(self);
        nwid = string_width(separg);
        nlen = seqvar_size(separg);

        ret = arrayvar_new(0);
        if (hlen < nlen || hwid < nwid) {
                array_append(ret, self);
                goto out;
        }

        hsrc = string_data(self);
        if (nwid != hwid)
                nsrc = widen_buffer(separg, hwid);
        else
                nsrc = string_data(separg);

        if (!!(flags & SF_RIGHT)) {
                Object *substr;
                while (hlen > nlen && maxsplit-- != 0) {
                        ssize_t idx;
                        void *found;
                        found = memrmem(hsrc, hlen, nsrc, nlen);
                        if (!found)
                                break;
                        idx = (found - hsrc) / hwid;
                        bug_on(idx > seqvar_size(self));
                        if (idx + nlen == hlen) {
                                if (!combine)
                                        array_append(ret, STRCONST_ID(mpty));
                        } else {
                                substr = stringvar_from_substr(self, idx + nlen, hlen);
                                array_append(ret, substr);
                                VAR_DECR_REF(substr);
                        }
                        if (!combine && idx + nlen != hlen &&
                            idx == 0 && maxsplit-- != 0) {
                                /* last sep */
                                array_append(ret, STRCONST_ID(mpty));
                        }
                        hlen = idx;
                }
                if (hlen) {
                        substr = stringvar_from_substr(self, 0, hlen);
                        array_append(ret, substr);
                        VAR_DECR_REF(substr);
                }
                array_reverse(ret);
        } else {
                Object *substr;
                size_t start = 0;
                while (start < hlen && maxsplit-- != 0) {
                        ssize_t idx;
                        void *found;
                        void *th = hsrc + start * hwid;

                        found = memmem(th, hlen - start, nsrc, nlen);
                        if (!found)
                                break;

                        idx = (found - hsrc) / hwid;
                        bug_on(idx > seqvar_size(self));
                        if (idx == start) {
                                if (!combine)
                                        array_append(ret, STRCONST_ID(mpty));
                        } else {
                                substr = stringvar_from_substr(self, start, idx);
                                array_append(ret, substr);
                                VAR_DECR_REF(substr);
                        }
                        if (!combine && idx + nlen == hlen &&
                            idx != start && maxsplit-- != 0) {
                                /* last sep */
                                array_append(ret, STRCONST_ID(mpty));
                        }
                        start = idx + nlen;
                }
                if (start < hlen) {
                        substr = stringvar_from_substr(self, start, hlen);
                        array_append(ret, substr);
                        VAR_DECR_REF(substr);
                }
        }

        if (nsrc != string_data(separg))
                efree(nsrc);

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
string_is2(Frame *fr, bool (*tst)(unsigned long c))
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

static bool
is_ident(Object *str)
{
        size_t i, n;
        long point;
        bug_on(!isvar_string(str));

        n = seqvar_size(str);
        bug_on(n == 0);

        point = string_getidx(str, 0);
        if (point != '_' && !evc_isalpha(point))
                return false;

        for (i = 1; i < n; i++) {
                point = string_getidx(str, i);
                if (!evc_isalnum(point) && point != '_')
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
                if (!evc_isalpha(point)) {
                        first = true;
                } else if (first) {
                        if (evc_islower(point))
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
        { return string_is2(fr, evc_isalnum); }

static Object *string_isalpha(Frame *fr)
        { return string_is2(fr, evc_isalpha); }

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
        { return string_is2(fr, evc_isdigit); }

static Object *string_isprintable(Frame *fr)
        { return string_is2(fr, evc_isprint); }

static Object *string_isspace(Frame *fr)
        { return string_is2(fr, evc_isspace); }

static Object *string_isupper(Frame *fr)
        { return string_is2(fr, evc_isupper); }

/*
 * string case-swapping & helpers
 */
static Object *
string_title(Frame *fr)
{
        Object *self;
        bool first;
        size_t i, n;
        struct string_writer_t wr;

        self = vm_get_this(fr);
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        /* XXX: Do I know that evc_toupper/lower do not change width? */
        string_writer_init(&wr, string_width(self));
        n = seqvar_size(self);
        first = true;
        for (i = 0; i < n; i++) {
                long point = string_getidx(self, i);
                bug_on(point < 0);
                if (evc_isalpha(point)) {
                        if (first) {
                                point = evc_toupper(point);
                                first = false;
                        } else {
                                point = evc_tolower(point);
                        }
                } else {
                        first = true;
                }
                string_writer_append(&wr, point);
        }
        return stringvar_from_writer(&wr);
}

static Object *
string_to(Frame *fr, unsigned long (*cb)(unsigned long))
{
        Object *self;
        size_t i, n;
        struct string_writer_t wr;

        self = vm_get_this(fr);
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        string_writer_init(&wr, string_width(self));
        n = seqvar_size(self);
        for (i = 0; i < n; i++) {
                long point = string_getidx(self, i);
                bug_on(point < 0);
                string_writer_append(&wr, cb(point));
        }
        return stringvar_from_writer(&wr);
}

static unsigned long
to_swap(unsigned long c)
{
        if (c < 128) {
                if (evc_isupper(c))
                        c = evc_tolower(c);
                else if (evc_islower(c))
                        c = evc_toupper(c);
        }
        return c;
}

static Object *
string_lower(Frame *fr)
{
        return string_to(fr, evc_tolower);
}

static Object *
string_swapcase(Frame *fr)
{
        return string_to(fr, to_swap);
}

static Object *
string_upper(Frame *fr)
{
        return string_to(fr, evc_toupper);
}

static struct type_inittbl_t string_methods[] = {
        V_INITTBL("capitalize",   string_capitalize,   0, 0, -1, -1),
        V_INITTBL("center",       string_center,       1, 1, -1, -1),
        V_INITTBL("count",        string_count,        1, 1, -1, -1),
        V_INITTBL("endswith",     string_endswith,     1, 1, -1, -1),
        V_INITTBL("expandtabs",   string_expandtabs,   1, 1, -1,  0),
        V_INITTBL("find",         string_find,         1, 1, -1, -1),
        V_INITTBL("format",       string_format_mthd,  1, 1,  0, -1),
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

static Object *
string_str(Object *v)
{
        struct buffer_t b;
        size_t i, n;
        enum { Q = '\'', BKSL = '\\' };

        bug_on(!isvar_string(v));

        /*
         * Since we're deliberately creating an all-ASCII string,
         * we know it's faster to create from C-string than from
         * Unicode points.
         */
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
                } else if (c < 128 && evc_isspace(c)) {
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
                } else if (c < 128 && !evc_isgraph(c)) {
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
                                /* XXX Hex is more compact than octal */
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
         * even for strings with all-matching Unicode points.  The
         * alternative is a for loop which is probably not as fast as
         * either strcmp or memcmp.
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

/* TODO: if arg=string, replace '%[fmt-args]' with arg */
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
                return NULL;
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
         * even if string_width(str) > 1, because the >1 non-ASCII
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

static Object *
string_from_encoded_obj(Object *obj, Object *encarg)
{
        static const struct str2enum_t ENCODINGS[] = {
                { .s = "utf-8",   .v = CODEC_UTF8 },
                { .s = "utf8",    .v = CODEC_UTF8 },
                { .s = "latin1",  .v = CODEC_LATIN1 },
                { .s = "latin-1", .v = CODEC_LATIN1 },
                { .s = "ascii",   .v = CODEC_ASCII },
                { .s = NULL, 0 },
        };
        int encoding;
        size_t n;
        const unsigned char *data;
        char *buf;

        if (!isvar_bytes(obj)) {
                err_setstr(TypeError,
                        "string() cannot encode %s object", typestr(obj));
                return ErrorVar;
        }
        if (strobj2enum(ENCODINGS, encarg, &encoding, 0, "encoding", 1)
            == RES_ERROR) {
                return ErrorVar;
        }

        n = seqvar_size(obj);
        if (n == 0)
                return VAR_NEW_REF(STRCONST_ID(mpty));

        data = bytes_get_data(obj);

        if (encoding == CODEC_LATIN1)
                return stringvar_from_points((void *)data, 1, n, SF_COPY);

        if (encoding == CODEC_ASCII) {
                /* Pre-check before allocating buffer below */
                size_t i;
                for (i = 0; i < n; i++) {
                        if ((unsigned int)data[i] > 127) {
                                err_setstr(ValueError,
                                        "value %d at position %ld is not ASCII",
                                        (unsigned int)data[i], i);
                                return ErrorVar;
                        }
                }
        }

        /*
         * FIXME: If CODEC_UTF8, do not pre-allocate buf like this.
         * Requires changing utf8_decode_one() so that it takes a
         * length argument, since data[] is not nullchar-terminated.
         */
        buf = emalloc(n + 1);
        memcpy(buf, data, n);
        buf[n] = '\0';
        if (encoding == CODEC_UTF8) {
                unsigned char *s;
                struct string_writer_t wr;
                string_writer_init(&wr, 1);
                s = (unsigned char *)buf;
                while (*s != '\0') {
                        unsigned char *endptr;
                        long point = utf8_decode_one(s, &endptr);
                        if (point < 0L) {
                                /*
                                 * We're being more strict here than in
                                 * string_parse().  I'd rather be more
                                 * consistent, but I noticed Python does
                                 * the same thing.
                                 */
                                size_t idx = (size_t)((char *)s - buf);
                                err_setstr(ValueError,
                                        "value %d at position %ld is not valid UTF-8",
                                        (unsigned int)data[idx], idx);
                                efree(buf);
                                string_writer_destroy(&wr);
                                return ErrorVar;
                        }
                        string_writer_append(&wr, point);
                        s = endptr;
                }
                efree(buf);
                return stringvar_from_writer(&wr);
        } else {
                bug_on(encoding != CODEC_ASCII);
                return stringvar_newf(buf, 0);
        }
}

static Object *
string_create(Frame *fr)
{
        Object *args, *kwargs, *encoding, *ret;
        int argc;

        args = vm_get_arg(fr, 0);
        kwargs = vm_get_arg(fr, 1);
        bug_on(!args || !isvar_array(args));
        bug_on(!kwargs || !isvar_dict(kwargs));

        ret = ErrorVar;
        argc = seqvar_size(args);
        encoding = dict_getitem(kwargs, STRCONST_ID(encoding));
        if (encoding) {
                if (argc > 1) {
                        err_doublearg("encoding");
                        goto out;
                } else if (argc == 0) {
                        err_setstr(TypeError, "Nothing to decode");
                        goto out;
                }
        } else if (argc > 1) {
                encoding = array_getitem(args, 1);
                bug_on(!encoding);
        }
        if (encoding && !isvar_string(encoding)) {
                err_setstr(TypeError,
                           "Expected: encoding=string but got %s",
                           typestr(encoding));
                goto out;
        }

        if (argc == 0) {
                ret = VAR_NEW_REF(STRCONST_ID(mpty));
        } else {
                Object *val = array_borrowitem(args, 0);
                bug_on(!val);
                if (encoding)
                        ret = string_from_encoded_obj(val, encoding);
                else if (isvar_string(val))
                        ret = VAR_NEW_REF(val);
                else
                        ret = var_str(val);
        }
out:
        if (encoding)
                VAR_DECR_REF(encoding);
        return ret;
}

/* **********************************************************************
 *                           API functions
 * *********************************************************************/

void
string_reader_init(struct string_reader_t *rd,
                   Object *str, size_t startpos)
{
        bug_on(!isvar_string(str));
        rd->dat = string_data(str);
        rd->wid = string_width(str);
        rd->len = seqvar_size(str);
        if (startpos > rd->len)
                startpos = rd->len;
        rd->pos = startpos;
}

/* like strchr, but for string objects, and only returns truth value */
bool
string_chr(Object *str, long pt)
{
        size_t i, n, w;
        void *p;

        bug_on(!isvar_string(str));

        n = seqvar_size(str);
        w = string_width(str);
        p = string_data(str);

        for (i = 0; i < n; i++) {
                if (string_getidx_raw(w, p, i) == pt)
                        return true;
        }

        return false;
}

/**
 * string_slide - similar to slide in helpers.c, but for string objects.
 * @str:        The string to slide across
 * @delims:     Character set of delimiters to skip
 * @pos:        Starting position to slide from
 *
 * If @delims are NULL or set to NullVar, skip only whitespace.
 * Otherwise skip any matching characters in @delims.
 *
 * Return: New position
 */
size_t
string_slide(Object *str, Object *delims, size_t pos)
{
        size_t slen;

        bug_on(!isvar_string(str));
        bug_on(delims != NULL &&
               delims != NullVar &&
               !isvar_string(delims));

        if (delims == NullVar)
                delims = NULL;

        slen = seqvar_size(str);
        while (pos < slen) {
                long point = string_getidx(str, pos);
                if (!evc_isspace(point) &&
                    !(delims && string_chr(delims, point))) {
                        break;
                }
                pos++;
        }
        return pos;
}

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

/*
 * @tup may be a list, if not called from VM
 *
 * TODO: Add a @dict arg, which may be NULL, so str.format can
 * take keyword arguments.
 */
Object *
string_format(Object *str, Object *tup)
{
        struct string_writer_t wr;
        size_t i, n, nt;
        size_t argi;

        if (arg_type_check(str, &StringType) == RES_ERROR)
                return ErrorVar;
        if (!isvar_tuple(tup) && !isvar_array(tup)) {
                err_setstr(TypeError, "format expects tuple or list");
                return ErrorVar;
        }

        n = seqvar_size(str);
        nt = seqvar_size(tup);
        i = 0;
        argi = 0;
        string_writer_init(&wr, string_width(str));
        while (i < n) {
                long point = string_getidx(str, i++);
                if (point == '{' && i < n) {
                        struct fmt_args_t fa;
                        Object *arg;
                        point = string_getidx(str, i++);
                        if (point == '{') {
                                string_writer_append(&wr, point);
                                continue;
                        }
                        if (point == ':') {
                                size_t newpos;

                                if (i == n)
                                        goto bad_format;
                                newpos = parse_fmt_args(str, &fa, i, '}');
                                if (newpos < 0)
                                        goto bad_format;
                                i = newpos;
                        } else {
                                if (isdigit_ascii(point)) {
                                        argi = str_finish_digit(str, &i, point);
                                        if ((int)argi < 0)
                                                goto bad_format;
                                }
                                /*
                                 * TODO: if point is ident, use
                                 * dict key.
                                 */
                                if (point != '}')
                                        goto bad_format;
                                default_fmt_args(&fa);
                        }

                        if (argi >= nt)
                                goto bad_format;

                        arg = seqvar_getitem(tup, argi++);
                        format2_output(&wr, arg, &fa);
                        VAR_DECR_REF(arg);
                } else {
                        if (point == '}') {
                                if (i >= n)
                                        goto bad_format;
                                point = string_getidx(str, i++);
                                if (point != '}')
                                        goto bad_format;
                        }
                        string_writer_append(&wr, point);
                }
        }
        return stringvar_from_writer(&wr);

bad_format:
        err_setstr(ValueError, "Malformed format string");
        string_writer_destroy(&wr);
        return ErrorVar;
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
        .flags  = 0,
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
        .create = string_create,
};


