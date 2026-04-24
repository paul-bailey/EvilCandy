/*
 * string.c - Built-in methods for string data types
 *
 * Strings have two buffers (unless they happen to be 100%-ASCII). The
 * first is the string objects's .s field, a C-string containing only
 * ASCII or UTF-8 encoded characters.  string_cstring() returns this
 * (directly, not copied).  The second buffer is an array of Unicode
 * points, whose width is specified by the string's .s_width field.  For
 * speed, the Unicode arrays are operated on the most.  The C string is
 * used for hashing and printing (since most every output takes UTF-8).
 *
 * seqvar_size(str) measures number of Unicode points, not the number of
 * encoded C-string bytes.  For the latter, use string_nbytes().  If
 * string_nbytes(str) does not match strlen(string_cstring(str)), it
 * means that 'str' has embedded nullchars.
 *
 * If any .s field is not properly UTF-8 encoded, then different Objects
 * with the exact same Unicode points could end up with different hashes.
 * So these all MUST have proper encoding.  stringvar_from_source() will
 * take care of this for literal expressions in user code, but all other
 * string-creation functions, which are for internal use, assume that the
 * argument is properly-encoded already.
 *
 * creation for INTERNAL use:
 *      stringvar_new()
 *      stringvar_newn()
 *      stringvar_from_ascii()
 *      stringvar_from_format()
 *      stringvar_from_vformat()
 *
 * creation from USER literal:
 *      stringvar_from_source()
 *
 * Other creation methods are from existing string or using the string-
 * writer API.
 */
#include <evilcandy/debug.h>
#include <evilcandy/string_writer.h>
#include <evilcandy/string_reader.h>
#include <evilcandy/vm.h>
#include <evilcandy/err.h>
#include <evilcandy/errmsg.h>
#include <evilcandy/ewrappers.h>
#include <evilcandy/evc_ctype.h>
#include <evilcandy/global.h>
#include <evilcandy/hash.h>
#include <evilcandy/types/array.h>
#include <evilcandy/types/dict.h>
#include <evilcandy/types/string.h>
#include <evilcandy/types/tuple.h>
#include <evilcandy/types/number_types.h>
#include <internal/uarg.h>
#include <internal/codec.h>
#include <internal/errmsg.h>
#include <internal/type_registry.h>
#include <internal/types/string.h>
#include <internal/types/number_types.h>
#include <internal/types/sequential_types.h>
#include <lib/helpers.h>
#include <lib/buffer.h>
#include <lib/utf8.h>

#include <errno.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>

/* user argument limits */
enum {
        JUST_MAX = 10000,
        PRECISION_MAX = 30,
        PAD_MAX = JUST_MAX,
};

enum {
        /* flags arg to stringvar_from_points, see comments there */
        SF_COPY = 0x0001,

        /* Other common flags to methods' helper functions */
        SF_RIGHT        = 0x0010,       /* from the right (not left) */
        SF_CENTER       = 0x0020,       /* both left and right */
        SF_SUPPRESS     = 0x0040,       /* suppress errors */
        SF_COUNT        = 0x0080,       /* count rather than find */
};

#define V2STR(v)                ((struct stringvar_t *)(v))


/* **********************************************************************
 *                      Common Helpers
 ***********************************************************************/

