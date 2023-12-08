/* string.c - Built-in methods for string data types */
#include "var.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>

/* user argument limits */
enum {
        JUST_MAX = 10000,
};

struct string_handle_t {
        struct buffer_t b;
        enum {
                STRING_ENC_UNK = 0,
                STRING_ENC_ASCII,
                STRING_ENC_UTF8,
        } enc;
};

static void
emismatch(const char *op)
{
        syntax("Mismatched types for %s operation", op);
}

static inline struct buffer_t *string_buf__(struct var_t *str)
        { return &str->s->b; }

static void
string_handle_reset(void *h)
{
        struct string_handle_t *sh = h;
        buffer_free(&sh->b);
}

static struct string_handle_t *
new_string_handle(void)
{
        struct string_handle_t *ret = type_handle_new(sizeof(*ret),
                                                string_handle_reset);
        buffer_init(&ret->b);
        return ret;
}

static inline void string_clear(struct var_t *str)
        { string_assign_cstring(str, ""); }

size_t
string_length(struct var_t *str)
{
        if (str->s->enc == STRING_ENC_ASCII)
                return buffer_size(string_buf__(str));
        /* TODO: Way to update enc */
        return utf8_strlen(string_get_cstring(str));
}

static void
string_putc(struct var_t *str, int c)
{
        bug_on(str->magic != TYPE_STRING);
        if ((unsigned)c > 127)
                str->s->enc = STRING_ENC_UNK;
        buffer_putc(string_buf__(str), c);
}

static void
string_puts(struct var_t *str, const char *s)
{
        if (!s)
                return;
        while (*s)
                string_putc(str, *s++);
}

/* format2 and helpers */

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
 * overhead of string_putc.
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
format2_i_helper(struct var_t *ret, unsigned long long ival, int base, int xchar)
{
        long long v;
        if (!ival)
                return;

        if (ival >= base)
                format2_i_helper(ret, ival / base, base, xchar);

        v = ival % base;
        if (v >= 10)
                v += xchar;
        else
                v += '0';

        string_putc(ret, (int)v);
}

