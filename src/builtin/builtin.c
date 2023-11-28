/* q-builtin.c - Built-in callbacks for script */
#include "builtin.h"
#include <egq.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define NLMAX 8

/* Private data for the global object */
static struct gbl_private_t {
        char nl[NLMAX];
} gbl;

static void
do_typeof(struct var_t *ret)
{
        struct var_t *p = getarg(0);
        qop_assign_cstring(ret, typestr(p->magic));
}

static bool
do_print_helper(struct var_t *v)
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
                printf("%s", string_get_cstring(v));
                break;
        default:
                return false;
        }
        return true;
}

static void
print_nl(void)
{
        char *s;
        for (s = gbl.nl; *s; s++)
                putchar((int)*s);
}

static void
do_print(struct var_t *ret)
{
        struct var_t *p = getarg(0);
        if (p->magic == QSTRING_MAGIC) {
                char *s = string_get_cstring(p);
                while (*s)
                        putchar((int)*s++);
        } else {
                do_print_helper(p);
        }
        print_nl();
        /* return empty */
}

static void
do_exit(struct var_t *ret)
{
        struct var_t *p = getarg(0);
        if (p && p->magic == QSTRING_MAGIC)
                printf("%s\n", string_get_cstring(p));
        exit(0);
}

static void
do_setnl(struct var_t *ret)
{
        struct var_t *nl = getarg(0);
        char *s;
        if (nl->magic != QSTRING_MAGIC)
                syntax("Expected argument: string");
        s = string_get_cstring(nl);
        memset(gbl.nl, 0, NLMAX);
        strncpy(gbl.nl, s, NLMAX-1);
}

static const struct inittbl_t gblinit[] = {
        TOFTBL("print",  do_print,  1, -1),
        TOFTBL("setnl",  do_setnl,  1, 1),
        TOFTBL("typeof", do_typeof, 1, 1),
        TOFTBL("exit",   do_exit,   0, -1),
        TOOTBL("Math",  bi_math_inittbl__),
        TOOTBL("Io",    bi_io_inittbl__),
        { .name = NULL },
};

/**
 * bi_build_internal_object__ - build up a C-defined object with a
 *                              linear table
 * @parent: The object to add new children to.  This is already set
 *          to be an object, and may or may not have children already
 * @tbl: Table to scan.  A unique child will be created for each member
 *       of this table.
 */
void
bi_build_internal_object__(struct var_t *parent, const struct inittbl_t *tbl)
{
        const struct inittbl_t *t;
        if (!tbl)
                return;
        for (t = tbl; t->name != NULL; t++) {
                struct var_t *child = var_new();
                switch (t->magic) {
                case QOBJECT_MAGIC:
                        object_init(child);
                        bi_build_internal_object__(child, t->tbl);
                        break;
                case QFUNCTION_MAGIC:
                        function_init_internal(child,
                                        t->cb, t->minargs, t->maxargs);
                        break;
                case QSTRING_MAGIC:
                        qop_assign_cstring(child, t->s);
                        child->flags = VF_CONST;
                        break;
                case QINT_MAGIC:
                        qop_assign_int(child, t->i);
                        child->flags = VF_CONST;
                        break;
                case QFLOAT_MAGIC:
                        qop_assign_float(child, t->f);
                        child->flags = VF_CONST;
                        break;
                default:
                        bug();
                }
                bug_on(child->magic == QEMPTY_MAGIC);
                object_add_child(parent, child, literal_put(t->name));
        }
}

/* initialize the builtin/ C file modules */
void
moduleinit_builtin(void)
{
        /* Do this first.  bi_build_internal_object__ de-references it. */
        q_.gbl = var_new();
        q_.gbl->name = literal_put("__gbl__");
        object_init(q_.gbl);
        object_set_priv(q_.gbl, &gbl, NULL);
        bi_build_internal_object__(q_.gbl, gblinit);

        /* Set up gbl private data */
        strcpy(gbl.nl, "\n");
}

