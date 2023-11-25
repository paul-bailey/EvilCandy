/* q-builtin.c - Built-in callbacks for script */
#include "builtin.h"
#include <egq.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void
qb_typeof(struct var_t *ret)
{
        struct var_t *p = getarg(0);
        qop_assign_cstring(ret, typestr(p->magic));
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
                printf("%s", string_get_cstring(v));
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
                char *s = string_get_cstring(p);
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
                printf("%s\n", string_get_cstring(p));
        exit(0);
}

static const struct inittbl_t gblinit[] = {
        TOFTBL("print",  qb_print,  1, -1),
        TOFTBL("typeof", qb_typeof, 1, 1),
        TOFTBL("exit",   qb_exit,   0, -1),
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
                child->name = literal(t->name);
                switch (t->magic) {
                case QOBJECT_MAGIC:
                        object_init(child);
                        bi_build_internal_object__(child, t->tbl);
                        break;
                case QPTRXI_MAGIC:
                        child->magic = t->magic;
                        child->fni = &t->h;
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
                object_add_child(parent, child);
        }
}

/* initialize the builtin/ C file modules */
void
moduleinit_builtin(void)
{
        /* Do this first.  bi_build_internal_object__ de-references it. */
        q_.gbl = var_new();
        q_.gbl->name = literal("__gbl__");
        object_init(q_.gbl);
        bi_build_internal_object__(q_.gbl, gblinit);
}