static inline bool
isdigit_ascii(long pt)
{
        return pt >= '0' && pt <= '9';
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

static long
string_getidx(Object *str, size_t idx)
{
        bug_on((int)idx < 0);
        bug_on(idx >= seqvar_size(str));
        return string_getidx_raw(string_width(str), string_data(str), idx);
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

/* used to determine if a buffer needs to be down-sized */
static size_t
find_max_width(size_t width, const void *unicode,
               size_t starti, size_t stopi)
{
        size_t i;
        long maxchr = 0;
        long thresh;

        switch (width) {
        case 1:
                return 1; /* can't downsize */
        case 2:
                thresh = 0xff;
                break;
        case 4:
                thresh = 0xffff;
                break;
        default:
                thresh = 0;
                bug();
        }

        if (width == 1)
                return 1;

        for (i = starti; i < stopi; i++) {
                long v = string_getidx_raw(width, unicode, i);
                if (v > thresh)
                        return width;
                if (v > maxchr)
                        maxchr = v;
        }
        return maxchr_to_width(maxchr);
}

/* ONLY CALL THIS IF YOU ALREADY CONFIRMED THAT @p IS ALL ASCII */
static Object *
stringvar_from_ascii_(void *p, size_t len)
{
        Object *ret = var_new(&StringType);
        struct stringvar_t *vs = V2STR(ret);

        vs->s_width     = 1;
        vs->s_ascii_len = len;

        vs->s = emalloc(len + 1);
        if (len)
                memcpy(vs->s, p, len);
        vs->s[len] = '\0';

        vs->s_hash      = 0;
        vs->s_ascii     = 1;
        vs->s_unicode   = vs->s;
        seqvar_set_size(ret, len);
        return ret;
}

/*
 * @enclen and @ascii may be NULL if these results are not needed.
 *
 * @enclen:     stores number of bytes in result minus the terminating
 *              nulchar; if this is different from strlen(result), it
 *              is because the result could store embedded nulchars.
 * @isascii:    Will store 'true' if @points are all ascii.
 */
static char *
string_encode_points_utf8(void *points, size_t width, size_t len,
                          size_t *enclen, int *isascii)
{
        size_t i;
        struct buffer_t buf;
        int ascii = 1;

        buffer_init(&buf);
        for (i = 0; i < len; i++) {
                long point = string_getidx_raw(width, points, i);
                if (point < 128) {
                        buffer_putc_strict(&buf, point);
                        continue;
                }
                ascii = 0;
                if (point < 0x7ff) {
                        buffer_putc(&buf, 0xc0 | (point >> 6));
                        buffer_putc(&buf, 0x80 | (point & 0x3f));
                } else if (point < 0xffff) {
                        buffer_putc(&buf, 0xe0 | (point >> 12));
                        buffer_putc(&buf, 0x80 | ((point >> 6) & 0x3f));
                        buffer_putc(&buf, 0x80 | (point & 0x3f));
                } else {
                        buffer_putc(&buf, 0xf0 | (point >> 18));
                        buffer_putc(&buf, 0x80 | ((point >> 12) & 0x3f));
                        buffer_putc(&buf, 0x80 | ((point >> 6) & 0x3f));
                        buffer_putc(&buf, 0x80 | (point & 0x3f));
                }
        }

        if (enclen)
                *enclen = buffer_size(&buf);
        if (isascii)
                *isascii = ascii;
        return buffer_trim(&buf);
}

static Object *
stringvar_from_points(void *points, size_t width,
                      size_t len, unsigned int flags)
{
        Object *ret;
        int ascii;
        size_t ascii_len;
        struct stringvar_t *vs;
        char *utf8;

        bug_on(!!len && !points);
        bug_on(!len && (!!points && *(char *)points != '\0'));

        /* We're early so we have to check if mpty exists */
        if (!len && STRCONST_ID(mpty))
                return VAR_NEW_REF(STRCONST_ID(mpty));

        utf8 = string_encode_points_utf8(points, width, len,
                                         &ascii_len, &ascii);

        ret = var_new(&StringType);
        vs = V2STR(ret);

        vs->s_width     = width;
        vs->s_ascii_len = ascii_len;
        vs->s           = utf8;
        vs->s_hash      = 0;
        vs->s_ascii     = ascii;
        seqvar_set_size(ret, len);
        if (ascii) {
                if (points && !(flags & SF_COPY))
                        efree(points);
                vs->s_unicode = vs->s;
        } else {
                if (!!(flags & SF_COPY)) {
                        vs->s_unicode = ememdup(points, len * width);
                } else {
                        vs->s_unicode = points;
                }
        }
        bug_on(!vs->s_unicode);
        bug_on(!vs->s);
        return ret;
}

/* There used to be flags, hence the `f', but they went obsolete */
static Object *
stringvar_newf(char *cstr, size_t size)
{
        struct string_writer_t wr;
        ssize_t n, max = size;
        struct utf8_state_t utf8_state;

        if (!cstr)
                cstr = "";

        /*
         * Fast path: if ASCII, then no decoding is necessary.
         *
         * XXX REVISIT: This assumes that most calls to stringvar_newf
         * are for internal functions which only pass ASCII strings.
         * But if that's not the case, then the mem_is_ascii() call
         * is a real time-waster.
         */
        if (mem_is_ascii(cstr, size))
                return stringvar_from_ascii_(cstr, size);

        string_writer_init(&wr, 1);
        memset(&utf8_state, 0, sizeof(utf8_state));
        n = string_writer_decode(&wr, cstr, max, -1, CODEC_UTF8, &utf8_state);
        /*
         * We're only getting called from internal code which should
         * know not to send us invalid input.
         */
        bug_on(n != max);
        (void)n;
        return stringvar_from_writer(&wr);
}

/*
 * helper to stringvar_from_source -
 *      interpolate a string's backslash escapes
 */
static enum result_t
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
                        case '0':
                                string_writer_append(&wr, '\0');
                                continue;
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
                                if (v >= 256)
                                        goto err;
                                string_writer_append(&wr, v);
                                continue;
                        }

                        if (c == 'x' || c == 'X') {
                                unsigned int v;
                                if (!isxdigit(s[0]) || !isxdigit(s[1]))
                                        goto err;
                                v = x2bin(s[0]) * 16 + x2bin(s[1]);

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
        unsigned char *tbuf, *src, *dst, *end;
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

static bool
match_here_anywidth(Object *haystack, Object *needle, size_t idx)
{
        size_t i, n = seqvar_size(needle);
        bug_on(!n);
        bug_on(idx + n > seqvar_size(haystack));
        for (i = 0; i < n; i++) {
                if (string_getidx(haystack, idx + i)
                    != string_getidx(needle, i)) {
                        return false;
                }
        }
        return true;
}


#define STRING_HELPER(name_) name_##_8
#define TYPE uint8_t
#include "string_include.c.h"
#undef STRING_HELPER
#undef TYPE

#define STRING_HELPER(name_) name_##_16
#define TYPE uint16_t
#include "string_include.c.h"
#undef STRING_HELPER
#undef TYPE

#define STRING_HELPER(name_) name_##_32
#define TYPE uint16_t
#include "string_include.c.h"
#undef STRING_HELPER
#undef TYPE


/* only call this if needle has made the necessary resize */
static ssize_t
find_or_count_by_width(const void *haystack,
                       const void *needle,
                       size_t start,
                       size_t stop,
                       size_t needle_len,
                       unsigned int flags,
                       size_t width)
{
        ssize_t (*func)(const void *, const void *,
                        size_t, size_t, size_t, unsigned int);
        switch (width) {
        case 1:
                func = find_idx_8;
                break;
        case 2:
                func = find_idx_16;
                break;
        case 4:
                func = find_idx_32;
                break;
        default:
                bug();
                return -1;
        }
        return func(haystack, needle, start, stop, needle_len, flags);
}

/*
 * Return value depends on flags:
 * If SF_COUNT set, returns #of needle in haystack, 0...5 zillion
 * Otherwise,
 *      IF SF_RIGHT set, returns first instance of needle in haystack
 *      from the right.
 *      Otherwise returns first instance of needle in haystack from
 *      the left.
 * If needle not found in haystack and we're not counting, returns -1
 * but does not throw exception.
 */
static ssize_t
find_or_count_within(Object *haystack, Object *needle,
                     unsigned int flags, size_t startpos, size_t endpos)
{
        ssize_t hwid, nwid, hlen, nlen, idx;

        bug_on(!isvar_string(haystack));
        bug_on(!isvar_string(needle));
        bug_on(startpos > endpos);

        hlen = endpos - startpos;
        nlen = seqvar_size(needle);
        hwid = string_width(haystack);
        nwid = string_width(needle);

        bug_on(!nlen);

        if (hwid < nwid || hlen < nlen)
                return !!(flags & SF_COUNT) ? 0 : -1;
        const unsigned char *nsrc, *hsrc;
        if (hwid != nwid)
                nsrc = widen_buffer(needle, hwid);
        else
                nsrc = string_data(needle);
        hsrc = string_data(haystack);

        idx = find_or_count_by_width(
                                hsrc, nsrc, startpos, endpos,
                                nlen, flags, hwid);

        if (nsrc != string_data(needle))
                efree((void *)nsrc);

        bug_on(idx >= (ssize_t)seqvar_size(haystack));
        return idx;
}

static inline ssize_t
find_or_count(Object *haystack, Object *needle, unsigned int flags)
{
        return find_or_count_within(haystack, needle,
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
        evc_sprintf(fmtbuf, sizeof(fmtbuf), "%%%s%s%d.%d%c",
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
 *         [{flags}{pad}.{precision}{conversion}]
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
 *          if arg is IntType (or FloatType, converted to IntType)
 *              x Hexadecimal, lowercase
 *              X Hexadecimal, uppercase
 *              d Integer, signed
 *              u Integer, unsigned
 *          if arg is FloatType (or IntType, converted to FloatType)
 *              f [-]ddd.dddd notation
 *              e Exponential notation with lower-case e
 *              E Exponential notation with upper-case E
 *          if arg is StringType
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
                                        bug_on((long)point < 0);
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

/* enough UAPI functions use this argument format */
#define FMT_1ARG_STRING(fname)  ("<s>[<s>!]{!}:" fname)
#define FMT_2ARG_STRING(fname)  ("<s>[<s><s>!]{!}:" fname)

static Object *
string_getprop_length(Object *self)
{
        bug_on(!isvar_string(self));
        return intvar_new(seqvar_size(self));
}

static Object *
string_getprop_nbytes(Object *self)
{
        bug_on(!isvar_string(self));
        return intvar_new(string_nbytes(self));
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
        Object *self, *list;

        if (vm_getargs(fr, "<s><[]>{!}", &self, &list) == RES_ERROR)
                return ErrorVar;

        return string_format(self, list);
}

static Object *
string_lrstrip_(Frame *fr, unsigned int flags, const char *fmt)
{
        Object *self, *arg, *ret;
        void *src, *skip;
        size_t srclen, skiplen, width, src_newend, src_newstart;
        struct stringvar_t *vsrc, *vskip;

        arg = NULL;
        if (vm_getargs(fr, fmt, &self, &arg) == RES_ERROR)
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
                void *newp = voidp_add(vsrc->s_unicode,
                                       vsrc->s_width * src_newstart);
                ret = stringvar_from_points(newp, vsrc->s_width,
                                            src_newend - src_newstart, SF_COPY);
        }
        if (src != vsrc->s_unicode)
                efree(src);
        if (skip != vskip->s_unicode)
                efree(skip);
        return ret;
}

#define string_lrstrip(fr, flg, fname) \
        string_lrstrip_(fr, flg, "<s>[|<s>!]{!}:" fname)

/*
 * lstrip()             no args implies whitespace
 * lstrip(charset)      charset is string
 */
static Object *
string_lstrip(Frame *fr)
{
        return string_lrstrip(fr, 0, "lstrip");
}

/*
 * rstrip()             no args implies whitespace
 * rstrip(charset)      charset is string
 */
static Object *
string_rstrip(Frame *fr)
{
        return string_lrstrip(fr, SF_RIGHT, "rstrip");
}

/*
 *  strip()             no args implies whitespace
 *  strip(charset)      charset is string
 */
static Object *
string_strip(Frame *fr)
{
        return string_lrstrip(fr, SF_CENTER, "strip");
}

#undef string_lrstrip

static Object *
string_replace(Frame *fr)
{
        struct string_writer_t wr;
        Object *haystack, *needle, *repl;
        ssize_t hlen, nlen, hwid, nwid, start;
        unsigned char *hsrc, *nsrc;
        ssize_t wr_wid, idx;

        if (vm_getargs(fr, FMT_2ARG_STRING("replace"),
                       &haystack, &needle, &repl) == RES_ERROR) {
                return ErrorVar;
        }

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
        idx = find_or_count_by_width(
                        hsrc, nsrc, start, hlen, nlen, 0, wr_wid);
        while (idx >= 0) {
                if (idx > start) {
                        string_writer_appendb(&wr, hsrc + start,
                                              hwid, idx - start);
                }
                string_writer_append_strobj(&wr, repl);
                start = idx + nlen;
                if (start + nlen > hlen)
                        break;
                idx = find_or_count_by_width(
                                hsrc, nsrc, start, hlen, nlen, 0, wr_wid);
        }
        if (start < hlen)
                string_writer_appendb(&wr, hsrc + start, hwid, hlen - start);
        if (nsrc != string_data(needle))
                efree(nsrc);
        return stringvar_from_writer(&wr);
}

static Object *
string_lrjust_(Frame *fr, unsigned int flags, const char *fmt)
{
        Object *self;
        struct string_writer_t wr;
        ssize_t newlen, selflen, padlen;

        bug_on((flags & (SF_CENTER|SF_RIGHT)) == (SF_CENTER|SF_RIGHT));

        if (vm_getargs(fr, fmt, &self, &newlen) == RES_ERROR)
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

#define string_lrjust(fr, flg, fname) \
        string_lrjust_(fr, flg, "<s>[z!]{!}:" fname)

/* rjust(amt)   integer arg */
static Object *
string_rjust(Frame *fr)
{
        return string_lrjust(fr, SF_RIGHT, "rjust");
}

/* rjust(amt)    integer arg */
static Object *
string_ljust(Frame *fr)
{
        return string_lrjust(fr, 0, "ljust");
}

static Object *
string_center(Frame *fr)
{
        return string_lrjust(fr, SF_CENTER, "center");
}

#undef string_lrjust

/*
 * XXX REVISIT: replace with iter_xxx() API,
 * allows for more variable types as input.
 */
static Object *
string_join(Frame *fr)
{
        struct string_writer_t wr;
        Object *self, *arg;
        size_t i, n, width;

        if (vm_getargs(fr, "<s>[<*>!]{!}:join", &self, &arg) == RES_ERROR)
                return ErrorVar;

        if (!isvar_seq_readable(arg)) {
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

        if (vm_getargs(fr, "<s>[!]{!}:capitalize", &self) == RES_ERROR)
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
string_count(Frame *fr)
{
        int count;
        Object *haystack, *needle;

        if (vm_getargs(fr, "<s>[<s>!]{!}:count", &haystack, &needle)
            == RES_ERROR) {
                return ErrorVar;
        }

        count = find_or_count(haystack, needle, SF_COUNT);
        return intvar_new(count);
}

static Object *
string_starts_or_ends_with_(Frame *fr, unsigned int flags, const char *fmt)
{
        Object *self, *arg;
        size_t i, n1, n2, start;

        if (vm_getargs(fr, fmt, &self, &arg) == RES_ERROR)
                return ErrorVar;

        n1 = seqvar_size(self);
        n2 = seqvar_size(arg);

        if (n2 > n1)
                goto hasnt;

        start = !!(flags & SF_RIGHT) ? n1 - n2 : 0;

        for (i = 0; i < n2; i++) {
                long p1, p2;

                p1 = string_getidx(self, i + start);
                p2 = string_getidx(arg, i);
                if (p1 != p2)
                        goto hasnt;
        }
        return gbl_new_bool(true);

hasnt:
        return gbl_new_bool(false);
}

#define string_starts_or_ends_with(fr, flg, fname) \
        string_starts_or_ends_with_(fr, flg, FMT_1ARG_STRING(fname))

static Object *
string_endswith(Frame *fr)
{
        return string_starts_or_ends_with(fr, SF_RIGHT, "endswith");
}

static Object *
string_startswith(Frame *fr)
{
        return string_starts_or_ends_with(fr, 0, "startswith");
}

#undef string_starts_or_ends_with

static Object *
string_expandtabs(Frame *fr)
{
        Object *self;
        int i, col, nextstop, tabsize;
        struct string_writer_t wr;
        size_t n;

        tabsize = 8;
        if (vm_getargs(fr, "<s>[!]{|i}:expandtabs", &self,
                        STRCONST_ID(tabsize), &tabsize) == RES_ERROR) {
                return ErrorVar;
        }

        if (tabsize < 0)
                tabsize = 0;

        n = seqvar_size(self);
        if (n > INT_MAX) {
                err_setstr(NotImplementedError,
                        "expandtabs cannot process string of length %ld",
                        (long)n);
                return ErrorVar;
        }

        string_writer_init(&wr, string_width(self));
        col = 0;
        nextstop = tabsize;
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
string_index_or_find_(Frame *fr, unsigned int flags, const char *fmt)
{
        Object *self, *arg;
        ssize_t res;

        if (vm_getargs(fr, fmt, &self, &arg) == RES_ERROR)
                return ErrorVar;
        res = find_or_count(self, arg, flags);
        if (res < 0) {
                if (!(flags & SF_SUPPRESS)) {
                        err_setstr(ValueError, "substring not found");
                        return ErrorVar;
                }
                return intvar_new(-1LL);
        }
        return intvar_new(res);
}

#define string_index_or_find(fr, flg, fname) \
        string_index_or_find_(fr, flg, FMT_1ARG_STRING(fname))

static Object *
string_find(Frame *fr)
{
        return string_index_or_find(fr, SF_SUPPRESS, "find");
}

static Object *
string_index(Frame *fr)
{
        return string_index_or_find(fr, 0, "index");
}

static Object *
string_rfind(Frame *fr)
{
        return string_index_or_find(fr, SF_SUPPRESS | SF_RIGHT, "rfind");
}

static Object *
string_rindex(Frame *fr)
{
        return string_index_or_find(fr, SF_RIGHT, "rindex");
}

#undef string_index_or_find

static Object *
string_lrpartition_(Frame *fr, unsigned int flags, const char *fmt)
{
        Object *self, *arg, *tup, **td;
        ssize_t idx;

        if (vm_getargs(fr, fmt, &self, &arg) == RES_ERROR)
                return ErrorVar;
        if (seqvar_size(arg) == 0) {
                err_setstr(ValueError, "Separator may not be empty");
                return ErrorVar;
        }

        idx = find_or_count(self, arg, flags);

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
                unsigned char *points = string_data(self);
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

#define string_lrpartition(fr, flg, fname) \
        string_lrpartition_(fr, flg, FMT_1ARG_STRING(fname))

static Object *
string_partition(Frame *fr)
{
        return string_lrpartition(fr, 0, "partition");
}

static Object *
string_rpartition(Frame *fr)
{
        return string_lrpartition(fr, SF_RIGHT, "rpartition");
}

#undef string_lrpartition

static Object *
string_removelr_(Frame *fr, unsigned int flags, const char *fmt)
{
        Object *self, *arg;
        size_t idx, pos, start, stop;

        if (vm_getargs(fr, fmt, &self, &arg) == RES_ERROR)
                return ErrorVar;
        if (seqvar_size(arg) > seqvar_size(self))
                goto return_self;

        pos = !!(flags & SF_RIGHT)
                ? seqvar_size(self) - seqvar_size(arg) : 0;

        idx = find_or_count_within(self, arg, flags, 0,
                                   seqvar_size(self));
        if (idx != pos)
                goto return_self;

        if (!!(flags & SF_RIGHT)) {
                start = 0;
                stop = pos;
        } else {
                start = seqvar_size(arg);
                stop = seqvar_size(self);
        }
        return stringvar_from_substr(self, start, stop);

return_self:
        VAR_INCR_REF(self);
        return self;
}

#define string_removelr(fr, flg, fname) \
        string_removelr_(fr, flg, FMT_1ARG_STRING(fname))

static Object *
string_removeprefix(Frame *fr)
{
        return string_removelr(fr, 0, "removeprefix");
}

static Object *
string_removesuffix(Frame *fr)
{
        return string_removelr(fr, SF_RIGHT, "removesuffix");
}

#undef string_removelr

static ssize_t
split_combine_right(Object *self, Object *sep, Object *array, ssize_t idx)
{
        ssize_t seplen = seqvar_size(sep);
        while (idx - seplen >= 0) {
                idx -= seplen;
                if (match_here_anywidth(self, sep, idx)) {
                        array_append(array, STRCONST_ID(mpty));
                } else {
                        idx += seplen;
                        break;
                }
        }
        return idx;
}

static ssize_t
split_combine_left(Object *self, Object *sep, Object *array,
                   ssize_t idx, ssize_t endpos)
{
        ssize_t seplen = seqvar_size(sep);
        while (idx + seplen < endpos) {
                idx += seplen;
                if (match_here_anywidth(self, sep, idx)) {
                        array_append(array, STRCONST_ID(mpty));
                } else {
                        idx -= seplen;
                        break;
                }
        }
        return idx;
}

static Object *
string_lrsplit(Frame *fr, unsigned int flags)
{
        enum { LRSPLIT_STACK_SIZE = 64, };
        Object *self, *separg, *array;
        const char *fmt;
        int maxsplit;
        bool combine;
        size_t startpos, seplen, endpos;
        bool right;

        separg = NULL;
        maxsplit = -1;
        combine = false;
        fmt = !!(flags & SF_RIGHT)
                ? "<s>[!]{|<s>i}:rsplit"
                : "<s>[!]{|<s>i}:split";
        if (vm_getargs(fr, fmt, &self, STRCONST_ID(sep), &separg,
                       STRCONST_ID(maxsplit), &maxsplit) == RES_ERROR) {
                return ErrorVar;
        }

        if (!separg) {
                combine = true;
                separg = STRCONST_ID(spc);
        }
        if (seqvar_size(separg) == 0) {
                err_setstr(ValueError, "Separator may not be empty");
                return ErrorVar;
        }

        array = arrayvar_new(0);
        startpos = 0;
        seplen = seqvar_size(separg);
        endpos = seqvar_size(self);
        right = !!(flags & SF_RIGHT);
        while (maxsplit-- != 0) {
                ssize_t substr_start, substr_end, idx;

                idx = find_or_count_within(self, separg, flags,
                                           startpos, endpos);
                if (idx < 0)
                        break;

                if (right) {
                        substr_start = idx + seplen;
                        substr_end = endpos;
                } else {
                        substr_start = startpos;
                        substr_end = idx;
                }

                if (substr_start != substr_end) {
                        Object *substr = stringvar_from_substr(
                                                        self,
                                                        substr_start,
                                                        substr_end);
                        array_append(array, substr);
                        VAR_DECR_REF(substr);
                }

                if (!combine) {
                        if (right) {
                                idx = split_combine_right(self, separg,
                                                          array, idx);
                        } else {
                                idx = split_combine_left(self, separg,
                                                         array, idx,
                                                         endpos);
                        }
                }

                if (right)
                        endpos = idx;
                else
                        startpos = idx + seplen;
        }
        if (startpos != endpos) {
                Object *substr = stringvar_from_substr(self, startpos, endpos);
                array_append(array, substr);
                VAR_DECR_REF(substr);
        }
        if (!!(flags & SF_RIGHT))
                array_reverse(array);
        return array;
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

static Object *
string_splitlines(Frame *fr)
{
        Object *self, *ret;
        int keepends = 0;
        size_t i, j, n;

        if (vm_getargs(fr, "<s>[!]{|i}:splitlines", &self,
                       STRCONST_ID(keepends), &keepends) == RES_ERROR) {
                return ErrorVar;
        }

        ret = arrayvar_new(0);

        n = seqvar_size(self);
        i = j = 0;
        while (i < n) {
                size_t eol;
                long pt = string_getidx(self, i);
                /* TODO: support non-ASCII line breaks */
                if (!(pt == '\r' || pt == '\n')) {
                        i++;
                        continue;
                }

                eol = i;
                i++;
                if (pt == '\r' && string_getidx(self, i) == '\n')
                        i++;
                if (keepends)
                        eol = i;

                if (eol == j) {
                        array_append(ret, STRCONST_ID(mpty));
                } else {
                        Object *substr;

                        substr = stringvar_from_substr(self, j, eol);
                        array_append(ret, substr);
                        VAR_DECR_REF(substr);
                }

                j = i;
        }
        if (i > j) {
                Object *substr = stringvar_from_substr(self, j, i);
                array_append(ret, substr);
                VAR_DECR_REF(substr);
        }
        return ret;
}

static Object *
string_zfill(Frame *fr)
{
        Object *self;
        ssize_t nz;
        size_t src_size;
        struct string_writer_t wr;
        long plusminus;

        if (vm_getargs(fr, "<s>[z!]{!}:zfill", &self, &nz) == RES_ERROR)
                return ErrorVar;

        src_size = seqvar_size(self);
        nz -= src_size;

        plusminus = string_getidx(self, 0);
        string_writer_init(&wr, 1);
        if (plusminus == '-' || plusminus == '+') {
                string_writer_append(&wr, plusminus);
                src_size--;
        }
        while (nz-- > 0)
                string_writer_append(&wr, '0');
        string_writer_appendb(&wr, string_data(self),
                              string_width(self), src_size);
        return stringvar_from_writer(&wr);
}


/*
 *      str.isXXXX() functions and helpers
 */

/* FIXME: should get funcname to use for vm_getargs() */
static Object *
string_is1(Frame *fr, bool (*cb)(Object *))
{
        Object *ret, *self;

        if (vm_getargs(fr, "<s>[!]{!}", &self) == RES_ERROR)
                return ErrorVar;

        if (seqvar_size(self) == 0) {
                ret = gbl_new_bool(false);
        } else {
                ret = gbl_new_bool(cb(self));
        }
        return ret;
}

static Object *
string_is2(Frame *fr, bool (*tst)(unsigned long c))
{
        bool bret;
        Object *self;

        if (vm_getargs(fr, "<s>[!]{!}", &self) == RES_ERROR)
                return ErrorVar;

        /* To be overwritten if false */
        bret = true;
        if (seqvar_size(self) == 0) {
                bret = false;
        } else {
                size_t i, n = seqvar_size(self);
                for (i = 0; i < n; i++) {
                        long point = string_getidx(self, i);
                        if (!tst(point)) {
                                bret = false;
                                break;
                        }
                }
        }
        return gbl_new_bool(bret);
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
        Object *self;
        bool ascii;

        if (vm_getargs(fr, "<s>[!]{!}:isascii", &self) == RES_ERROR)
                return ErrorVar;

        ascii = V2STR(self)->s_ascii;
        return gbl_new_bool(ascii);
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

        if (vm_getargs(fr, "<s>[!]{!}:title", &self) == RES_ERROR)
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

        if (vm_getargs(fr, "<s>[!]{!}:title", &self) == RES_ERROR)
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

static struct type_method_t string_methods[] = {
        {"capitalize",   string_capitalize},
        {"center",       string_center},
        {"count",        string_count},
        {"endswith",     string_endswith},
        {"expandtabs",   string_expandtabs},
        {"find",         string_find},
        {"format",       string_format_mthd},
        {"index",        string_index},
        {"isalnum",      string_isalnum},
        {"isalpha",      string_isalpha},
        {"isascii",      string_isascii_mthd},
        {"isdigit",      string_isdigit},
        {"isident",      string_isident},
        {"isprintable",  string_isprintable},
        {"isspace",      string_isspace},
        {"istitle",      string_istitle},
        {"isupper",      string_isupper},
        {"join",         string_join},
        {"ljust",        string_ljust},
        {"lower",        string_lower},
        {"lstrip",       string_lstrip},
        {"partition",    string_partition},
        {"removeprefix", string_removeprefix},
        {"removesuffix", string_removesuffix},
        {"replace",      string_replace},
        {"rfind",        string_rfind},
        {"rindex",       string_rindex},
        {"rjust",        string_rjust},
        {"rpartition",   string_rpartition},
        {"rsplit",       string_rsplit},
        {"rstrip",       string_rstrip},
        {"split",        string_split},
        {"splitlines",   string_splitlines},
        {"startswith",   string_startswith},
        {"strip",        string_strip},
        {"swapcase",     string_swapcase},
        {"title",        string_title},
        {"upper",        string_upper},
        {"zfill",        string_zfill},
        {NULL, NULL},
};

/* **********************************************************************
 *                      Operator Methods
 * *********************************************************************/

static Object *
string_str(Object *v)
{
        struct buffer_t b;
        size_t i, n, npts;
        enum { Q = '\'', BKSL = '\\' };

        bug_on(!isvar_string(v));

        /*
         * Since we're deliberately creating an all-ASCII string, we'll
         * use the less cumbersome buffer_t instead of string_writer_t.
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
                } else if (c == '\0') {
                        buffer_putc(&b, BKSL);
                        buffer_putc(&b, '0');
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
                } else if (c >= 128) {
                        char buf[10];
                        buffer_putc(&b, BKSL);
                        if (c > 0xffffu) {
                                bug_on(!utf8_valid_unicode(c));
                                buffer_putc(&b, 'U');
                                evc_sprintf(buf, sizeof(buf), "%08x", (int)c);
                                buffer_puts(&b, buf);
                        } else if (c > 0xffu) {
                                buffer_putc(&b, 'u');
                                evc_sprintf(buf, sizeof(buf), "%04x", (int)c);
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
        npts = buffer_size(&b);
        return stringvar_from_points(buffer_trim(&b), 1, npts, 0);
}

static void
string_reset(Object *str)
{
        struct stringvar_t *vs = V2STR(str);
        if (vs->s_unicode != vs->s && vs->s_unicode != NULL)
                efree(vs->s_unicode);
        if (vs->s)
                efree(vs->s);
}

Object *
string_cat(Object *a, Object *b)
{
        struct string_writer_t wr;
        size_t wa, wb;

        if (!b)
                return stringvar_from_ascii("");

        if (!isvar_string(b)) {
                err_setstr(TypeError,
                           "Mismatched types for + operation");
                return ErrorVar;
        }
        /* The concatenation width will be wider of the two */
        wa = string_width(a);
        wb = string_width(b);
        string_writer_init(&wr, wa > wb ? wa : wb);
        string_writer_append_strobj(&wr, a);
        string_writer_append_strobj(&wr, b);
        return stringvar_from_writer(&wr);
}

static bool
string_cmpeq(Object *a, Object *b)
{
        bug_on(!isvar_string(a) || !isvar_string(b));
        return string_eq(a, b);
}

static enum result_t
string_cmp(Object *a, Object *b, int *result)
{
        const char *sa, *sb;
        size_t na, nb;

        /*
         * XXX: I probably debugged all the corner cases where a string's
         * Unicode array has a larger width than it needs.  So no need
         * to go into the C strings.
         */
        bug_on(!isvar_string(a) || !isvar_string(b));
        sa = string_cstring(a);
        sb = string_cstring(b);
        if (sa == sb) {
                *result = 0;
                return RES_OK;
        }

        na = string_nbytes(a);
        nb = string_nbytes(b);
        if (na != nb) {
                int ret;
                size_t cmpsize = na > nb ? nb : na;
                if (!cmpsize) {
                        *result = na ? 1 : -1;
                        return RES_OK;
                }
                ret = memcmp(sa, sb, cmpsize);
                if (!ret) {
                        *result = na > nb ? 1 : -1;
                        return RES_OK;
                }
                *result = ret;
                return RES_OK;
        }

        if (!na)
                *result = nb ? -1 : 0;
        else
                *result = memcmp(sa, sb, na);
        return RES_OK;
}

static bool
string_cmpz(Object *a)
{
        bug_on(!isvar_string(a));

        /* treat "" same as NULL in comparisons */
        return seqvar_size(a) == 0;
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
                return ErrorVar;
        }
}

/* comparisons, helpers to string_getslice */
static bool slice_cmp_lt(ssize_t a, ssize_t b) { return a < b; }
static bool slice_cmp_gt(ssize_t a, ssize_t b) { return a > b; }

static Object *
string_getslice(Object *str, ssize_t start, ssize_t stop, ssize_t step)
{
        struct string_writer_t wr;
        bool (*cmp)(ssize_t, ssize_t);

        if (start == stop)
                return stringvar_from_ascii("");

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
string_getitem(Object *str, size_t idx)
{
        long point;

        bug_on(!isvar_string(str));
        bug_on(idx >= seqvar_size(str));

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

        idx = find_or_count(str, substr, SF_SUPPRESS);
        return idx >= 0;
}

static Object *
string_from_encoded_obj(Object *obj, int encoding)
{
        size_t n;
        const unsigned char *data;

        if (!isvar_bytes(obj)) {
                err_setstr(TypeError,
                        "string() cannot encode %s object", typestr(obj));
                return ErrorVar;
        }
        n = seqvar_size(obj);
        if (n == 0)
                return VAR_NEW_REF(STRCONST_ID(mpty));

        data = bytes_get_data(obj);
        return stringvar_from_binary(data, n, encoding);
}

static Object *
string_create(Frame *fr)
{
        int encoding = -1;
        int encoding2 = -1;
        Object *what = NULL;
        Object *codec_dict = gbl_borrow_mns_dict(MNS_CODEC);
        bug_on(!codec_dict);
        if (vm_getargs(fr, "[|<*>e!]{|e}:string", &what,
                       codec_dict, &encoding,
                       STRCONST_ID(encoding),
                       codec_dict, &encoding2) == RES_ERROR) {
                return ErrorVar;
        }
        if (encoding >= 0 && encoding2 >= 0) {
                err_doublearg("encoding");
                return ErrorVar;
        }
        if (encoding < 0)
                encoding = encoding2;
        if (!what)
                return VAR_NEW_REF(STRCONST_ID(mpty));
        if (isvar_string(what))
                return VAR_NEW_REF(what);
        if (encoding >= 0)
                return string_from_encoded_obj(what, encoding);
        return var_str(what);
}

/* **********************************************************************
 *                          Codec helpers
 * *********************************************************************/

static ssize_t
string_writer_decode_latin1(struct string_writer_t *wr,
                            const void *data, size_t n, ssize_t max)
{
        const unsigned char *u8 = data;
        const unsigned char *end;

        if (n > max && max >= 0)
                n = max;
        end = u8 + n;

        while (u8 < end && max != 0) {
                unsigned int c = *u8++;
                string_writer_append(wr, c);
                max--;
        }
        return n;
}

static ssize_t
string_writer_decode_ascii(struct string_writer_t *wr,
                           const void *data, size_t n, ssize_t max)
{
        const unsigned char *u8 = data;
        const unsigned char *end;

        if (n > max && max >= 0)
                n = max;
        end = u8 + n;

        u8 += string_writer_get_ascii(wr, u8, n);
        if (u8 != end) {
                err_ord(CODEC_ASCII, u8[0]);
                return -1;
        }
        return n;
}

static void
err_decode(int codec, const char *why)
{
        char codecbuf[16];
        if (!why)
                why = "invalid data"; /* why else? */
        codec_str(codec, codecbuf, sizeof(codecbuf));

        /* TODO: replace with CodecError */
        err_setstr(ValueError, "cannot decode data as %s: %s",
                   codecbuf, why);
}

/* ie ((c & 0xc0) == 0x80), but usu. one less instruction */
static inline bool is_continuation(int c) { return c < 0xc0 && c >= 0x80; }

static ssize_t
string_writer_decode_utf8(struct string_writer_t *wr, const void *data,
                          size_t n, size_t max, struct utf8_state_t *state)
{
        static const unsigned long UNICODE_MAXCHR = 0x10ffffu;
        const unsigned char *u8 = data;
        const unsigned char *end = u8 + n;
        unsigned long point = 0;
        if (state && state->state != UTF8_STATE_ASCII && max != 0) {
                /* This means that calling code is not dealing with it */
                bug_on(state->state == UTF8_STATE_ERR);

                point = state->point;
                while (u8 < end && state->state != UTF8_STATE_ASCII) {
                        bug_on(state->state > UTF8_STATE_GET3);
                        if (!is_continuation(u8[0]))
                                goto err_continuation_byte;
                        point = (point << 6) & (u8[0] & 0x3fu);
                        state->buf[state->state - 1] = u8[0];
                        state->state--;
                        u8++;
                }

                if (state->state != UTF8_STATE_ASCII) {
                        /* end before full decode */
                        bug_on(u8 != end);
                        return (size_t)(u8 - (unsigned char *)data);
                }

                if (point > UNICODE_MAXCHR)
                        goto err_ord;
                string_writer_append(wr, point);
                memset(state, 0, sizeof(*state));
                max--;
        }

        while (u8 < end && max != 0) {
                unsigned int c = *u8;
                if (c < 128) {
                        /* TODO: aligned version of this */
                        string_writer_append(wr, c);
                        max--;
                        u8++;
                        continue;
                }

                if (c >= 0xf0u && c < 0xf8u) {
                        if (end - u8 < 4)
                                break;
                        if (!is_continuation(u8[1]) ||
                            !is_continuation(u8[2]) ||
                            !is_continuation(u8[3])) {
                                goto err_continuation_byte;
                        }
                        point = ((c & 0x07u) << 18)
                                | ((u8[1] & 0x3fu) << 12)
                                | ((u8[2] & 0x3fu) << 6)
                                | (u8[3] & 0x3fu);
                        if (point > UNICODE_MAXCHR)
                                goto err_ord;
                        u8 += 4;
                        max--;
                        string_writer_append(wr, point);
                } else if (c >= 0xe0u && c < 0xf0u) {
                        /*
                         * Check for surrogate pairs.  This can't be done
                         * after the decoding, because they won't decode
                         * that way.
                         */
                        if (end - u8 < 3)
                                break;
                        if (!is_continuation(u8[1]) ||
                            !is_continuation(u8[2])) {
                                goto err_continuation_byte;
                        }

                        point = ((c & 0x0fu) << 12)
                                | ((u8[1] & 0x3fu) << 6)
                                | (u8[2] & 0x3fu);
                        if (point > UNICODE_MAXCHR)
                                goto err_ord;
                        if (point >= 0xd800u && point <= 0xdfffu) {
                                /* invalid surrogate pair */
                                goto err_surrogate;
                        }
                        u8 += 3;
                        max--;
                        string_writer_append(wr, point);
                } else if (c >= 0xc0u && c < 0xe0) {
                        if (end - u8 < 2)
                                break;
                        if (!is_continuation(u8[1]))
                                goto err_continuation_byte;
                        point = ((c & 0x1fu) << 6) | (u8[1] & 0x3fu);
                        if (point > UNICODE_MAXCHR)
                                goto err_ord;
                        u8 += 2;
                        max--;
                        string_writer_append(wr, point);
                } else {
                        /* [0x80, 0xc0) are invalid UTF8 */
                        goto err_start_byte;
                }
        }

        if (u8 != end && state && max != 0) {
                state->cookiepos = end - u8;
                unsigned int c = *u8++;
                if (c >= 0xf0u && c < 0xf8u) {
                        state->point = c & 0x07u;
                        state->state = UTF8_STATE_GET3;
                } else if (c >= 0xe0u && c < 0xf0u) {
                        state->point = c & 0x0fu;
                        state->state = UTF8_STATE_GET2;
                } else if (c >= 0xc0u && c < 0xe0) {
                        state->point = c & 0x1fu;
                        state->state = UTF8_STATE_GET1;
                } else {
                        bug();
                }
                while (u8 < end) {
                        c = *u8;
                        if (!is_continuation(u8[0]))
                                goto err_continuation_byte;
                        state->buf[3 - state->state] = c;
                        state->point = (state->point << 6) | (c & 0x3fu);
                        state->state--;
                        /*
                         * We're only in this loop because we didn't have
                         * enough bytes left, so this would be a bug.
                         */
                        bug_on(state->state == UTF8_STATE_ASCII);
                        u8++;
                }
        }
        return (size_t)(u8 - (unsigned char *)data);

err_start_byte:
        err_decode(CODEC_UTF8, "bad start byte");
        return -1;

err_continuation_byte:
        err_decode(CODEC_UTF8, "bad continuation byte");
        return -1;

err_ord:
        err_ord(CODEC_UTF8, point);
        return -1;

err_surrogate:
        err_decode(CODEC_UTF8, "invalid surrogate pair");
        return -1;
}

/* **********************************************************************
 *                           String iterator
 * *********************************************************************/

struct string_iterator_t {
        Object base;
        Object *target;
        size_t i;
};

#define O2SIT(o)        ((struct string_iterator_t *)(o))

static Object *
string_iter_next(Object *it)
{
        struct string_iterator_t *sit = O2SIT(it);
        if (!sit->target) {
                return NULL;
        } else if (sit->i < seqvar_size(sit->target)) {
                return string_getitem(sit->target, sit->i++);
        } else {
                VAR_DECR_REF(sit->target);
                sit->target = NULL;
                return NULL;
        }
}

static void
string_iter_reset(Object *it)
{
        if (O2SIT(it)->target)
                VAR_DECR_REF(O2SIT(it)->target);
        O2SIT(it)->target = NULL;
}

struct type_t StringIterType = {
        .name           = "string_iterator",
        .reset          = string_iter_reset,
        .size           = sizeof(struct string_iterator_t),
        .iter_next      = string_iter_next,
};

static Object *
string_get_iter(Object *str)
{
        bug_on(!isvar_string(str));
        Object *ret = var_new(&StringIterType);
        O2SIT(ret)->target = VAR_NEW_REF(str);
        O2SIT(ret)->i = 0;
        return ret;
}


/* **********************************************************************
 *                           API functions
 * *********************************************************************/

/**
 * string_encode_utf8 - Get a UTF8-encoded C string from a string object
 * @str: String to encode
 * @size: (output) number of encoded bytes, minus the nulchar terminator.
 *        This could be different from strlen(result) if the result has
 *        embedded nulchars.
 *
 * Return: nulchar-terminated UTF8-encoded C string.
 */
char *
string_encode_utf8(Object *str, size_t *size)
{
        /* XXX: if @str has invalid utf8, is this a bug or error? */
        bug_on(!isvar_string(str));
        return string_encode_points_utf8(string_data(str),
                                         string_width(str),
                                         seqvar_size(str), size, NULL);
}

/**
 * string_writer_append_strobj - Copy decoded string @str
 *                               into string_writer @wr.
 */
void
string_writer_append_strobj(struct string_writer_t *wr, Object *str)
{
        string_writer_appendb(wr, string_data(str),
                              string_width(str), seqvar_size(str));
}

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

Object *
stringvar_from_writer(struct string_writer_t *wr)
{
        size_t width, len;
        void *buf = string_writer_finish(wr, &width, &len);
        return stringvar_from_points(buf, width, len, 0);
}

/*
 * stringvar_from_substr - Create a new string out of a substring.
 * @old: String to get a substring out of
 * @start: Start index
 * @stop: Last index plus one
 *
 * Return: Functional equivalent of "old[start:stop:1]"
 */
Object *
stringvar_from_substr(Object *old, size_t start, size_t stop)
{
        size_t width, maxwidth, len;
        void *buf;
        Object *ret;

        bug_on(start >= seqvar_size(old));
        bug_on(stop > seqvar_size(old));
        bug_on(stop < start);

        if (stop == start) {
                if (STRCONST_ID(mpty))
                        return VAR_NEW_REF(STRCONST_ID(mpty));
                return stringvar_from_ascii("");
        }

        width = string_width(old);
        len  = stop - start;
        buf = voidp_add(string_data(old), start * width);
        /*
         * Quickly scan unicode to determine if width of substring
         * does not need to shrink.  If not, we can call the quicker
         * stringvar_from_points.
         *
         * We need the Unicode-array's width to be form-fitting, not
         * only because it saves space, but two strings with the
         * same array of Unicode points need to have matching widths,
         * or else a comparison could yield a false negative.
         */
        maxwidth = find_max_width(width, buf, start, stop);
        if (maxwidth != width) {
                /*
                 * Substring does not contain widest chars in @old.
                 * We need to resize, which getslice will do.
                 */
                bug_on(maxwidth > width);
                ret = string_getslice(old, start, stop, 1);
        } else {
                ret = stringvar_from_points(buf, width, len, SF_COPY);
        }
        return ret;
}

/*
 * This is functionally equivalent to stringvar_new(), except that it
 * bypasses all the UTF8-decoding steps, to more quickly create a string
 * object.  This does not perform any checks to ensure @cstr is all-ASCII
 * For that reason...
 *
 *      ***ONLY CALL THIS FOR OBVIOUSLY ALL-ASCII STRING LITERALS***
 */
Object *
stringvar_from_ascii(const char *cstr)
{
        return stringvar_from_ascii_((void *)cstr, strlen(cstr));
}

/**
 * stringvar_new - Get a string var from a UTF-8-encoded or ASCII string.
 * @cstr: C-string to set string to, or NULL to make empty string
 *
 * Return: new string var containing a copy of @cstr.
 */
Object *
stringvar_new(const char *cstr)
{
        return stringvar_newf((char *)cstr, strlen(cstr));
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
        return stringvar_newf((char *)cstr, n);
}

/**
 * stringvar_from_vformat - Like stringvar_from_format, but
 *                          with va_list instead.
 * @fmt: Format string to use, same format as standard C printf
 */
Object *
stringvar_from_vformat(const char *fmt, va_list ap)
{
        size_t len;
        struct buffer_t b;
        char *s;
        Object *ret;

        buffer_init(&b);
        buffer_vprintf(&b, fmt, ap);
        len = buffer_size(&b);
        s = buffer_trim(&b);
        ret = stringvar_newf(s, len);
        efree(s);
        return ret;
}

/**
 * stringvar_from_format - Get a string from a formatted C string
 * @fmt: Format string to use, same format as standard C printf
 */
Object *
stringvar_from_format(const char *fmt, ...)
{
        Object *ret;
        va_list ap;

        va_start(ap, fmt);
        ret = stringvar_from_vformat(fmt, ap);
        va_end(ap);

        return ret;
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
 * An error will occur if a Unicode escape sequence is out of bounds
 * (either greater 0x10FFFF or an invalid surrogate pair).
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
 * string_writer_decode - Decode a binary array into a string writer
 * @wr:         String writer, which MUST have been initialized before
 *              this call.  This function assumes appends to it.
 * @data:       Data to decode
 * @datalen:    Size of @data in bytes
 * @max:        Maximum number of unicode points to retrieve, or -1 to
 *              retrieve as much as the input or the buffer size will
 *              allow.
 * @codec:      A CODEC_xxx enum
 * @state:      If non-NULL, a UTF8 state machine.  This is so multiple
 *              calls to this function can be made on non-contiguous
 *              buffers.  An encoding might straddle the end of one
 *              buffer and the start of the next.  Initialize this state
 *              machine before the first call.
 *
 * Return: - @n if every character was decoded.
 *         - @n if there were some stragglers, but @state is non-NULL.
 *           If the function returns and state->state != UTF8_STATE_ASCII,
 *           then there were stragglers.
 *         - some positive value less than @n if there exist some
 *           stragglers and @state is NULL.
 *           straggling undecodable characters at the end of @data
 *           (perhaps because a multi-byte encoding straddles the end of
 *           this buffer and the start of the next)
 *         - -1 if the data cannot be decoded according to @codec.
 *           An exception will be thrown in this case.
 */
ssize_t
string_writer_decode(struct string_writer_t *wr, const void *data,
                     size_t datalen, ssize_t max, int codec,
                     struct utf8_state_t *state)
{
        switch (codec) {
        case CODEC_ASCII:
                return string_writer_decode_ascii(wr, data, max, datalen);
        case CODEC_LATIN1:
                return string_writer_decode_latin1(wr, data, max, datalen);
        default:
                bug();
                return -1;
        case CODEC_UTF8:
                return string_writer_decode_utf8(wr, data,
                                                 datalen, max, state);
        }
}

/* XXX: Unused outside this file */
/**
 * stringvar_from_binary - Encode a binary array into a string
 * @data: Binary data to encode.  This need not be nulchar terminated,
 *        but it may not contain data that would result in any invalid
 *        Unicode points (including equal to zero) according to the
 *        encoding being used.  This data will be copied.
 * @n:    Size of @data in bytes
 * @encoding: A CODEC_xxx enumeration
 *
 * Return: A new string object, or ErrorVar.
 *
 * TODO: This is used by file reads, where a UTF-8-encoded Unicode point
 * could straddle the end of one buffer and the start of another, so we
 * need args for straggler_ptr suppress_errors in case that happens.
 */
Object *
stringvar_from_binary(const void *data, size_t n, int encoding)
{
        struct string_writer_t wr;
        ssize_t res;
        struct utf8_state_t state;

        memset(&state, 0, sizeof(state));

        string_writer_init(&wr, 1);
        res = string_writer_decode(&wr, data, n, -1, encoding, &state);
        if (res != n || state.state != UTF8_STATE_ASCII) {
                /* Do not accept stragglers */
                if (!err_occurred()) {
                        err_setstr(ValueError,
                                   "data contains invalid characters");
                }
                string_writer_destroy(&wr);
                return ErrorVar;
        }
        return stringvar_from_writer(&wr);
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
                                if ((ssize_t)newpos < 0)
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

/* Call string_hash(), not this */
hash_t
string_update_hash__(Object *v)
{
        V2STR(v)->s_hash = fnv_hash(string_cstring(v), string_nbytes(v));
        return V2STR(v)->s_hash;
}

static hash_t
string_hash_cb(Object *v)
{
        return string_hash(v);
}

/**
 * string_search - C hook for string searching
 */
ssize_t
string_search(Object *haystack, Object *needle, size_t startpos)
{
        return find_or_count_within(haystack, needle, 0,
                                    startpos, seqvar_size(haystack));
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
        .add    = string_cat,
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
        .cmpeq  = string_cmpeq,
        .reset  = string_reset,
        .prop_getsets = string_prop_getsets,
        .create = string_create,
        .hash   = string_hash_cb,
        .get_iter = string_get_iter,
};