static void
format2_i(struct var_t *ret, struct var_t *arg,
          int conv, bool rjust, int padc, size_t padlen, int precision)
{
        int base;
        int xchar = 'A' - 10;
        long long ival = arg->magic == TYPE_INT
                         ? arg->i : (long long)ret->f;
        struct buffer_t *buf = string_buf__(ret);
        size_t count;

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

        count = string_buf__(ret)->p;
        if (!ival) {
                string_putc(ret, '0');
        } else {
                unsigned long long uval;
                if (conv == 'd' && ival < 0) {
                        string_putc(ret, '-');
                        uval = -ival;
                } else {
                        uval = (unsigned long long)ival;
                }
                format2_i_helper(ret, uval, base, xchar);
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
format2_e_exp(struct var_t *ret, int exp)
{
        if (exp == 0)
                return;
        if (exp > 0)
                format2_e_exp(ret, exp / 10);
        string_putc(ret, (exp % 10) + '0');
}

/* FIXME: subtle difference from above, try to eliminate one of these */
static void
format2_f_ihelper(struct var_t *ret, unsigned int v)
{
        if (v >= 10)
                format2_f_ihelper(ret, v / 10);
        string_putc(ret, (v % 10) + '0');
}

static void
format2_e(struct var_t *ret, struct var_t *arg,
          int conv, bool rjust, int padc, size_t padlen, int precision)
{
        int exp = 0;
        int sigfig = 0;
        double ival;
        /* checked before this call */
        double v = arg->magic == TYPE_FLOAT ? arg->f : (double)arg->i;
        double dv = v;
        struct buffer_t *buf = string_buf__(ret);

        size_t count = buf->p;

        if (dv < 0.0) {
                string_putc(ret, '-');
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
        string_putc(ret, (int)ival + '0');
        ++sigfig;

        string_putc(ret, '.');
        while (sigfig < precision) {
                dv *= 10.0;
                dv = modf(dv, &ival);
                string_putc(ret, (int)ival + '0');
                sigfig++;
        }

        /* print exponent */
        bug_on(conv != 'e' && conv != 'E');
        string_putc(ret, conv);
        if (exp < 0) {
                string_putc(ret, '-');
                exp = -exp;
        } else {
                string_putc(ret, '+');
        }
        /* %e requires exponent to be at least two digits */
        if (exp < 10)
                string_putc(ret, '0');

        if (exp == 0)
                string_putc(ret, '0');
        else
                format2_e_exp(ret, exp);

        if (!rjust)
                padc = ' ';
        count = buf->p - count;
        if (count < padlen) {
                padlen -= count;
                padwrite(string_buf__(ret), padc, padlen);
                if (rjust) {
                        swap_pad(buf, count, padlen);
                }
        }
}

static void
format2_f(struct var_t *ret, struct var_t *arg,
          int conv, bool rjust, int padc, size_t padlen, int precision)
{
        double v = arg->magic == TYPE_FLOAT ? arg->f : (double)arg->i;
        bool have_dot = false;
        struct buffer_t *buf = string_buf__(ret);
        size_t count = buf->p;

        if (!isfinite(v)) {
                if (isnan(v)) {
                        string_puts(ret, "nan");
                } else {
                        if (v == -INFINITY)
                                string_putc(ret, '-');
                        string_puts(ret, "inf");
                }
        } else {
                double iptr, rem, scale;
                int i;

                if (v < 0.0) {
                        string_putc(ret, '-');
                        v = -v;
                }
                for (scale = 1.0, i = 0; i < precision; i++)
                        scale *= 0.1;
                v += scale * 0.5;
                rem = modf(v, &iptr);

                format2_f_ihelper(ret, (unsigned int)iptr);

                if (precision > 0) {
                        have_dot = true;
                        string_putc(ret, '.');
                        while (precision--) {
                                rem *= 10.0;
                                string_putc(ret, (int)rem + '0');
                                rem = modf(rem, &iptr);
                        }
                }
        }

        if (!rjust && !have_dot)
                padc = ' ';
        count = buf->p - count;
        if (count < padlen) {
                padlen -= count;
                padwrite(string_buf__(ret), padc, padlen);
                if (rjust) {
                        swap_pad(buf, count, padlen);
                }
        }
}

static void
format2_s(struct var_t *ret, struct var_t *arg,
          int conv, bool rjust, int padc, size_t padlen, int precision)
{
        const char *src;
        size_t count, count_bytes;
        struct buffer_t *buf = string_buf__(ret);

        if (arg->magic == TYPE_STRING)
                src = string_get_cstring(arg);
        else    /* TYPE_STRPTR */
                src = arg->strptr;

        if (!src) {
                count = count_bytes = 6;
                string_puts(ret, "(null)");
        } else {
                count_bytes = buf->p;
                count = utf8_strlen(src);
                string_puts(ret, src);
                count_bytes = buf->p - count_bytes;
        }

        /*
         * This is trickier than the numbers' case, because the string
         * length might get confused by Unicode characters.
         */
        if (count < padlen) {
                padlen -= count;
                padwrite(string_buf__(ret), padc, padlen);
                if (rjust)
                        swap_pad(buf, count_bytes, padlen);
        }
}

static size_t
format2_helper(struct var_t *ret, const char *s, int argi)
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

        switch ((conv = *s++)) {
        case 'x':
        case 'X':
        case 'd':
        case 'u':
                if (!isnumvar(v))
                        return 0;
                format2_i(ret, v, conv, rjust, padc, padlen, precision);
                break;
        case 'f':
                if (!isnumvar(v))
                        return 0;
                format2_f(ret, v, conv, rjust, padc, padlen, precision);
                break;
        case 'e':
        case 'E':
                if (!isnumvar(v))
                        return 0;
                format2_e(ret, v, conv, rjust, padc, padlen, precision);
                break;
        case 's':
                if (v->magic != TYPE_STRING && v->magic != TYPE_STRPTR)
                        return 0;
                format2_s(ret, v, conv, rjust, padc, padlen, precision);
                break;
        default:
        case '\0':
                /* don't try to format this */
                return 0;
        }

        return s - ssave;
}

static void
string_format2(struct var_t *ret)
{
        char cbuf[5];
        size_t size;
        struct var_t *self = get_this();
        const char *s;
        int argi = 0;

        bug_on(self->magic != TYPE_STRING);
        s = string_get_cstring(self);

        string_init(ret, 0);
        if (!s) {
                string_putc(ret, '\0');
                return;
        }

        while ((size = utf8_strgetc(s, cbuf)) != 0) {
                s += size;
                if (size > 1) {
                        string_puts(ret, cbuf);
                        continue;
                }
                if (cbuf[0] != '%') {
                        string_putc(ret, cbuf[0]);
                        continue;
                }
                size = utf8_strgetc(s, cbuf);
                if (size == 1 && cbuf[0] == '%') {
                        s++;
                        string_putc(ret, '%');
                        continue;
                }
                /* don't update @s, we'll check it again in helper */
                s += format2_helper(ret, s, argi++);
        }
}


/* len() (no args)
 * returns length of C string stored in self
 *
 * Gotta call it something different, "string_length" already taken
 */
static void
string_length_method(struct var_t *ret)
{
        struct var_t *self = get_this();
        bug_on(self->magic != TYPE_STRING);
        integer_init(ret, string_length(self));
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
static void
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
}

/* toint() (no args)
 * returns int
 */
static void
string_toint(struct var_t *ret)
{
        struct var_t *self = get_this();
        long long i = 0LL;
        char *s;
        bug_on(self->magic != TYPE_STRING);
        s = string_get_cstring(self);
        if (s) {
                int errno_save = errno;
                char *endptr;
                i = strtoll(s, &endptr, 0);
                if (endptr == s || errno)
                        i = 0;
                errno = errno_save;
        }
        integer_init(ret, i);
}

/*
 * tofloat()  (no args)
 * returns float
 */
static void
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

        qop_mov(ret, self);
        return arg ? string_get_cstring(arg) : NULL;
}

/*
 * lstrip()             no args implies whitespace
 * lstrip(charset)      charset is string
 */
static void
string_lstrip(struct var_t *ret)
{
        const char *charset = strip_common(ret);
        buffer_lstrip(string_buf__(ret), charset);
}

/*
 * rstrip()             no args implies whitespace
 * rstrip(charset)      charset is string
 */
static void
string_rstrip(struct var_t *ret)
{
        const char *charset = strip_common(ret);
        buffer_rstrip(string_buf__(ret), charset);
}

/*
 *  strip()             no args implies whitespace
 *  strip(charset)      charset is string
 */
static void
string_strip(struct var_t *ret)
{
        const char *charset = strip_common(ret);
        buffer_rstrip(string_buf__(ret), charset);
        buffer_lstrip(string_buf__(ret), charset);
}

static void
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
        end = haystack + string_length(self);
        needle = string_get_cstring(vneedle);
        needle_len = string_length(vneedle);

        if (!haystack || end == haystack) {
                buffer_putc(string_buf__(ret), '\0');
                return;
        }

        if (!needle || !needle_len) {
                buffer_puts(string_buf__(ret), string_get_cstring(self));
                return;
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
}

static void
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
                return;
        string_init(ret, s);
}

