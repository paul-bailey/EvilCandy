/* string.c - Built-in methods for string data types */
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
        SF_SUPPRESS     = 0x0020,       /* suppress errors */
};

#define V2STR(v)                ((struct stringvar_t *)(v))
#define V2CSTR(v)               string_cstring(v)
#define STRING_LENGTH(str)      seqvar_size(str)
#define STRING_NBYTES(str)      string_nbytes(str)


/* **********************************************************************
 *                      Common Helpers
 ***********************************************************************/

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
stringvar_from_points(void *points, size_t width, size_t len)
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
                return stringvar_newf("", SF_COPY);
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

                if (point < 0x7ff) {
                        buffer_putc(&b, 0xc0 | (point >> 6));
                        buffer_putc(&b, 0x80 | (point & 0x3f));
                } else if (point < 0xffff) {
                        buffer_putc(&b, 0xe0 | (point >> 12));
                        buffer_putc(&b, 0x80 | ((point >> 6) & 0x3f));
                        buffer_putc(&b, 0x80 | (point & 0x3f));
                } else {
                        buffer_putc(&b, 0xf0 | (point >> 18));
                        buffer_putc(&b, 0x80 | ((point >> 12) & 0x3f));
                        buffer_putc(&b, 0x80 | ((point >> 6) & 0x3f));
                        buffer_putc(&b, 0x80 | (point & 0x3f));
                }
        }
        ret = var_new(&StringType);
        vs = V2STR(ret);

        vs->s_unicode   = points;
        vs->s_enc_len   = len;
        vs->s_width     = width;
        vs->s           = buffer_trim(&b);
        vs->s_ascii_len = strlen(vs->s);
        vs->s_hash      = 0;
        vs->s_ascii     = ascii;
        seqvar_set_size(ret, len);
        return ret;
}

