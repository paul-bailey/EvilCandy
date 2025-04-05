/* string.c - Built-in methods for string data types */
#include "var.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>

#define STRING_LENGTH(str)      ((str)->s->s_info.enc_len)

/* user argument limits */
enum {
        JUST_MAX = 10000,
        PRECISION_MAX = 30,
        PAD_MAX = JUST_MAX,
};

struct string_handle_t {
        struct buffer_t b;
        struct utf8_info_t s_info;
};


/* **********************************************************************
 *                      Common Helpers
 ***********************************************************************/

static inline struct buffer_t *string_buf__(struct var_t *str)
        { return &str->s->b; }

static void
string_handle_reset(void *h)
{
        struct string_handle_t *sh = h;
        buffer_free(&sh->b);
}

static void
string_clear_info(struct utf8_info_t *info)
{
        info->enc_len = info->ascii_len = 0;
        info->enc = STRING_ENC_ASCII;
}

static struct string_handle_t *
new_string_handle(void)
{
        struct string_handle_t *ret = type_handle_new(sizeof(*ret),
                                                string_handle_reset);
        buffer_init(&ret->b);
        string_clear_info(&ret->s_info);
        return ret;
}

static void
string_update_info(struct utf8_info_t *dst, struct utf8_info_t *src)
{
        dst->enc_len += src->enc_len;
        dst->ascii_len += src->ascii_len;

        if (dst->enc == STRING_ENC_ASCII || src->enc == STRING_ENC_UNK)
                dst->enc = src->enc;
}

static void
string_clear(struct var_t *str)
{
        string_clear_info(&str->s->s_info);
        buffer_reset(string_buf__(str));
}

