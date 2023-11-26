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

static struct string_handle_t *
new_string_handle(void)
{
        struct string_handle_t *ret = emalloc(sizeof(*ret));
        ret->nref = 0;
        buffer_init(&ret->b);
        return ret;
}

static void
string_handle_reset(struct string_handle_t *sh)
{
        buffer_free(&sh->b);
        free(sh);
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
        bug_on(self->magic != QSTRING_MAGIC);
        qop_assign_int(ret, string_length(self));
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
        bug_on(self->magic != QSTRING_MAGIC);

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
        char *s;
        bug_on(self->magic != QSTRING_MAGIC);
        s = string_get_cstring(self);
        if (s) {
                int errno_save = errno;
                char *endptr;
                i = strtoll(s, &endptr, 0);
                if (endptr == s || errno)
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
        char *s;
        bug_on(self->magic != QSTRING_MAGIC);
        s = string_get_cstring(self);
        if (s) {
                int errno_save = errno;
                char *endptr;
                f = strtod(s, &endptr);
                if (endptr == s || errno)
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
        struct var_t *vneedle = getarg(0);
        struct var_t *vrepl   = getarg(1);
        char *haystack, *needle, *end;
        size_t needle_len;

        bug_on(self->magic != QSTRING_MAGIC);
        bug_on(!vneedle || !vrepl);

        arg_type_check(vneedle, QSTRING_MAGIC);
        arg_type_check(vrepl, QSTRING_MAGIC);

        /* guarantee ret is string */
        if (ret->magic == QEMPTY_MAGIC)
                string_init(ret);
        /* XXX bug, or syntax error? */
        bug_on(ret->magic != QSTRING_MAGIC);

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

static struct type_inittbl_t string_methods[] = {
        V_INITTBL("len",     string_length_method, 0, 0),
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
        str->s->nref--;
        if (str->s->nref <= 0) {
                bug_on(str->s->nref < 0);
                string_handle_reset(str->s);
        }
}

static void
string_add(struct var_t *a, struct var_t *b)
{
        if (b->magic != QSTRING_MAGIC)
                emismatch("+");
        buffer_puts(string_buf__(a), string_get_cstring(b));
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
        if (from->magic != QSTRING_MAGIC)
                emismatch("mov");
        bug_on(!!to->s && to->magic == QSTRING_MAGIC);
        to->s = from->s;
        to->s->nref++;
}

static const struct operator_methods_t string_primitives = {
        .add            = string_add,
        .cmp            = string_cmp,
        .cmpz           = string_cmpz,
        .mov            = string_mov,
        .reset          = string_reset,
};

/**
 * string_init - Convert an empty variable into a string type
 * @var: An empty variable to turn into a string
 *
 * Return: @var
 */
struct var_t *
string_init(struct var_t *var)
{
        bug_on(var->magic != QEMPTY_MAGIC);
        var->magic = QSTRING_MAGIC;
        var->s = new_string_handle();
        var->s->nref = 1;
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
        bug_on(str->magic != QSTRING_MAGIC);

        struct buffer_t *buf = string_buf__(str);
        buffer_reset(buf);
        if (!s)
                s = "";
        buffer_puts(buf, s);
}

/**
 * string_substr - Get substring
 * @str: String object
 * @i:  Index into the string to get.
 *      <0 means indexed from the end.
 *
 * Return character at @i, or '\0' if @i is out of range.
 *
 * XXX: Bad name, we're returning a char,
 * not a nulchar-terminated C string
 */
int
string_substr(struct var_t *str, int i)
{
        char *s;

        bug_on(str->magic != QSTRING_MAGIC);
        i = index_translate(i, string_length(str));
        if (i < 0)
                return '\0';
        s = string_get_cstring(str);
        return s ? s[i] : '\0';
}

void
typedefinit_string(void)
{
        var_config_type(QSTRING_MAGIC, "string",
                        &string_primitives, string_methods);
}

