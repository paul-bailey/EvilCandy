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

/* flags arg to stringvar_newf, see comments there */
enum { SF_COPY = 1, };

#define V2STR(v)                ((struct stringvar_t *)(v))
#define V2CSTR(v)               string_cstring(v)
#define STRING_LENGTH(str)      seqvar_size(str)
#define STRING_NBYTES(str)      (V2STR(str)->s_info.ascii_len)


/* **********************************************************************
 *                      Common Helpers
 ***********************************************************************/

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
        utf8_scan(cstr, &vs->s_info);
        /*
         * We only hash the first time it's needed.  If we never need
         * it, we never hash.
         */
        vs->s_hash = 0;
        seqvar_set_size(ret, vs->s_info.enc_len);
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
static char *
string_parse(const char *src)
{
        int c, q;
        struct buffer_t b;
        const char *s = src;
        enum { BKSL = '\\', SQ = '\'', DQ = '"' };

        buffer_init(&b);

        q = *s++;
        bug_on(q != SQ && q != DQ);

again:
        while ((c = *s++) != q) {
                /* should have been trapped already */
                bug_on(c == 0);
                if (c == BKSL) {
                        c = *s++;
                        if (c == q) {
                                buffer_putc(&b, c);
                                continue;
                        } else if (c == 'n') {
                                /*
                                 * could be in switch,
                                 * but it's our 99% scenario.
                                 */
                                buffer_putc(&b, '\n');
                                continue;
                        }

                        switch (c) {
                        case 'a': /* bell - but why? */
                                buffer_putc(&b, '\a');
                                continue;
                        case 'b':
                                buffer_putc(&b, '\b');
                                continue;
                        case 'e':
                                buffer_putc(&b, '\033');
                                continue;
                        case 'f':
                                buffer_putc(&b, '\f');
                                continue;
                        case 'v':
                                buffer_putc(&b, '\v');
                                continue;
                        case 'r':
                                buffer_putc(&b, '\r');
                                continue;
                        case 't':
                                buffer_putc(&b, '\t');
                                continue;
                        case BKSL:
                                buffer_putc(&b, BKSL);
                                continue;
                        default:
                                break;
                        }

                        if (isodigit(c)) {
                                int i, v;
                                --s;
                                for (i = 0, v = 0; i < 3; i++, s++) {
                                        if (!isodigit(*s))
                                                break;
                                        /* '0' & 7 happens to be 0 */
                                        v = (v << 3) + (*s & 7);
                                }
                                if (v == 0 || v >= 256)
                                        goto err;
                                buffer_putc(&b, v);
                                continue;
                        }

                        if (c == 'x' || c == 'X') {
                                int v;
                                if (!isxdigit(s[0]) || !isxdigit(s[1]))
                                        goto err;
                                v = x2bin(s[0]) * 16 + x2bin(s[1]);
                                if (v == 0)
                                        goto err;

                                s += 2;
                                buffer_putc(&b, v);
                                continue;
                        }

                        if (c == 'u' || c == 'U') {
                                char ubuf[5];
                                uint32_t point = 0;
                                int i, amt = c == 'u' ? 4 : 8;
                                size_t ubuf_size;
                                for (i = 0; i < amt; i++) {
                                        if (!isxdigit((int)(s[i])))
                                                goto err;
                                        point <<= 4;
                                        point |= x2bin(s[i]);
                                }

                                if (point == 0)
                                        goto err;
                                /* out-of-range for Unicode */
                                if (point > 0x10ffff)
                                        goto err;

                                ubuf_size = utf8_encode(point, ubuf);
                                bug_on(ubuf_size == 0);
                                (void)ubuf_size;

                                s += amt;
                                buffer_puts(&b, ubuf);
                                continue;
                        }

                        /* wrapping code would have caught this */
                        bug_on(c == '\0');

                        /* unsupported escape */
                        goto err;
                } else {
                        buffer_putc(&b, c);
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

        if (buffer_size(&b) == 0) {
                /* empty string, should we allow it? */
                buffer_free(&b);
                char *ret = emalloc(1);
                ret[0] = '\0';
                return ret;
        }

        return buffer_trim(&b);

err:
        buffer_free(&b);
        return NULL;
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

        return stringvar_newf(buffer_trim(&t), 0);
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

static Object *
string_split(Frame *fr)
{
        Object *self = get_this(fr);
        Object *ret;
        bug_on(!isvar_string(self));
        const char *str;

        ret = arrayvar_new(0);
        str = V2CSTR(self);

        if (!str || seqvar_size(self) == 0)
                return ret;

        for (;;) {
                const char *start, *end;
                char *newbuf;
                size_t size;
                Object *item;

                start = str;
                while (*start != '\0' && isspace((int)(*start)))
                        ++start;
                if (*start == '\0')
                        break;
                end = start;
                while (*end != '\0' && !isspace((int)(*end)))
                        ++end;
                size = (end - start);
                newbuf = emalloc(size + 1);
                memcpy(newbuf, start, size);
                newbuf[size] = '\0';
                item = stringvar_nocopy(newbuf);
                array_append(ret, item);
                VAR_DECR_REF(item);

                str = end;
        }
        return ret;
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
        Object *ret = array_getitem(arr, i);
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
        Object *self = vm_get_this(fr);
        Object *arg = vm_get_arg(fr, 0);
        const char *needle, *haystack;
        int count, ndlen;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(arg, &StringType) == RES_ERROR)
                return ErrorVar;

        needle = string_cstring(arg);
        haystack = string_cstring(self);
        /*
         * strstr has too complicated a return value,
         * just do this manually.
         */
        count = 0;
        ndlen = STRING_NBYTES(arg);
        while (*haystack != '\0') {
                if (!strncmp(haystack, needle, ndlen)) {
                        count++;
                        haystack += ndlen;
                } else {
                        haystack++;
                }
        }
        return intvar_new(count);
}

static Object *
string_endswith(Frame *fr)
{
        Object *self = vm_get_this(fr);
        Object *arg = vm_get_arg(fr, 0);
        /* TODO: optional start, stop args */
        const char *needle, *haystack;
        int endswith, nlen, hlen;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(arg, &StringType) == RES_ERROR)
                return ErrorVar;

        needle = string_cstring(arg);
        haystack = string_cstring(self);
        nlen = STRING_NBYTES(arg);
        hlen = STRING_NBYTES(self);

        if (nlen > hlen) {
                endswith = 0;
        } else {
                endswith = strcmp(&haystack[hlen - nlen], needle) == 0;
        }
        return intvar_new(endswith);
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
        while ((c = *src++) != '\0') {
                if (c == '\t') {
                        if (col == nextstop)
                                nextstop += tabsize;
                        while (col < nextstop) {
                                buffer_putc(&b, ' ');
                                col++;
                        }
                        nextstop += tabsize;
                        continue;
                } else {
                        if (col == nextstop)
                                nextstop += tabsize;
                        buffer_putc(&b, c);
                        col++;
                }
        }
        return stringvar_newf(buffer_trim(&b), 0);
}

/* helper to str.find() and str.index() */
static Object *
string_index_or_find(Frame *fr, int suppress)
{
        Object *self = vm_get_this(fr);
        Object *arg = vm_get_arg(fr, 0);
        const char *haystack, *needle, *found;

        if (arg_type_check(self, &StringType) == RES_ERROR)
                return ErrorVar;
        if (arg_type_check(arg, &StringType) == RES_ERROR)
                return ErrorVar;

        needle = string_cstring(arg);
        haystack = string_cstring(self);
        found = strstr(haystack, needle);
        if (!found && !suppress) {
                err_setstr(ValueError, "substring not found");
                return ErrorVar;
        }
        return intvar_new(found ? (int)(found - haystack) : -1);
}

static Object *
string_find(Frame *fr)
{
        return string_index_or_find(fr, 1);
}

static Object *
string_index(Frame *fr)
{
        return string_index_or_find(fr, 0);
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

static bool
is_ident(const char *s)
{
        int c;
        if ((c = *s++) != '_' && !isalpha(c))
                return false;
        while ((c = *s++) != '\0') {
                if (!isalnum(c) && c != '_')
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
                if (!isalpha(c)) {
                        first = true;
                } else if (first) {
                        if (islower(c))
                                return false;
                        first = false;
                }
        }
        return true;
}

static bool is_alnum(unsigned int c) { return c < 128 && isalnum(c); }
static bool is_alpha(unsigned int c) { return c < 128 && isalpha(c); }
static bool is_ascii(unsigned int c) { return c < 128; }
static bool is_digit(unsigned int c) { return c < 128 && isdigit(c); }
static bool is_printable(unsigned int c) { return c < 128 && isprint(c); }
static bool is_space(unsigned int c) { return c < 128 && isspace(c); }
static bool is_upper(unsigned int c) { return c < 128 && isupper(c); }

static Object *string_isident(Frame *fr)
        { return string_is1(fr, is_ident); }

static Object *string_istitle(Frame *fr)
        { return string_is1(fr, is_title); }

static Object *string_isalnum(Frame *fr)
        { return string_is2(fr, is_alnum); }

static Object *string_isalpha(Frame *fr)
        { return string_is2(fr, is_alpha); }

static Object *string_isascii(Frame *fr)
        { return string_is2(fr, is_ascii); }

static Object *string_isdigit(Frame *fr)
        { return string_is2(fr, is_digit); }

static Object *string_isprintable(Frame *fr)
        { return string_is2(fr, is_printable); }

static Object *string_isspace(Frame *fr)
        { return string_is2(fr, is_space); }

static Object *string_isupper(Frame *fr)
        { return string_is2(fr, is_upper); }

static struct type_inittbl_t string_methods[] = {
        V_INITTBL("capitalize",  string_capitalize,  0, 0, -1, -1),
        V_INITTBL("center",      string_center,      1, 1, -1, -1),
        V_INITTBL("copy",        string_copy,        0, 0, -1, -1),
        V_INITTBL("count",       string_count,       1, 1, -1, -1),
        V_INITTBL("endswith",    string_endswith,    0, 0, -1, -1),
        V_INITTBL("expandtabs",  string_expandtabs,  1, 1, -1,  0),
        V_INITTBL("find",        string_find,        1, 1, -1, -1),
        V_INITTBL("format",      string_format,      1, 1,  0, -1),
        V_INITTBL("format2",     string_format2,     1, 1,  0, -1),
        V_INITTBL("index",       string_index,       1, 1, -1, -1),
        V_INITTBL("isalnum",     string_isalnum,     0, 0, -1, -1),
        V_INITTBL("isalpha",     string_isalpha,     0, 0, -1, -1),
        V_INITTBL("isascii",     string_isascii,     0, 0, -1, -1),
        V_INITTBL("isdigit",     string_isdigit,     0, 0, -1, -1),
        V_INITTBL("isident",     string_isident,     0, 0, -1, -1),
        V_INITTBL("isprintable", string_isprintable, 0, 0, -1, -1),
        V_INITTBL("isspace",     string_isspace,     0, 0, -1, -1),
        V_INITTBL("istitle",     string_istitle,     0, 0, -1, -1),
        V_INITTBL("isupper",     string_isupper,     0, 0, -1, -1),
        V_INITTBL("join",        string_join,        1, 1, -1, -1),
        V_INITTBL("ljust",       string_ljust,       1, 1, -1, -1),
        V_INITTBL("lstrip",      string_lstrip,      0, 1, -1, -1),
        V_INITTBL("replace",     string_replace,     2, 2, -1, -1),
        V_INITTBL("rjust",       string_rjust,       1, 1, -1, -1),
        V_INITTBL("rstrip",      string_rstrip,      0, 1, -1, -1),
        V_INITTBL("split",       string_split,       0, 0, -1, -1),
        V_INITTBL("strip",       string_strip,       0, 1, -1, -1),
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
                } else if (isspace(c)) {
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
                } else if (!isgraph(c)) {
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

        if (V2STR(str)->s_info.enc != STRING_ENC_UTF8) {
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
        char *s = string_parse(tokenstr);
        if (!s)
                return ErrorVar;
        return stringvar_newf(s, 0);
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


