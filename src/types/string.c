/* string.c - Built-in methods for string data types */
/*
 * XXX REVISIT: Get my mind off of C strings.  This isn't 1977.
 * Allow nulchars in the middle of the strings--I already know
 * the string's length--and stop relying so much on string.h functions.
 */
#include "types_priv.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>

/* user argument limits */
enum {
        JUST_MAX = 10000,
        PRECISION_MAX = 30,
        PAD_MAX = JUST_MAX,
};

struct stringvar_t {
        struct var_t base;
        char *s;        /* the actual C string */
        bool s_imm;     /* true if immortal */
        struct utf8_info_t s_info;
};

#define V2STR(v)                ((struct stringvar_t *)(v))
#define V2CSTR(v)               (V2STR(v)->s)
#define STRING_LENGTH(str)      (V2STR(str)->s_info.enc_len)
#define STRING_NBYTES(str)      (V2STR(str)->s_info.ascii_len)


/* **********************************************************************
 *                      Common Helpers
 ***********************************************************************/

enum {
        /* flags arg to stringvar_newf, see comments there */
        SF_COPY         = 1,
        SF_IMMORTAL     = 2,
};

/*
 * Flags are:
 *      SF_COPY         make a copy of @cstr
 *      SF_IMMORTAL     use @cstr exactly and do not free on reset
 *      0               use @cstr exactly and free on reset
 */
static struct var_t *
stringvar_newf(char *cstr, unsigned int flags)
{
        struct stringvar_t *vs;
        struct var_t *ret;

        if (!cstr) {
                bug_on(!!(flags & SF_IMMORTAL));
                cstr = "";
                flags |= SF_COPY;
        }

        /* These cannot both be set */
        bug_on((flags & (SF_COPY|SF_IMMORTAL)) == (SF_COPY|SF_IMMORTAL));

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
        vs->s_imm = !!(flags & SF_IMMORTAL);
        utf8_scan(cstr, &vs->s_info);
        return ret;
}