/* rjust(amt)   integer arg */
static void
string_rjust(struct var_t *ret)
{
        struct var_t *self = get_this();
        struct var_t *arg = vm_get_arg(0);
        size_t len;
        long long just;

        arg_type_check(arg, TYPE_INT);

        just = arg->i;
        if (just < 0 || just >= JUST_MAX)
                syntax("Range limit error");

        len = string_length(self);
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
                        string_putc(ret, ' ');
                string_puts(ret, string_get_cstring(self));
        } else {
                string_init(ret, string_get_cstring(self));
        }
}

/* rjust(amt)    integer arg */
static void
string_ljust(struct var_t *ret)
{
        struct var_t *self = get_this();
        struct var_t *arg = vm_get_arg(0);
        size_t len;
        long long just;

        arg_type_check(arg, TYPE_INT);

        just = arg->i;
        if (just < 0 || just >= JUST_MAX)
                syntax("Range limit error");

        len = string_length(self);
        string_init(ret, string_get_cstring(self));
        if (len < just) {
                just -= len;
                while (just--)
                        string_putc(ret, ' ');
        }
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
        TBLEND,
};

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
                        emismatch("+");
                rval = string_get_cstring(b);
        }
        ret = var_new();
        string_init(ret, lval);
        buffer_puts(string_buf__(ret), rval);
        return ret;
}

static int
string_cmp(struct var_t *a, struct var_t *b)
{
        int r;
        if (!string_get_cstring(a))
                return string_get_cstring(b) ? -1 : 1;
        else if (!string_get_cstring(b))
                return 1;
        r = strcmp(string_get_cstring(a), string_get_cstring(b));
        return r ? (r < 0 ? -1 : 1) : 0;
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
        to->s = from->s;
        TYPE_HANDLE_INCR_REF(to->s);
        to->magic = TYPE_STRING;
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

        struct buffer_t *buf = string_buf__(str);
        buffer_reset(buf);
        str->s->enc = STRING_ENC_ASCII;
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
        struct buffer_t *buf;
        struct var_t *new;
        char cbuf[5];
        char *src;

        bug_on(str->magic != TYPE_STRING);
        src = string_get_cstring(str);
        if (!src || src[0] == '\0')
                return NULL;

        if (str->s->enc == STRING_ENC_ASCII) {
                buf = string_buf__(str);
                idx = index_translate(idx, buffer_size(buf));
                if (idx < 0)
                        return NULL;

                cbuf[0] = src[idx];
                cbuf[1] = '\0';
        } else {
                /* XXX REVISIT: double-dipping, maybe make
                 * utf8_...() return more data about the string.
                 */
                idx = index_translate(idx, utf8_strlen(src));
                if (idx < 0)
                        return NULL;
                if (utf8_subscr_str(src, idx, cbuf) < 0)
                        return NULL;
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

        bug_on(ret->magic != TYPE_EMPTY);
        string_init(ret, NULL);

        string_clear(ret);
        while ((c = getc(fp)) != delim && c != EOF)
                string_putc(ret, c);
        if (stuff_delim && c == delim)
                string_putc(ret, c);
}


void
typedefinit_string(void)
{
        var_config_type(TYPE_STRING, "string",
                        &string_primitives, string_methods);
}

