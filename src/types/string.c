/* string.c - Built-in methods for string data types */
#include "var.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

static void
emismatch(const char *op)
{
        syntax("Mismatched types for %s operation", op);
}

/* len() (no args)
 * returns length of C string stored in self
 */
static void
string_length(struct var_t *ret)
{
        struct var_t *self = get_this();
        int len = 0;
        bug_on(self->magic != QSTRING_MAGIC);
        if (self->s.s)
                len = strlen(self->s.s);
        qop_assign_int(ret, len);
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
                q = getarg(la++);
        } else if (isdigit(*s)) {
                char *endptr;
                int i = strtoul(s, &endptr, 10);
                if (*endptr == '}') {
                        q = getarg(i);
                        la = i + 1;
                        s = endptr;
                }
        }
        if (!q)
                return false;

        switch (q->magic) {
        case QINT_MAGIC:
                sprintf(vbuf, "%lld", q->i);
                buffer_puts(t, vbuf);
                break;
        case QFLOAT_MAGIC:
                sprintf(vbuf, "%g", q->f);
                buffer_puts(t, vbuf);
                break;
        case QEMPTY_MAGIC:
                buffer_puts(t, "(null)");
                break;
        case QSTRING_MAGIC:
                buffer_puts(t, q->s.s);
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
        char *s;
        bug_on(self->magic != QSTRING_MAGIC);

        buffer_reset(&t);
        if (!self->s.s) {
                if (!t.s)
                        buffer_putc(&t, 'a');
                buffer_reset(&t);
                goto done;
        }

        for (s = self->s.s; *s != '\0'; s++) {
                if (*s == '{' &&
                    string_format_helper(&s, &t, &lastarg)) {
                        continue;
                }
                buffer_putc(&t, *s);
        }

done:
        qop_assign_cstring(ret, t.s);
}

/* toint() (no args)
 * returns int
 */
static void
string_toint(struct var_t *ret)
{
        struct var_t *self = get_this();
        long long i = 0LL;
        bug_on(self->magic != QSTRING_MAGIC);
        if (self->s.s) {
                int errno_save = errno;
                char *endptr;
                i = strtoll(self->s.s, &endptr, 0);
                if (endptr == self->s.s || errno)
                        i = 0;
                errno = errno_save;
        }
        qop_assign_int(ret, i);
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
        bug_on(self->magic != QSTRING_MAGIC);
        if (self->s.s) {
                int errno_save = errno;
                char *endptr;
                f = strtod(self->s.s, &endptr);
                if (endptr == self->s.s || errno)
                        f = 0.;
                errno = errno_save;
        }
        qop_assign_float(ret, f);
}

static const char *
strip_common(struct var_t *ret)
{
        struct var_t *arg = getarg(0);
        struct var_t *self = get_this();
        bug_on(self->magic != QSTRING_MAGIC);

        /* arg may be NULL, else it must be string */
        if (arg)
                arg_type_check(arg, QSTRING_MAGIC);

        qop_mov(ret, self);
        return arg ? arg->s.s : NULL;
}

/*
 * lstrip()             no args implies whitespace
 * lstrip(charset)      charset is string
 */
static void
string_lstrip(struct var_t *ret)
{
        const char *charset = strip_common(ret);
        buffer_lstrip(&ret->s, charset);
}

/*
 * rstrip()             no args implies whitespace
 * rstrip(charset)      charset is string
 */
static void
string_rstrip(struct var_t *ret)
{
        const char *charset = strip_common(ret);
        buffer_rstrip(&ret->s, charset);
}

/*
 *  strip()             no args implies whitespace
 *  strip(charset)      charset is string
 */
static void
string_strip(struct var_t *ret)
{
        const char *charset = strip_common(ret);
        buffer_rstrip(&ret->s, charset);
        buffer_lstrip(&ret->s, charset);
}

static void
string_replace(struct var_t *ret)
{
        struct var_t *self    = get_this();
        struct var_t *vneedle = getarg(0);
        struct var_t *vrepl   = getarg(1);
        char *haystack, *end;

        bug_on(self->magic != QSTRING_MAGIC);
        bug_on(!vneedle || !vrepl);

        arg_type_check(vneedle, QSTRING_MAGIC);
        arg_type_check(vrepl, QSTRING_MAGIC);

        qop_assign_cstring(ret, "");

        /* end not technically needed, but in case of match() bugs */
        haystack = self->s.s;
        end = haystack + self->s.p;

        if (!haystack || haystack[0] == '\0')
                return;
        if (!vneedle->s.s || vneedle->s.s[0] == '\0') {
                buffer_puts(&ret->s, self->s.s);
                return;
        }

        while (*haystack && haystack < end) {
                ssize_t size = match(vneedle->s.s, haystack);
                if (size == -1)
                         break;
                buffer_nputs(&ret->s, haystack, size);
                buffer_puts(&ret->s, vrepl->s.s);
                haystack += size + vneedle->s.p;
        }
        bug_on(haystack > end);
        if (*haystack != '\0')
                buffer_puts(&ret->s, haystack);
}

static struct type_inittbl_t string_methods[] = {
        V_INITTBL("len",     string_length, 0, 0),
        V_INITTBL("format",  string_format, 0, -1),
        V_INITTBL("toint",   string_toint, 0, 0),
        V_INITTBL("tofloat", string_tofloat, 0, 0),
        V_INITTBL("lstrip",  string_lstrip, 0, 1),
        V_INITTBL("rstrip",  string_rstrip, 0, 1),
        V_INITTBL("replace", string_replace, 2, 2),
        V_INITTBL("strip",   string_strip, 0, 1),
        TBLEND,
};

static void
string_reset(struct var_t *str)
{
        buffer_free(&str->s);
}

static void
string_add(struct var_t *a, struct var_t *b)
{
        if (b->magic != QSTRING_MAGIC)
                emismatch("+");
        buffer_puts(&a->s, b->s.s);
}

static int
string_cmp(struct var_t *a, struct var_t *b)
{
        int r;
        if (!a->s.s)
                return b->s.s ? -1 : 1;
        else if (!b->s.s)
                return 1;
        r = strcmp(a->s.s, b->s.s);
        return r ? (r < 0 ? -1 : 1) : 0;
}

static bool
string_cmpz(struct var_t *a)
{
        if (!a->s.s)
                return true;
        /* In C "" is non-NULL, but here we're calling it zero */
        return a->s.s[0] == '\0';
}

static void
string_mov(struct var_t *to, struct var_t *from)
{
        if (from->magic != QSTRING_MAGIC)
                emismatch("mov");
        qop_assign_cstring(to, from->s.s);
}

static const struct operator_methods_t string_primitives = {
        .add            = string_add,
        .cmp            = string_cmp,
        .cmpz           = string_cmpz,
        .mov            = string_mov,
        .reset          = string_reset,
};

void
typedefinit_string(void)
{
        var_config_type(QSTRING_MAGIC, "string",
                        &string_primitives, string_methods);
}