static inline struct var_t *
string_copy__(struct var_t *str)
{
        return stringvar_newf(V2CSTR(str), SF_COPY);
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
padwrite(struct buffer_t *buf, int padc, size_t padlen)
{
        while (padlen--)
                buffer_putc(buf, padc);
}

/*
 * XXX: This looks redundant, but the alternative--writing to a
 * temporary buffer before deciding whether to right justify or not--
 * is maybe slower.  Needs testing, temporary buffers don't have the
 * overhead of buffer_putc.
 */
static void
swap_pad(struct buffer_t *buf, size_t count, size_t padlen)
{
        char *right = buf->s + buf->p - 1;
        char *left = right - padlen;
        if (!buf->s)
                return;
        while (count--) {
                int c = *right;
                *right-- = *left;
                *left-- = c;
        }
}

static void
format2_i_helper(struct buffer_t *buf, unsigned long long ival, int base, int xchar)
{
        long long v;
        if (!ival)
                return;

        if (ival >= base)
                format2_i_helper(buf, ival / base, base, xchar);

        v = ival % base;
        if (v >= 10)
                v += xchar;
        else
                v += '0';

        buffer_putc(buf, (int)v);
}

static void
format2_i(struct buffer_t *buf, struct var_t *arg,
          int conv, bool rjust, int padc, size_t padlen, int precision)
{
        int base;
        size_t count;
        int xchar = 'A' - 10;
        long long ival = numvar_toint(arg);

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

        count = buf->p;
        if (!ival) {
                buffer_putc(buf, '0');
        } else {
                unsigned long long uval;
                if (conv == 'd' && ival < 0) {
                        buffer_putc(buf, '-');
                        uval = -ival;
                } else {
                        uval = (unsigned long long)ival;
                }
                format2_i_helper(buf, uval, base, xchar);
        }

        count = buf->p - count;
        if (count < padlen) {
                padlen -= count;
                padwrite(buf, padc, padlen);
                if (rjust) {
                        swap_pad(buf, count, padlen);
                }
        }
}

/* helper to format2_e - print exponent */
static void
format2_e_exp(struct buffer_t *buf, int exp)
{
        if (exp == 0)
                return;
        if (exp > 0)
                format2_e_exp(buf, exp / 10);
        buffer_putc(buf, (exp % 10) + '0');
}

/* FIXME: subtle difference from above, try to eliminate one of these */
static void
format2_f_ihelper(struct buffer_t *buf, unsigned int v)
{
        if (v >= 10)
                format2_f_ihelper(buf, v / 10);
        buffer_putc(buf, (v % 10) + '0');
}

static void
format2_e(struct buffer_t *buf, struct var_t *arg,
          int conv, bool rjust, int padc, size_t padlen, int precision)
{
        int exp = 0;
        int sigfig = 0;
        double ival;
        /* checked before this call */
        double v = numvar_tod(arg);
        double dv = v;

        size_t count = buf->p;

        if (dv < 0.0) {
                buffer_putc(buf, '-');
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
        buffer_putc(buf, (int)ival + '0');
        ++sigfig;

        buffer_putc(buf, '.');
        while (sigfig < precision) {
                dv *= 10.0;
                dv = modf(dv, &ival);
                buffer_putc(buf, (int)ival + '0');
                sigfig++;
        }

        /* print exponent */
        bug_on(conv != 'e' && conv != 'E');
        buffer_putc(buf, conv);
        if (exp < 0) {
                buffer_putc(buf, '-');
                exp = -exp;
        } else {
                buffer_putc(buf, '+');
        }
        /* %e requires exponent to be at least two digits */
        if (exp < 10)
                buffer_putc(buf, '0');

        if (exp == 0)
                buffer_putc(buf, '0');
        else
                format2_e_exp(buf, exp);

        if (!rjust)
                padc = ' ';
        count = buf->p - count;
        if (count < padlen) {
                padlen -= count;
                padwrite(buf, padc, padlen);
                if (rjust) {
                        swap_pad(buf, count, padlen);
                }
        }
}

static void
format2_f(struct buffer_t *buf, struct var_t *arg,
          int conv, bool rjust, int padc, size_t padlen, int precision)
{
        double v = numvar_tod(arg);
        bool have_dot = false;
        size_t count = buf->p;

        if (!isfinite(v)) {
                if (isnan(v)) {
                        buffer_puts(buf, "nan");
                } else {
                        if (v == -INFINITY)
                                buffer_putc(buf, '-');
                        buffer_puts(buf, "inf");
                }
        } else {
                double iptr, rem, scale;
                int i;

                if (v < 0.0) {
                        buffer_putc(buf, '-');
                        v = -v;
                }
                for (scale = 1.0, i = 0; i < precision; i++)
                        scale *= 0.1;
                v += scale * 0.5;
                rem = modf(v, &iptr);

                format2_f_ihelper(buf, (unsigned int)iptr);

                if (precision > 0) {
                        have_dot = true;
                        buffer_putc(buf, '.');
                        while (precision--) {
                                rem *= 10.0;
                                buffer_putc(buf, (int)rem + '0');
                                rem = modf(rem, &iptr);
                        }
                }
        }

        if (!rjust && !have_dot)
                padc = ' ';
        count = buf->p - count;
        if (count < padlen) {
                padlen -= count;
                padwrite(buf, padc, padlen);
                if (rjust) {
                        swap_pad(buf, count, padlen);
                }
        }
}

static void
format2_s(struct buffer_t *buf, struct var_t *arg,
          int conv, bool rjust, int padc, size_t padlen, int precision)
{
        const char *src;
        size_t count, count_bytes;

        src = V2CSTR(arg);
        if (!src) {
                count = count_bytes = 6;
                buffer_puts(buf, "(null)");
        } else {
                count_bytes = buf->p;
                count = utf8_strlen(src);
                buffer_puts(buf, src);
                count_bytes = buf->p - count_bytes;
        }

        /*
         * This is trickier than the numbers' case, because the string
         * length might get confused by Unicode characters.
         */
        if (count < padlen) {
                padlen -= count;
                padwrite(buf, padc, padlen);
                if (rjust)
                        swap_pad(buf, count_bytes, padlen);
        }
}

static size_t
format2_helper(struct vmframe_t *fr, struct buffer_t *buf, const char *s, int argi)
{
        const char *ssave = s;
        bool rjust = true;
        int padc = ' ';
        size_t padlen = 0;
        int precision = 6;
        struct var_t *v = vm_get_arg(fr, argi);
        int conv;

        /*
         * XXX should warn, but if this is in a user loop or function,
         * I don't want to flood the output.
         */
        if (!v)
                return 0;

        /* get flags.  @cbuf already filled with next char */
        for (;;) {
                switch (*s) {
                case '-':
                        rjust = false;
                        s++;
                        continue;
                case '0':
                        padc = '0';
                        s++;
                        continue;
                }
                break;
        }
        if (isdigit((int)*s)) {
                while (isdigit((int)*s)) {
                        padlen = 10 * padlen + (*s - '0');
                        s++;
                }
        }
        if (*s == '.') {
                s++;
                if (isdigit((int)*s)) {
                        precision = 0;
                        while (isdigit((int)*s)) {
                                precision = 10 * precision + (*s - '0');
                                s++;
                        }
                }
        }
        if (padlen >= PAD_MAX)
                padlen = PAD_MAX;
        if (precision >= PRECISION_MAX)
                precision = PRECISION_MAX;

        switch ((conv = *s++)) {
        case 'x':
        case 'X':
        case 'd':
        case 'u':
                if (!isnumvar(v))
                        return 0;
                format2_i(buf, v, conv, rjust, padc, padlen, precision);
                break;
        case 'f':
                if (!isnumvar(v))
                        return 0;
                format2_f(buf, v, conv, rjust, padc, padlen, precision);
                break;
        case 'e':
        case 'E':
                if (!isnumvar(v))
                        return 0;
                format2_e(buf, v, conv, rjust, padc, padlen, precision);
                break;
        case 's':
                if (!isvar_string(v))
                        return 0;
                format2_s(buf, v, conv, rjust, padc, padlen, precision);
                break;
        default:
        case '\0':
                /* don't try to format this */
                return 0;
        }

        return s - ssave;
}

/*
 * format2(...)         var args
 *
 * Lightweight printf-like alternative to format()
 *
 * Accepts %[{flags}{pad}.{precision}]{conversion}
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
static struct var_t *
string_format2(struct vmframe_t *fr)
{
        char cbuf[5];
        size_t size;
        struct var_t *ret, *self = get_this(fr);
        const char *s;
        struct buffer_t b;
        int argi = 0;

        bug_on(!isvar_string(self));
        s = V2CSTR(self);

        if (!s || *s == '\0') {
                VAR_INCR_REF(self);
                return self;
        }

        buffer_init(&b);
        while ((size = utf8_strgetc(s, cbuf)) != 0) {
                s += size;
                if (size > 1) {
                        buffer_puts(&b, cbuf);
                } else if (cbuf[0] == '%') {
                        size = utf8_strgetc(s, cbuf);
                        if (size == 1 && cbuf[0] == '%') {
                                s++;
                                buffer_putc(&b, '%');
                        } else {
                                s += format2_helper(fr, &b, s, argi++);
                        }
                } else  {
                        buffer_putc(&b, cbuf[0]);
                }
        }

        ret = stringvar_newf(b.s, 0);
        return ret;
}

/* **********************************************************************
 *              Built-in type methods (not format2)
 * *********************************************************************/

/* len() (no args)
 * returns length of C string stored in self
 *
 * Gotta call it something different, "string_length" already taken
 */
static struct var_t *
string_length_method(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        bug_on(!isvar_string(self));
        return intvar_new(STRING_LENGTH(self));
}

static bool
string_format_helper(struct vmframe_t *fr, char **src,
                     struct buffer_t *t, int *lastarg)
{
        char vbuf[64];
        char *s = *src;
        int la = *lastarg;
        struct var_t *q = NULL;
        ++s;
        if (*s == '}') {
                q = frame_get_arg(fr, la++);
        } else if (isdigit(*s)) {
                char *endptr;
                int i = strtoul(s, &endptr, 10);
                if (*endptr == '}') {
                        q = frame_get_arg(fr, i);
                        la = i + 1;
                        s = endptr;
                }
        }
        if (!q)
                return false;

        if (isvar_int(q)) {
                sprintf(vbuf, "%lld", intvar_toll(q));
                buffer_puts(t, vbuf);
        } else if (isvar_float(q)) {
                sprintf(vbuf, "%g", floatvar_tod(q));
                buffer_puts(t, vbuf);
        } else if (isvar_empty(q)) {
                buffer_puts(t, "(null)");
        } else if (isvar_string(q)) {
                buffer_puts(t, V2CSTR(q));
        } else {
                return false;
        }
        *lastarg = la;
        *src = s;
        return true;
}

/*
 * format(...)
 * returns type string
 */
static struct var_t *
string_format(struct vmframe_t *fr)
{
        static struct buffer_t t;
        struct var_t *self = get_this(fr);
        int lastarg = 0;
        char *s, *self_s;
        bug_on(!isvar_string(self));

        self_s = V2CSTR(self);
        if (!self_s)
                return stringvar_newf("", 0);

        buffer_init(&t);
        for (s = self_s; *s != '\0'; s++) {
                if (*s == '{' &&
                    string_format_helper(fr, &s, &t, &lastarg)) {
                        continue;
                }
                buffer_putc(&t, *s);
        }

        return stringvar_newf(t.s, 0);
}

/* toint() (no args)
 * returns int
 */
static struct var_t *
string_toint(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        long long i = 0LL;
        char *s;

        bug_on(!isvar_string(self));
        s = V2CSTR(self);
        /* XXX Revisit: throw exception if not numerical? */
        if (s) {
                int errno_save = errno;
                char *endptr;
                i = strtoll(s, &endptr, 0);
                if (endptr == s || errno)
                        i = 0;
                errno = errno_save;
        }
        return intvar_new(i);
}

/*
 * tofloat()  (no args)
 * returns float
 */
static struct var_t *
string_tofloat(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        double f = 0.;
        char *s;
        bug_on(!isvar_string(self));
        s = V2CSTR(self);
        if (s) {
                int errno_save = errno;
                char *endptr;
                f = strtod(s, &endptr);
                if (endptr == s || errno)
                        f = 0.;
                errno = errno_save;
        }
        return floatvar_new(f);
}

/*
 * lstrip()             no args implies whitespace
 * lstrip(charset)      charset is string
 */
static struct var_t *
string_lstrip(struct vmframe_t *fr)
{
        const char *charset;
        struct var_t *arg = frame_get_arg(fr, 0);
        struct var_t *self = get_this(fr);
        struct buffer_t b;
        bug_on(!isvar_string(self));

        /* arg may be NULL, else it must be string */
        if (arg && arg_type_check(arg, &StringType) != 0)
                return ErrorVar;

        buffer_init(&b);
        buffer_puts(&b, V2CSTR(self));
        charset = arg ? V2CSTR(arg) : NULL;
        buffer_lstrip(&b, charset);

        return stringvar_newf(b.s, 0);
}

/*
 * rstrip()             no args implies whitespace
 * rstrip(charset)      charset is string
 */
static struct var_t *
string_rstrip(struct vmframe_t *fr)
{
        const char *charset;
        struct var_t *arg = frame_get_arg(fr, 0);
        struct var_t *self = get_this(fr);
        struct buffer_t b;
        bug_on(!isvar_string(self));

        /* arg may be NULL, else it must be string */
        if (arg && arg_type_check(arg, &StringType) != 0)
                return ErrorVar;

        buffer_init(&b);
        buffer_puts(&b, V2CSTR(self));
        charset = arg ? V2CSTR(arg) : NULL;
        buffer_rstrip(&b, charset);
        return stringvar_newf(b.s, 0);
}

/*
 *  strip()             no args implies whitespace
 *  strip(charset)      charset is string
 */
static struct var_t *
string_strip(struct vmframe_t *fr)
{
        const char *charset;
        struct var_t *arg = frame_get_arg(fr, 0);
        struct var_t *self = get_this(fr);
        struct buffer_t b;
        bug_on(!isvar_string(self));

        /* arg may be NULL, else it must be string */
        if (arg && arg_type_check(arg, &StringType) != 0)
                return ErrorVar;

        buffer_init(&b);
        buffer_puts(&b, V2CSTR(self));
        charset = arg ? V2CSTR(arg) : NULL;
        buffer_rstrip(&b, charset);
        buffer_lstrip(&b, charset);
        return stringvar_newf(b.s, 0);
}

static struct var_t *
string_replace(struct vmframe_t *fr)
{
        struct buffer_t b;
        struct var_t *self    = get_this(fr);
        struct var_t *vneedle = frame_get_arg(fr, 0);
        struct var_t *vrepl   = frame_get_arg(fr, 1);
        char *haystack, *needle, *end;
        size_t needle_len;

        bug_on(!isvar_string(self));
        bug_on(!vneedle || !vrepl);

        if (arg_type_check(vneedle, &StringType) != 0)
                return ErrorVar;
        if (arg_type_check(vrepl, &StringType) != 0)
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
        return stringvar_newf(b.s, 0);
}

/* XXX Superfluous, the way we do things now, remove? */
static struct var_t *
string_copy(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        bug_on(!isvar_string(self));
        return string_copy__(self);
}

/* rjust(amt)   integer arg */
static struct var_t *
string_rjust(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        struct var_t *arg = vm_get_arg(fr, 0);
        size_t len;
        long long just;

        if (arg_type_check(arg, &IntType) != 0)
                return ErrorVar;

        just = intvar_toll(arg);
        if (just < 0 || just >= JUST_MAX) {
                err_setstr(RuntimeError, "Range limit error");
                return ErrorVar;
        }

        len = STRING_LENGTH(self);
        if (len < just) {
                /*
                 * TODO: "need_len = just + (bytes_len - len) + 1"
                 * Need to replace buffer_t API, allocate this in
                 * one chunk, and memset... much faster than this
                 * in the case of "rjust(gazillion)"
                 */
                struct buffer_t b;
                buffer_init(&b);
                just -= len;
                while (just--)
                        buffer_putc(&b, ' ');
                buffer_puts(&b, V2CSTR(self));
                return stringvar_newf(b.s, 0);
        } else {
                return string_copy__(self);
        }
}

/* rjust(amt)    integer arg */
static struct var_t *
string_ljust(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        struct var_t *arg = vm_get_arg(fr, 0);
        size_t len;
        long long just;

        if (arg_type_check(arg, &IntType) != 0)
                return ErrorVar;

        just = intvar_toll(arg);
        if (just < 0 || just >= JUST_MAX) {
                err_setstr(RuntimeError, "Range limit error");
                return ErrorVar;
        }

        len = STRING_LENGTH(self);
        if (len < just) {
                struct buffer_t b;
                buffer_init(&b);
                buffer_puts(&b, V2CSTR(self));
                just -= len;
                while (just--)
                        buffer_putc(&b, ' ');
                return stringvar_newf(b.s, 0);
        } else {
                return string_copy__(self);
        }
}

static struct var_t *
string_join(struct vmframe_t *fr)
{
        struct buffer_t b;
        struct var_t *self = get_this(fr);
        struct var_t *arg = vm_get_arg(fr, 0);
        char *joinstr;
        struct var_t *elem;
        int idx;

        if ((joinstr = V2CSTR(self)) == NULL)
                joinstr = "";

        if (arg_type_check(arg, &ArrayType) != 0)
                return ErrorVar;

        idx = 0;
        elem = array_getitem(arg, idx);
        if (!elem)
                return stringvar_newf(joinstr, SF_COPY);

        if (!isvar_string(elem)) {
                err_setstr(RuntimeError,
                           "string.join method may only join lists of strings");
                VAR_DECR_REF(elem);
                return ErrorVar;
        }

        buffer_init(&b);
        buffer_puts(&b, V2CSTR(elem));
        VAR_DECR_REF(elem);
        for (;;) {
                idx++;
                elem = array_getitem(arg, idx);
                if (!elem)
                        break;
                if (joinstr[0] != '\0')
                        buffer_puts(&b, joinstr);
                buffer_puts(&b, V2CSTR(elem));
                VAR_DECR_REF(elem);
        }
        return stringvar_newf(b.s, 0);
}

static struct type_inittbl_t string_methods[] = {
        V_INITTBL("len",     string_length_method, 0, 0),
        V_INITTBL("format",  string_format, 0, -1),
        V_INITTBL("format2", string_format2, 0, -1),
        V_INITTBL("ljust",   string_ljust, 1, 0),
        V_INITTBL("rjust",   string_rjust, 1, 0),
        V_INITTBL("toint",   string_toint, 0, 0),
        V_INITTBL("tofloat", string_tofloat, 0, 0),
        V_INITTBL("lstrip",  string_lstrip, 0, 1),
        V_INITTBL("rstrip",  string_rstrip, 0, 1),
        V_INITTBL("replace", string_replace, 2, 2),
        V_INITTBL("strip",   string_strip, 0, 1),
        V_INITTBL("copy",    string_copy, 0, 0),
        V_INITTBL("join",    string_join, 1, 0),
        TBLEND,
};


/* **********************************************************************
 *                      Operator Methods
 * *********************************************************************/

static void
string_reset(struct var_t *str)
{
        struct stringvar_t *vs = V2STR(str);
        if (!vs->s_imm)
                free(vs->s);
}

static struct var_t *
string_cat(struct var_t *a, struct var_t *b)
{
        char *catstr;
        char *lval, *rval;
        size_t rlen, llen;

        if (!isvar_string(b)) {
                err_setstr(RuntimeError,
                           "Mismatched types for + operation");
                return NULL;
        }

        lval = V2CSTR(a);
        llen = STRING_NBYTES(a);

        rval = V2CSTR(b);
        rlen = STRING_NBYTES(b);

        catstr = emalloc(llen + rlen + 1);
        memcpy(catstr, lval, llen);
        memcpy(catstr + llen, rval, rlen + 1);
        return stringvar_newf(catstr, 0);
}

/* helper to string_cmp */
static int
compare_strings(const char *a, const char *b)
{
        if (!a || !b)
                return a != b;
        return !!strcmp(a, b);
}

static int
string_cmp(struct var_t *a, struct var_t *b)
{
        if (isvar_string(b)) {
                if (V2CSTR(a) == V2CSTR(b))
                        return 0;
                if (STRING_NBYTES(a) != STRING_NBYTES(b))
                        return 1;
                return compare_strings(V2CSTR(a), V2CSTR(b));
        } else {
                return 1;
        }
}

static bool
string_cmpz(struct var_t *a)
{
        char *s = V2CSTR(a);
        /* treat "" same as NULL in comparisons */
        return s ? s[0] == '\0' : true;
}

static struct var_t *
string_cp(struct var_t *v)
{
        return string_copy__(v);
}

/* .getitem sequence method for string  */
static struct var_t *
string_getitem(struct var_t *str, int idx)
{
        char cbuf[5];
        char *src;

        bug_on(!isvar_string(str));
        src = V2CSTR(str);
        if (!src || src[0] == '\0')
                return NULL;

        idx = index_translate(idx, STRING_LENGTH(str));
        if (idx < 0)
                return NULL;

        if (V2STR(str)->s_info.enc != STRING_ENC_UTF8) {
                /* ASCII, Latin1, or some undecoded binary */
                cbuf[0] = src[idx];
                cbuf[1] = '\0';
        } else {
                if (utf8_subscr_str(src, idx, cbuf) < 0) {
                        /* code managing .s_info has bug */
                        bug();
                        return NULL;
                }
        }
        return stringvar_newf(cbuf, SF_COPY);
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
struct var_t *
stringvar_new(const char *cstr)
{
        return stringvar_newf((char *)cstr, SF_COPY);
}

/**
 * stringvar_from_immortal - Get a string that contains an immortal string
 * @immstr: C-string already returned from literal().
 *
 * Return: new string var containing @immstr exactly.  @immstr will be
 *      protected from free() when the return value's destructor function
 *      is called.
 */
struct var_t *
stringvar_from_immortal(const char *immstr)
{
        return stringvar_newf((char *)immstr, SF_IMMORTAL);
}

/*
 * WARNING!! This does not produce a reference! Whatever you are doing
 * with the return value, do it now.  Treat it as READ-ONLY.
 *
 * FIXME: This is not thread safe, and "do it quick" is not a good enough
 * solution.
 */
char *
string_get_cstring(struct var_t *str)
{
        bug_on(!isvar_string(str));
        return V2CSTR(str);
}

/**
 * string_from_file - get a line from file and put in in a string var
 * @fp:         File to read from
 * @delim:      Delimiter to read to.
 * @stuff_delim: True to include the delimiter with the string, false to leave
 *              it out (it will not be ungetc'd).
 *
 * Return: the line as a string var
 */
struct var_t *
string_from_file(FILE *fp, int delim, bool stuff_delim)
{
        int c;
        struct buffer_t b;

        buffer_init(&b);
        while ((c = getc(fp)) != delim && c != EOF) {
                buffer_putc(&b, c);
        }
        if (stuff_delim && c == delim)
                buffer_putc(&b, c);
        return stringvar_newf(b.s, 0);
}

/**
 * string_length - Get the length of @str
 *
 * Return:
 * length, in number of CHARACTERS, not bytes.  @str might be UTF-8-
 * encoded.
 */
static size_t
string_length(struct var_t *str)
{
        return STRING_LENGTH(str);
}

struct seq_methods_t string_seq_methods = {
        .getitem        = string_getitem,
        .setitem        = NULL,
        .cat            = string_cat,
        .sort           = NULL,
        .len            = string_length,
};

struct type_t StringType = {
        .name   = "string",
        .opm    = NULL,
        .cbm    = string_methods,
        .mpm    = NULL,
        .sqm    = &string_seq_methods,
        .size   = sizeof(struct stringvar_t),
        .cmp    = string_cmp,
        .cmpz   = string_cmpz,
        .reset  = string_reset,
        .cp     = string_cp,
};


