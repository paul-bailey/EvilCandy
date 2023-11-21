/* q-builtin.c - Built-in callbacks for script */
#include "builtin.h"
#include <egq.h>
#include <stdlib.h>
#include <string.h>

static void
qb_typeof(struct var_t *ret)
{
        struct var_t *p = getarg(0);
        qop_assign_cstring(ret, typestr(p->magic));
}

static void
float_tostr(struct var_t *ret)
{
        char buf[64];
        ssize_t len;
        struct var_t *self = get_this();
        bug_on(self->magic != QFLOAT_MAGIC);
        len = snprintf(buf, sizeof(buf), "%.8g", self->f);
        /* this should be impossible */
        bug_on(len >= sizeof(buf));
        qop_assign_cstring(ret, buf);
}

static void
int_tostr(struct var_t *ret)
{
        char buf[64];
        ssize_t len;
        struct var_t *self = get_this();
        bug_on(self->magic != QINT_MAGIC);
        len = snprintf(buf, sizeof(buf), "%lld", self->i);
        bug_on(len >= sizeof(buf));
        qop_assign_cstring(ret, buf);
}


static bool
qb_print_helper(struct var_t *v)
{
        switch (v->magic) {
        case QINT_MAGIC:
                printf("%lld", v->i);
                break;
        case QFLOAT_MAGIC:
                printf("%.8g", v->f);
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

/* Initialize the global object */
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
                } else if (t->magic == QINTL_MAGIC) {
                        child = var_new();
                        child->name = literal(t->name);
                        child->magic = QINTL_MAGIC;
                        child->fni   = &t->h;
                        object_add_child(parent, child);
                } else {
                        /*
                         * TODO: support this.  Need some way of making
                         * sure they're const.
                         */
                        bug();
                }
        }
}

static const struct inittbl_t gblinit[] = {
        TOFTBL("print",  qb_print,  1, -1),
        TOFTBL("typeof", qb_typeof, 1, 1),
        TOFTBL("exit",   qb_exit,   0, -1),
        TOOTBL("Math", bi_math_inittbl__),
        { .name = NULL },
};

static const struct inittbl_t float_methods[] = {
        TOFTBL("tostr", float_tostr, 0, 0),
        TBLEND,
};

static const struct inittbl_t int_methods[] = {
        TOFTBL("tostr", int_tostr, 0, 0),
        TBLEND,
};

/**
 * bi_init_type_methods__ - Initialize a type's built-in methods
 * @tbl: Table describing methods
 * @magic: Q*_MAGIC enum corresponding to the typedef
 */
void
bi_init_type_methods__(const struct inittbl_t *tbl, int magic)
{
        const struct inittbl_t *t = tbl;
        struct list_t *parent_list = &TYPEDEFS[magic].methods;
        while (t->name != NULL) {
                struct var_t *v = var_new();
                bug_on(!strcmp(t->name, "SANITY"));
                v->magic = QINTL_MAGIC;
                v->name = literal(t->name);
                v->fni = &t->h;
                list_add_tail(&v->siblings, parent_list);
                t++;
        }
}

void
moduleinit_builtin(void)
{
        int i;

        /* Do this first.  initialize_helper de-references it. */
        q_.gbl = object_new(NULL, "__gbl__");

        initialize_helper(q_.gbl, gblinit);
        for (i = QEMPTY_MAGIC; i < Q_NMAGIC; i++)
                list_init(&TYPEDEFS[i].methods);
        bi_moduleinit_string__();
        bi_moduleinit_object__();

        bi_init_type_methods__(float_methods, QFLOAT_MAGIC);
        bi_init_type_methods__(int_methods, QINT_MAGIC);
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
                w = list2var(m);
                if (!strcmp(w->name, method_name))
                        return w;
        }
        return NULL;
}