static inline Object *
string_copy__(Object *str)
{
        VAR_INCR_REF(str);
        return str;
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
                        point = utf8_decode_one(s - 1, &endptr);
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
format2_i(struct buffer_t *buf, Object *arg,
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
format2_e(struct buffer_t *buf, Object *arg,
          int conv, bool rjust, int padc, size_t padlen, int precision)
{
        int exp = 0;
        int sigfig = 0;
        double ival;
        /* checked before this call */
        double v = realvar_tod(arg);
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
format2_f(struct buffer_t *buf, Object *arg,
          int conv, bool rjust, int padc, size_t padlen, int precision)
{
        double v = realvar_tod(arg);
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
format2_s(struct buffer_t *buf, Object *arg,
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
format2_helper(Object **args, struct buffer_t *buf, const char *s, int argi, int argc)
{
        const char *ssave = s;
        bool rjust = true;
        int padc = ' ';
        size_t padlen = 0;
        int precision = 6;
        Object *v;
        int conv;

        /*
         * XXX should warn, but if this is in a user loop or function,
         * I don't want to flood the output.
         */
        if (argi >= argc)
                return 0;

        v = args[argi];
        bug_on(!v);

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
                if (!isvar_real(v))
                        return 0;
                format2_i(buf, v, conv, rjust, padc, padlen, precision);
                break;
        case 'f':
                if (!isvar_real(v))
                        return 0;
                format2_f(buf, v, conv, rjust, padc, padlen, precision);
                break;
        case 'e':
        case 'E':
                if (!isvar_real(v))
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
static Object *
string_format2(Frame *fr)
{
        char cbuf[5];
        size_t size, argc;
        Object *ret, *list, *self, **args;
        const char *s;
        struct buffer_t b;
        size_t argi = 0;

        self = vm_get_this(fr);
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        list = vm_get_arg(fr, 0);
        bug_on(!list);
        bug_on(!isvar_array(list));
        /*
         * This is so format2 can return from whereever without having
         * to fuss over reference counters.
         */
        args = array_get_data(list);
        argc = seqvar_size(list);

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
                                s += format2_helper(args, &b, s, argi++, argc);
                        }
                } else  {
                        buffer_putc(&b, cbuf[0]);
                }
        }

        ret = stringvar_newf(buffer_trim(&b), 0);
        return ret;
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

/*
 * lstrip()             no args implies whitespace
 * lstrip(charset)      charset is string
 */
static Object *
string_lstrip(Frame *fr)
{
        const char *charset;
        Object *arg = frame_get_arg(fr, 0);
        Object *self = get_this(fr);
        struct buffer_t b;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        /* arg may be NULL, else it must be string */
        if (arg && arg_type_check(arg, &StringType) == RES_ERROR)
                return ErrorVar;

        buffer_init(&b);
        buffer_puts(&b, V2CSTR(self));
        charset = arg ? V2CSTR(arg) : NULL;
        buffer_lstrip(&b, charset);

        return stringvar_newf(buffer_trim(&b), 0);
}

/*
 * rstrip()             no args implies whitespace
 * rstrip(charset)      charset is string
 */
static Object *
string_rstrip(Frame *fr)
{
        const char *charset;
        Object *arg = frame_get_arg(fr, 0);
        Object *self = get_this(fr);
        struct buffer_t b;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        /* arg may be NULL, else it must be string */
        if (arg && arg_type_check(arg, &StringType) == RES_ERROR)
                return ErrorVar;

        buffer_init(&b);
        buffer_puts(&b, V2CSTR(self));
        charset = arg ? V2CSTR(arg) : NULL;
        buffer_rstrip(&b, charset);
        return stringvar_newf(buffer_trim(&b), 0);
}

/*
 *  strip()             no args implies whitespace
 *  strip(charset)      charset is string
 */
static Object *
string_strip(Frame *fr)
{
        const char *charset;
        Object *arg = frame_get_arg(fr, 0);
        Object *self = get_this(fr);
        struct buffer_t b;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        /* arg may be NULL, else it must be string */
        if (arg && arg_type_check(arg, &StringType) == RES_ERROR)
                return ErrorVar;

        buffer_init(&b);
        buffer_puts(&b, V2CSTR(self));
        charset = arg ? V2CSTR(arg) : NULL;
        buffer_rstrip(&b, charset);
        buffer_lstrip(&b, charset);
        return stringvar_newf(buffer_trim(&b), 0);
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

/* XXX Superfluous, the way we do things now, remove? */
static Object *
string_copy(Frame *fr)
{
        Object *self = get_this(fr);

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        return string_copy__(self);
}

/* rjust(amt)   integer arg */
static Object *
string_rjust(Frame *fr)
{
        Object *self = get_this(fr);
        Object *arg = vm_get_arg(fr, 0);
        size_t len;
        long long just;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(arg, &IntType) == RES_ERROR)
                return ErrorVar;

        just = intvar_toll(arg);
        if (just < 0 || just >= JUST_MAX) {
                err_setstr(ValueError, "Range limit error");
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
                return stringvar_newf(buffer_trim(&b), 0);
        } else {
                return string_copy__(self);
        }
}

/* rjust(amt)    integer arg */
static Object *
string_ljust(Frame *fr)
{
        Object *self = get_this(fr);
        Object *arg = vm_get_arg(fr, 0);
        size_t len;
        long long just;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(arg, &IntType) == RES_ERROR)
                return ErrorVar;

        just = intvar_toll(arg);
        if (just < 0 || just >= JUST_MAX) {
                err_setstr(ValueError, "Range limit error");
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
                return stringvar_newf(buffer_trim(&b), 0);
        } else {
                return string_copy__(self);
        }
}

/* helper to string_join below */
static Object *
join_next_str(Object *arr, int i)
{
        Object *ret = seqvar_getitem(arr, i);
        /* see string_join below, we already checked that i is ok */
        bug_on(!ret);
        if (!isvar_string(ret)) {
                err_setstr(TypeError,
                           "string.join method may only join lists of strings");
                VAR_DECR_REF(ret);
                return NULL;
        }
        return ret;
}

static Object *
string_join(Frame *fr)
{
        struct buffer_t b;
        Object *self = get_this(fr);
        Object *arg = vm_get_arg(fr, 0);
        const char *joinstr;
        Object *elem;
        int i;
        size_t n;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        if ((joinstr = V2CSTR(self)) == NULL)
                joinstr = "";

        if (arg_type_check(arg, &ArrayType) == RES_ERROR)
                return ErrorVar;

        if ((n = seqvar_size(arg)) == 0)
                return stringvar_newf("", 0);

        elem = join_next_str(arg, 0);
        if (!elem)
                return ErrorVar;

        buffer_init(&b);
        buffer_puts(&b, V2CSTR(elem));
        VAR_DECR_REF(elem);
        for (i = 1; i < n; i++) {
                elem = join_next_str(arg, i);
                if (!elem) {
                        buffer_free(&b);
                        return ErrorVar;
                }
                if (joinstr[0] != '\0')
                        buffer_puts(&b, joinstr);
                buffer_puts(&b, V2CSTR(elem));
                VAR_DECR_REF(elem);
        }
        return stringvar_newf(buffer_trim(&b), 0);
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
        Object *self = vm_get_this(fr);
        Object *arg = vm_get_arg(fr, 0);
        char *dst, *end, *newbuf;
        int len, src_len, nbytes, src_nbytes, padlen;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        if (arg_type_check(arg, &IntType) == RES_ERROR)
                return ErrorVar;

        len = intvar_toi(arg);
        if (err_occurred())
                return ErrorVar;

        src_len = seqvar_size(self);
        if (len < src_len)
                len = src_len;
        src_nbytes = STRING_NBYTES(self);
        nbytes = src_nbytes + (len - src_len);

        dst = newbuf = emalloc(nbytes + 1);
        end = dst + nbytes;

        padlen = (len - src_len) / 2;
        while (padlen-- > 0)
                *dst++ = ' ';
        memcpy(dst, string_cstring(self), src_nbytes);
        dst += src_nbytes;
        while (dst < end)
                *dst++ = ' ';
        *dst = '\0';
        return stringvar_newf(newbuf, SF_COPY);
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
        int tabsize, col, c, nextstop;
        const char *src;
        struct buffer_t b;

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

        buffer_init(&b);
        col = 0;
        nextstop = tabsize;
        src = string_cstring(self);
        /*
         * FIXME: col all wrong if there are utf-8 characters in here.
         */
        while ((c = *src++) != '\0') {
                if (c == '\n') {
                        col = 0;
                        nextstop = tabsize;
                        buffer_putc(&b, c);
                } else if (c == '\t') {
                        if (col == nextstop)
                                nextstop += tabsize;
                        while (col < nextstop) {
                                buffer_putc(&b, ' ');
                                col++;
                        }
                        nextstop += tabsize;
                } else {
                        if (col == nextstop)
                                nextstop += tabsize;
                        buffer_putc(&b, c);
                        col++;
                }
        }
        return stringvar_newf(buffer_trim(&b), 0);
}

static Object *
string_index_or_find(Frame *fr, unsigned int flags)
{
        Object *self = vm_get_this(fr);
        Object *arg = vm_get_arg(fr, 0);
        const char *haystack, *needle, *found;
        int res;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(arg, &StringType) == RES_ERROR)
                return ErrorVar;

        needle = string_cstring(arg);
        haystack = string_cstring(self);
        if (!!(flags & SF_RIGHT))
                found = strrstr(haystack, needle);
        else
                found = strstr(haystack, needle);
        if (!found && !(flags & SF_SUPPRESS)) {
                err_setstr(ValueError, "substring not found");
                return ErrorVar;
        }
        if (!found)
                return VAR_NEW_REF(gbl.neg_one);
        res = (int)(found - haystack);
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
        const char *haystack, *needle, *found;

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

        haystack = string_cstring(self);
        needle = string_cstring(arg);

        if (!!(flags & SF_RIGHT))
                found = strrstr(haystack, needle);
        else
                found = strstr(haystack, needle);

        tup = tuplevar_new(3);
        td = tuple_get_data(tup);
        VAR_DECR_REF(td[0]);
        VAR_DECR_REF(td[1]);
        VAR_DECR_REF(td[2]);
        if (!found) {
                td[0] = VAR_NEW_REF(self);
                td[1] = VAR_NEW_REF(STRCONST_ID(mpty));
                td[2] = VAR_NEW_REF(STRCONST_ID(mpty));
        } else {
                int idx = (int)(found - haystack);
                if (idx == 0) {
                        td[0] = VAR_NEW_REF(STRCONST_ID(mpty));
                } else {
                        td[0] = stringvar_newn(haystack, idx);
                }

                td[1] = VAR_NEW_REF(arg);

                idx += STRING_NBYTES(arg);
                if (idx == STRING_NBYTES(self)) {
                        td[2] = VAR_NEW_REF(STRCONST_ID(mpty));
                } else {
                        td[2] = stringvar_newf((char *)&haystack[idx],
                                               SF_COPY);
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
string_is1(Frame *fr, bool (*cb)(const char *))
{
        Object *ret;
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;

        if (seqvar_size(self) == 0) {
                ret = gbl.zero;
        } else {
                ret = cb(string_cstring(self)) ? gbl.one : gbl.zero;
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
                unsigned int c;
                const char *s = string_cstring(self);
                while ((c = *s++) != '\0') {
                        if (!tst(c)) {
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
static bool is_ascii(unsigned int c) { return c < 128; }
static bool is_digit(unsigned int c) { return c < 128 && isdigit(c); }
static bool is_printable(unsigned int c) { return c < 128 && isprint(c); }
static bool is_space(unsigned int c) { return c < 128 && isspace(c); }
static bool is_upper(unsigned int c) { return c < 128 && isupper(c); }
static bool is_lower(unsigned int c) { return c < 128 && islower(c); }

static bool
is_ident(const char *s)
{
        int c;
        if ((c = *s++) != '_' && !is_alpha(c))
                return false;
        while ((c = *s++) != '\0') {
                if (!is_alnum(c) && c != '_')
                        return false;
        }
        return true;
}

static bool
is_title(const char *s)
{
        int c;
        bool first = true;
        while ((c = *s++) != '\0') {
                if (!is_alpha(c)) {
                        first = true;
                } else if (first) {
                        if (is_lower(c))
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
static Object *string_isascii_mthd(Frame *fr)
        { return string_is2(fr, is_ascii); }

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
        V_INITTBL("copy",         string_copy,         0, 0, -1, -1),
        V_INITTBL("count",        string_count,        1, 1, -1, -1),
        V_INITTBL("endswith",     string_endswith,     1, 1, -1, -1),
        V_INITTBL("expandtabs",   string_expandtabs,   1, 1, -1,  0),
        V_INITTBL("find",         string_find,         1, 1, -1, -1),
        V_INITTBL("format",       string_format,       1, 1,  0, -1),
        V_INITTBL("format2",      string_format2,      1, 1,  0, -1),
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
        const char *s;
        int c;
        enum { Q = '\'', BKSL = '\\' };

        bug_on(!isvar_string(v));

        s = V2CSTR(v);
        buffer_init(&b);

        buffer_putc(&b, Q);
        while ((c = *s++) != '\0') {
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
                } else if (c > 128 || !isgraph(c)) {
                        buffer_putc(&b, BKSL);
                        buffer_putc(&b, ((c >> 6) & 0x03) + '0');
                        buffer_putc(&b, ((c >> 3) & 0x07) + '0');
                        buffer_putc(&b, (c & 0x07) + '0');
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
        return strcmp(a, b);
}

static int
string_cmp(Object *a, Object *b)
{
        bug_on(!isvar_string(a) || !isvar_string(b));
        return compare_strings(V2CSTR(a), V2CSTR(b));
}

static bool
string_cmpz(Object *a)
{
        const char *s = V2CSTR(a);
        /* treat "" same as NULL in comparisons */
        return s ? s[0] == '\0' : true;
}

/* comparisons, helpers to string_getslice */
static bool slice_cmp_lt(int a, int b) { return a < b; }
static bool slice_cmp_gt(int a, int b) { return a > b; }

static Object *
string_getslice(Object *str, int start, int stop, int step)
{
        const char *src;
        struct buffer_t b;
        bool (*cmp)(int, int);

        if (start == stop)
                return stringvar_new("");

        buffer_init(&b);
        src = V2CSTR(str);
        cmp = (start < stop) ? slice_cmp_lt : slice_cmp_gt;

        if (string_isascii(str)) {
                /* thank god */
                while (cmp(start, stop)) {
                        buffer_putc(&b, src[start]);
                        start += step;
                }
        } else {
                char cbuf[5];
                while (cmp(start, stop)) {
                        if (utf8_subscr_str(src, start, cbuf) < 0) {
                                bug();
                        }
                        buffer_puts(&b, cbuf);
                        start += step;
                }
        }
        return stringvar_from_buffer(&b);
}

/* .getitem sequence method for string  */
static Object *
string_getitem(Object *str, int idx)
{
        char cbuf[5];
        const char *src;

        bug_on(!isvar_string(str));
        src = V2CSTR(str);
        if (!src || src[0] == '\0')
                return NULL;

        bug_on(idx >= STRING_LENGTH(str));

        if (string_isascii(str)) {
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

static bool
string_hasitem(Object *str, Object *substr)
{
        const char *haystack, *needle;
        bug_on(!isvar_string(str));
        /* XXX policy, throw error instead? */
        if (!isvar_string(substr))
                return false;

        haystack = V2CSTR(str);
        needle = V2CSTR(substr);
        return strstr(haystack, needle) != NULL;
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
        return stringvar_from_points(buf, width, len);
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

struct type_t StringType = {
        .name   = "string",
        .opm    = NULL,
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