static void
string_puts(struct var_t *str, const char *s)
{
        struct buffer_t *buf = string_buf__(str);
        struct utf8_info_t src_info;

        if (!s)
                return;

        utf8_scan(s, &src_info);
        string_update_info(&str->s->s_info, &src_info);
        buffer_puts(buf, s);
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
        long long ival = arg->magic == TYPE_INT
                         ? arg->i : (long long)arg->f;

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
        double v = arg->magic == TYPE_FLOAT ? arg->f : (double)arg->i;
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
        double v = arg->magic == TYPE_FLOAT ? arg->f : (double)arg->i;
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

        if (arg->magic == TYPE_STRING)
                src = string_get_cstring(arg);
        else    /* TYPE_STRPTR */
                src = arg->strptr;

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
format2_helper(struct buffer_t *buf, const char *s, int argi)
{
        const char *ssave = s;
        bool rjust = true;
        int padc = ' ';
        size_t padlen = 0;
        int precision = 6;
        struct var_t *v = vm_get_arg(argi);
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
                if (v->magic != TYPE_STRING && v->magic != TYPE_STRPTR)
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
static int
string_format2(struct var_t *ret)
{
        char cbuf[5];
        size_t size;
        struct var_t *self = get_this();
        const char *s;
        struct buffer_t *buf;
        int argi = 0;

        bug_on(self->magic != TYPE_STRING);
        s = string_get_cstring(self);

        string_init(ret, 0);
        buf = string_buf__(ret);
        if (!s) {
                buffer_putc(buf, '\0');
                return 0;
        }

        while ((size = utf8_strgetc(s, cbuf)) != 0) {
                s += size;
                if (size > 1) {
                        buffer_puts(buf, cbuf);
                } else if (cbuf[0] == '%') {
                        size = utf8_strgetc(s, cbuf);
                        if (size == 1 && cbuf[0] == '%') {
                                s++;
                                buffer_putc(buf, '%');
                        } else {
                                s += format2_helper(buf, s, argi++);
                        }
                } else  {
                        buffer_putc(buf, cbuf[0]);
                }
        }

        utf8_scan(string_get_cstring(ret), &ret->s->s_info);
        return 0;
}

/* **********************************************************************
 *              Built-in type methods (not format2)
 * *********************************************************************/

/* len() (no args)
 * returns length of C string stored in self
 *
 * Gotta call it something different, "string_length" already taken
 */
static int
string_length_method(struct var_t *ret)
{
        struct var_t *self = get_this();
        bug_on(self->magic != TYPE_STRING);
        integer_init(ret, STRING_LENGTH(self));
        return 0;
}

static bool
string_format_helper(char **src, struct buffer_t *t, int *lastarg)
{
        char vbuf[64];
        char *s = *src;
        int la = *lastarg;
        struct var_t *q = NULL;
        ++s;
        if (*s == '}') {
                q = frame_get_arg(la++);
        } else if (isdigit(*s)) {
                char *endptr;
                int i = strtoul(s, &endptr, 10);
                if (*endptr == '}') {
                        q = frame_get_arg(i);
                        la = i + 1;
                        s = endptr;
                }
        }
        if (!q)
                return false;

        switch (q->magic) {
        case TYPE_INT:
                sprintf(vbuf, "%lld", q->i);
                buffer_puts(t, vbuf);
                break;
        case TYPE_FLOAT:
                sprintf(vbuf, "%g", q->f);
                buffer_puts(t, vbuf);
                break;
        case TYPE_EMPTY:
                buffer_puts(t, "(null)");
                break;
        case TYPE_STRING:
                buffer_puts(t, string_get_cstring(q));
                break;
        default:
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
static int
string_format(struct var_t *ret)
{
        static struct buffer_t t = { 0 };
        struct var_t *self = get_this();
        int lastarg = 0;
        char *s, *self_s;
        bug_on(self->magic != TYPE_STRING);

        buffer_reset(&t);
        self_s = string_get_cstring(self);
        if (!self_s) {
                if (!t.s)
                        buffer_putc(&t, 'a');
                buffer_reset(&t);
                goto done;
        }

        for (s = self_s; *s != '\0'; s++) {
                if (*s == '{' &&
                    string_format_helper(&s, &t, &lastarg)) {
                        continue;
                }
                buffer_putc(&t, *s);
        }

done:
        string_init(ret, t.s);
        return 0;
}

/* toint() (no args)
 * returns int
 */
static int
string_toint(struct var_t *ret)
{
        struct var_t *self = get_this();
        long long i = 0LL;
        char *s;
        bug_on(self->magic != TYPE_STRING);
        s = string_get_cstring(self);
        /* XXX Revisit: throw exception if not numerical? */
        if (s) {
                int errno_save = errno;
                char *endptr;
                i = strtoll(s, &endptr, 0);
                if (endptr == s || errno)
                        i = 0;
                errno = errno_save;
        }
        integer_init(ret, i);
        return 0;
}

/*
 * tofloat()  (no args)
 * returns float
 */
static int
string_tofloat(struct var_t *ret)
{
        struct var_t *self = get_this();
        double f = 0.;
        char *s;
        bug_on(self->magic != TYPE_STRING);
        s = string_get_cstring(self);
        if (s) {
                int errno_save = errno;
                char *endptr;
                f = strtod(s, &endptr);
                if (endptr == s || errno)
                        f = 0.;
                errno = errno_save;
        }
        float_init(ret, f);
        return 0;
}

static const char *
strip_common(struct var_t *ret)
{
        struct var_t *arg = frame_get_arg(0);
        struct var_t *self = get_this();
        bug_on(self->magic != TYPE_STRING);

        /* arg may be NULL, else it must be string */
        if (arg)
                arg_type_check(arg, TYPE_STRING);

        if (!qop_mov(ret, self))
                return NULL;
        return arg ? string_get_cstring(arg) : NULL;
}

/*
 * lstrip()             no args implies whitespace
 * lstrip(charset)      charset is string
 */
static int
string_lstrip(struct var_t *ret)
{
        const char *charset = strip_common(ret);
        buffer_lstrip(string_buf__(ret), charset);
        return 0;
}

/*
 * rstrip()             no args implies whitespace
 * rstrip(charset)      charset is string
 */
static int
string_rstrip(struct var_t *ret)
{
        const char *charset = strip_common(ret);
        buffer_rstrip(string_buf__(ret), charset);
        return 0;
}

/*
 *  strip()             no args implies whitespace
 *  strip(charset)      charset is string
 */
static int
string_strip(struct var_t *ret)
{
        const char *charset = strip_common(ret);
        buffer_rstrip(string_buf__(ret), charset);
        buffer_lstrip(string_buf__(ret), charset);
        return 0;
}

static int
string_replace(struct var_t *ret)
{
        struct var_t *self    = get_this();
        struct var_t *vneedle = frame_get_arg(0);
        struct var_t *vrepl   = frame_get_arg(1);
        char *haystack, *needle, *end;
        size_t needle_len;

        bug_on(self->magic != TYPE_STRING);
        bug_on(!vneedle || !vrepl);

        arg_type_check(vneedle, TYPE_STRING);
        arg_type_check(vrepl, TYPE_STRING);

        /* guarantee ret is string */
        if (ret->magic == TYPE_EMPTY)
                string_init(ret, NULL);
        /* XXX bug, or syntax error? */
        bug_on(ret->magic != TYPE_STRING);

        buffer_reset(string_buf__(ret));

        /* end not technically needed, but in case of match() bugs */
        haystack = string_get_cstring(self);
        end = haystack + STRING_LENGTH(self);
        needle = string_get_cstring(vneedle);
        needle_len = STRING_LENGTH(vneedle);

        if (!haystack || end == haystack) {
                buffer_putc(string_buf__(ret), '\0');
                return 0;
        }

        if (!needle || !needle_len) {
                buffer_puts(string_buf__(ret), string_get_cstring(self));
                return 0;
        }

        while (*haystack && haystack < end) {
                ssize_t size = match(needle, haystack);
                if (size == -1)
                         break;
                buffer_nputs(string_buf__(ret), haystack, size);
                buffer_puts(string_buf__(ret), string_get_cstring(vrepl));
                haystack += size + needle_len;
        }
        bug_on(haystack > end);
        if (*haystack != '\0')
                buffer_puts(string_buf__(ret), haystack);
        return 0;
}

static int
string_copy(struct var_t *ret)
{
        char *s;
        struct var_t *self = get_this();
        bug_on(self->magic != TYPE_STRING);

        if (ret->magic == TYPE_EMPTY)
                string_init(ret, NULL);
        bug_on(ret->magic != TYPE_STRING);

        buffer_reset(string_buf__(ret));
        s = string_get_cstring(self);
        if (!s)
                return 0;
        string_init(ret, s);
        return 0;
}

/* rjust(amt)   integer arg */
static int
string_rjust(struct var_t *ret)
{
        struct var_t *self = get_this();
        struct var_t *arg = vm_get_arg(0);
        size_t len;
        long long just;
        struct buffer_t *buf = string_buf__(ret);

        arg_type_check(arg, TYPE_INT);

        just = arg->i;
        if (just < 0 || just >= JUST_MAX) {
                syntax_noexit("Range limit error");
                return -1;
        }

        len = STRING_LENGTH(self);
        if (len < just) {
                /*
                 * TODO: "need_len = just + (bytes_len - len) + 1"
                 * Need to replace buffer_t API, allocate this in
                 * one chunk, and memset... much faster than this
                 * in the case of "rjust(gazillion)"
                 */
                just -= len;
                string_init(ret, NULL);
                while (just--)
                        buffer_putc(buf, ' ');
                string_puts(ret, string_get_cstring(self));
        } else {
                string_init(ret, string_get_cstring(self));
        }
        return 0;
}

/* rjust(amt)    integer arg */
static int
string_ljust(struct var_t *ret)
{
        struct var_t *self = get_this();
        struct var_t *arg = vm_get_arg(0);
        size_t len;
        long long just;
        struct buffer_t *buf = string_buf__(ret);

        arg_type_check(arg, TYPE_INT);

        just = arg->i;
        if (just < 0 || just >= JUST_MAX) {
                syntax_noexit("Range limit error");
                return -1;
        }

        len = STRING_LENGTH(self);
        string_init(ret, string_get_cstring(self));
        if (len < just) {
                just -= len;
                while (just--)
                        buffer_putc(buf, ' ');
        }
        return 0;
}

static int
string_join(struct var_t *ret)
{
        struct var_t *self = get_this();
        struct var_t *arg = vm_get_arg(0);
        char *joinstr;
        struct var_t *elem;
        int idx;

        if ((joinstr = string_get_cstring(self)) == NULL)
                joinstr = "";

        arg_type_check(arg, TYPE_LIST);

        idx = 0;
        elem = array_child(arg, idx);
        if (!elem) {
                string_init(ret, "");
                return 0;
        }

        if (elem->magic != TYPE_STRING)
                syntax("string.join method may only join lists of strings");

        string_init(ret, string_get_cstring(elem));
        for (;;) {
                idx++;
                elem = array_child(arg, idx);
                if (!elem)
                        break;
                string_puts(ret, joinstr);
                string_puts(ret, string_get_cstring(elem));
        }
        return 0;
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
        TYPE_HANDLE_DECR_REF(str->s);
}

static struct var_t *
string_add(struct var_t *a, struct var_t *b)
{
        struct var_t *ret;
        char *rval;
        char *lval = string_get_cstring(a);
        if (b->magic == TYPE_STRPTR)
                rval = b->strptr;
        else {
                if (b->magic != TYPE_STRING)
                        syntax("Mismatched types for %s operation", "+");
                rval = string_get_cstring(b);
        }
        ret = var_new();
        string_init(ret, lval);
        buffer_puts(string_buf__(ret), rval);
        return ret;
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
        switch (b->magic) {
        case TYPE_STRING:
                if (a->s == b->s)
                        return 0;
                if (a->s->s_info.ascii_len != b->s->s_info.ascii_len)
                        return 1;
                return compare_strings(string_get_cstring(a),
                                       string_get_cstring(b));
        case TYPE_STRPTR:
                return compare_strings(string_get_cstring(a),
                                       b->strptr);
        default:
                return 1;
        }
}

static bool
string_cmpz(struct var_t *a)
{
        char *s = string_get_cstring(a);
        /* treat "" same as NULL in comparisons */
        return s ? s[0] == '\0' : true;
}

static void
string_mov(struct var_t *to, struct var_t *from)
{
        string_init(to, string_get_cstring(from));
}

static int
string_mov_strict(struct var_t *to, struct var_t *from)
{
        if (from->magic == TYPE_STRPTR) {
                string_assign_cstring(to, from->strptr);
        } else if (from->magic == TYPE_STRING) {
                string_assign_cstring(to, string_get_cstring(from));
        } else {
                return -1;
        }
        return 0;
}

static const struct operator_methods_t string_primitives = {
        .add            = string_add,
        .cmp            = string_cmp,
        .cmpz           = string_cmpz,
        .mov            = string_mov,
        .mov_strict     = string_mov_strict,
        .reset          = string_reset,
};


/* **********************************************************************
 *                           API functions
 * *********************************************************************/

/**
 * string_init - Convert an empty variable into a string type
 * @var: An empty variable to turn into a string
 * @cstr: Initial C-string to set @v to, or NULL to do that later
 *
 * Return: @var
 */
struct var_t *
string_init(struct var_t *var, const char *cstr)
{
        bug_on(var->magic != TYPE_EMPTY);
        var->magic = TYPE_STRING;
        var->s = new_string_handle();
        if (cstr)
                string_assign_cstring(var, cstr);
        else
                string_clear(var);
        return var;
}

/**
 * string_assign_cstring - Assign a new C string to a string type
 * @str:  String var_t type
 * @s:    C string to set it to
 *
 * Clobber old string in @str if one exists
 */
void
string_assign_cstring(struct var_t *str, const char *s)
{
        bug_on(str->magic != TYPE_STRING);

        string_clear(str);
        if (!s)
                s = "";
        string_puts(str, s);
}

/**
 * string_nth_child - Get nth char in string
 * @str:        String var to get char from
 * @idx:        Index into @str to look
 *
 * Return:
 * Char, as a TYPE_STRING struct var_t.  The calling code must be
 * responsible for the GC for this.
 */
struct var_t *
string_nth_child(struct var_t *str, int idx)
{
        struct var_t *new;
        char cbuf[5];
        char *src;

        bug_on(str->magic != TYPE_STRING);
        src = string_get_cstring(str);
        if (!src || src[0] == '\0')
                return NULL;

        idx = index_translate(idx, STRING_LENGTH(str));
        if (idx < 0)
                return NULL;

        if (str->s->s_info.enc == STRING_ENC_ASCII) {
                cbuf[0] = src[idx];
                cbuf[1] = '\0';
        } else {
                if (utf8_subscr_str(src, idx, cbuf) < 0) {
                        /* code managing str->s->s_info has bug */
                        bug();
                        return NULL;
                }
        }
        new = var_new();
        string_init(new, cbuf);
        return new;
}

/*
 * WARNING!! This is not reentrance safe!  Whatever you are doing
 * with the return value, do it now.
 *
 * FIXME: This is also not thread safe, and "do it quick" is not a good
 * enough solution.
 */
char *
string_get_cstring(struct var_t *str)
{
        bug_on(str->magic != TYPE_STRING);
        return str->s->b.s;
}

/**
 * string_init_from_file - Initialize a string with a line from file
 * @ret:        Empty variable to initialize
 * @fp:         File to read from
 * @delim:      Delimiter to read to.
 * @stuff_delim: True to include the delimiter with the string, false to leave
 *              it out (it will not be ungetc'd).
 */
void
string_init_from_file(struct var_t *ret, FILE *fp, int delim, bool stuff_delim)
{
        int c;
        struct buffer_t *buf;

        bug_on(ret->magic != TYPE_EMPTY);
        string_init(ret, NULL);
        buf = string_buf__(ret);
        string_clear(ret);
        while ((c = getc(fp)) != delim && c != EOF) {
                buffer_putc(buf, c);
        }
        if (stuff_delim && c == delim)
                buffer_putc(buf, c);
        utf8_scan(buf->s, &ret->s->s_info);
}

/**
 * string_length - Get the length of @str
 *
 * Return:
 * length, in number of CHARACTERS, not bytes.  @str might be UTF-8-
 * encoded.
 */
size_t
string_length(struct var_t *str)
{
        return STRING_LENGTH(str);
}

void
typedefinit_string(void)
{
        var_config_type(TYPE_STRING, "string",
                        &string_primitives, string_methods);
}

