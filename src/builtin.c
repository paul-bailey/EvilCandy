/* q-builtin.c - Built-in callbacks for script */
#include "egq.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static struct var_t *
getarg(int n)
{
        if (n < 0 || n >= (q_.sp - 1 - q_.fp))
                return NULL;
        return (struct var_t *)(q_.fp + 1 + n);
}

static struct var_t *
getself(void)
{
        return q_.fp;
}

static void
qb_typeof(struct var_t *ret)
{
        struct var_t *p = getarg(0);
        qop_assign_cstring(ret, typestr(p->magic));
}

static void
object_len(struct var_t *ret)
{
        struct list_t *list;
        struct var_t *v;
        int i = 0;

        v = getarg(0);
        if (!v) {
                v = getself();
                bug_on(v->magic != QOBJECT_MAGIC);
        }
        switch (v->magic) {
        case QOBJECT_MAGIC:
                i = 0;
                list_foreach(list, &v->o.h->children)
                        ++i;
                break;
        case QSTRING_MAGIC:
                i = 0;
                if (v->s.s)
                      i = strlen(v->s.s);
                break;
        default:
                i = 1;
        }
        qop_assign_int(ret, i);
}

static void
object_append(struct var_t *ret)
{
        warning("object .append method not supported yet");
}

static void
float_tostr(struct var_t *ret)
{
}

static void
int_tostr(struct var_t *ret)
{
}

static void
string_length(struct var_t *ret)
{
        struct var_t *self = getself();
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

static void
string_format(struct var_t *ret)
{
        static struct buffer_t t = { 0 };
        struct var_t *self = getself();
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

static void
string_toint(struct var_t *ret)
{
}

static void
string_tofloat(struct var_t *ret)
{
}


static bool
qb_print_helper(struct var_t *v)
{
        switch (v->magic) {
        case QINT_MAGIC:
                printf("%lld", v->i);
                break;
        case QFLOAT_MAGIC:
                printf("%g", v->f);
                break;
        case QEMPTY_MAGIC:
                printf("(null)");
                break;
        case QSTRING_MAGIC:
                printf("%s", v->s.s);
                break;
        default:
                return false;
        }
        return true;
}

static void
qb_print(struct var_t *ret)
{
        struct var_t *p = getarg(0);
        if (p->magic == QSTRING_MAGIC) {
                char *s = p->s.s;
                while (*s)
                        putchar((int)*s++);
        } else {
                qb_print_helper(p);
        }
        /* return empty */
}

static void
qb_exit(struct var_t *ret)
{
        struct var_t *p = getarg(0);
        if (p && p->magic == QSTRING_MAGIC)
                printf("%s\n", p->s.s);
        exit(0);
}

#define TOFTBL(n, cb, m, M) \
        { .magic = QINTL_MAGIC, .name = n, \
                .h = { .fn = cb, .minargs = m, .maxargs = M }}

#define TOOTBL(n, p) \
        { .magic = QOBJECT_MAGIC, .name = n, .tbl = p }

#define TBLEND { .name = NULL }

struct inittbl_t {
        int magic;
        const char *name;
        union {
                struct func_intl_t h;
                struct inittbl_t *tbl;
        };
};

static void
initialize_helper(struct var_t *parent, const struct inittbl_t *tbl)
{
        const struct inittbl_t *t;
        if (!tbl)
                return;
        for (t = tbl; t->name != NULL; t++) {
                struct var_t *child;
                if (t->magic == QOBJECT_MAGIC) {
                        child = object_new(parent, t->name);
                        initialize_helper(child, t->tbl);
                } else {
                        child = var_new();
                        child->name = literal(t->name);
                        child->magic = QINTL_MAGIC;
                        child->fni   = &t->h;
                        object_add_child(parent, child);
                }
        }
}

static const struct inittbl_t gblinit[] = {
        TOFTBL("print",  qb_print,  1, -1),
        TOFTBL("typeof", qb_typeof, 1, 1),
        TOFTBL("exit",   qb_exit,   0, -1),
        { .name = NULL },
};

static const struct inittbl_t typemethods[] = {
        /* QEMPTY_MAGIC */
        TBLEND,
        /* QOBJECT_MAGIC */
        TOFTBL("len",    object_len,    0, 0),
        TOFTBL("append", object_append, 0, 0),
        TBLEND,
        /* QFUNCTION_MAGIC */
        TBLEND,
        /* QFLOAT_MAGIC */
        TOFTBL("tostr", float_tostr, 0, 0),
        TBLEND,
        /* QINT_MAGIC */
        TOFTBL("tostr", int_tostr, 0, 0),
        TBLEND,
        /* QSTRING_MAGIC */
        TOFTBL("len",     string_length, 0, 0),
        TOFTBL("format",  string_format, 0, -1),
        TOFTBL("toint",   string_toint, 0, 0),
        TOFTBL("tofloat", string_tofloat, 0, 0),
        TBLEND,
        /* QPTRX_MAGIC */
        TBLEND,
        /* QINTL_MAGIC */
        TBLEND,
        /* QARRAY_MAGIC */
        TBLEND,
        /* Q_NMAGIC */
        { .name = "SANITY" },
};

static const struct inittbl_t *
typemethod_helper(const struct inittbl_t *tbl,
                  struct list_t *parent_list)
{
        const struct inittbl_t *t = tbl;
        while (t->name != NULL) {
                struct var_t *v = var_new();
                bug_on(!strcmp(t->name, "SANITY"));
                v->magic = QINTL_MAGIC;
                v->name = literal(t->name);
                v->fni = &t->h;
                list_add_tail(&v->siblings, parent_list);
                t++;
        }
        return t + 1;
}

void
moduleinit_builtin(void)
{
        int i;
        const struct inittbl_t *t = typemethods;

        /* Do this first.  initialize_helper de-references it. */
        q_.gbl = object_new(NULL, "__gbl__");

        initialize_helper(q_.gbl, gblinit);
        for (i = 0; i < Q_NMAGIC; i++) {
                struct list_t *list = &TYPEDEFS[i].methods;
                list_init(list);
                t = typemethod_helper(t, list);
        }
}

/**
 * Check if @v's type has a built-in method named @method_name
 *      and return it if it does, NULL otherwise
 */
struct var_t *
builtin_method(struct var_t *v, const char *method_name)
{
        int magic = v->magic;
        struct list_t *methods, *m;
        struct var_t *w;

        bug_on(magic < 0 || magic > Q_NMAGIC);

        methods = &TYPEDEFS[magic].methods;
        list_foreach(m, methods) {
                w = container_of(m, struct var_t, siblings);
                if (!strcmp(w->name, method_name))
                        return w;
        }
        return NULL;
}


