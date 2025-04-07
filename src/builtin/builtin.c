/* q-builtin.c - Built-in callbacks for script */
#include "builtin.h"
#include <evilcandy.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define NLMAX 8

/* Private data for the global object */
static struct gbl_private_t {
        char nl[NLMAX];
} gbl;

static struct var_t *
do_typeof(struct vmframe_t *fr)
{
        struct var_t *ret = var_new();
        struct var_t *p = frame_get_arg(fr, 0);
        string_init(ret, typestr(p));
        return ret;
}

static bool
do_print_helper(struct var_t *v)
{
        switch (v->magic) {
        case TYPE_INT:
                printf("%lld", v->i);
                break;
        case TYPE_FLOAT:
                printf("%.8g", v->f);
                break;
        case TYPE_EMPTY:
                printf("(null)");
                break;
        case TYPE_STRING:
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

static struct var_t *
do_print(struct vmframe_t *fr)
{
        struct var_t *p = frame_get_arg(fr, 0);
        if (p->magic == TYPE_STRING) {
                char *s = string_get_cstring(p);
                while (*s)
                        putchar((int)*s++);
        } else {
                do_print_helper(p);
        }
        print_nl();
        return var_new();
}

static struct var_t *
do_exit(struct vmframe_t *fr)
{
        struct var_t *p = frame_get_arg(fr, 0);
        if (p && p->magic == TYPE_STRING)
                printf("%s\n", string_get_cstring(p));
        exit(0);

        /*
         * we'll obviously never reach this.
         * Compilers make me do these things.
         */
        return var_new();
}

static struct var_t *
do_setnl(struct vmframe_t *fr)
{
        struct var_t *nl = frame_get_arg(fr, 0);
        char *s;
        if (nl->magic != TYPE_STRING) {
                err_argtype("string");
                return ErrorVar;
        }
        s = string_get_cstring(nl);
        memset(gbl.nl, 0, NLMAX);
        strncpy(gbl.nl, s, NLMAX-1);
        return var_new();
}

static const struct inittbl_t gblinit[] = {
        TOFTBL("print",  do_print,  1, -1),
        TOFTBL("setnl",  do_setnl,  1, 1),
        TOFTBL("typeof", do_typeof, 1, 1),
        /* XXX: maybe exit should be a method of __gbl__._sys */
        TOFTBL("exit",   do_exit,   0, -1),
        TOOTBL("_math",  bi_math_inittbl__),
        TOOTBL("_io",    bi_io_inittbl__),
        TOSTBL("ParserError", "Parser Error"),
        TOSTBL("RuntimeError",  "Runtime Error"),
        /*
         * these are for non-fatal errors. Things like bug traps
         * or failed malloc calls result in a stderr message and exit(),
         * so no fancy error handling for that.
         */
        TOSTBL("SystemError",   "System error"),
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
                case TYPE_DICT:
                        object_init(child);
                        bi_build_internal_object__(child, t->tbl);
                        break;
                case TYPE_FUNCTION:
                        function_init_internal(child,
                                        t->cb, t->minargs, t->maxargs);
                        break;
                case TYPE_STRING:
                        string_init(child, t->s);
                        child->flags = VF_CONST;
                        break;
                case TYPE_INT:
                        integer_init(child, t->i);
                        child->flags = VF_CONST;
                        break;
                case TYPE_FLOAT:
                        float_init(child, t->f);
                        child->flags = VF_CONST;
                        break;
                default:
                        bug();
                }
                bug_on(child->magic == TYPE_EMPTY);
                if (object_add_child(parent, child, literal_put(t->name))
                    != 0) {
                        /*
                         * Whether this is a "bug" or not is philosophical.
                         * Anyway, it can't be user error, so something
                         * internal failed this operation.
                         */
                        bug();
                }
        }
}

struct var_t *GlobalObject;
struct var_t *ParserError;
struct var_t *RuntimeError;
struct var_t *SystemError;

/* initialize the builtin/ C file modules */
void
moduleinit_builtin(void)
{
        /* Do this first.  bi_build_internal_object__ de-references it. */
        GlobalObject = var_new();
        object_init(GlobalObject);
        object_set_priv(GlobalObject, &gbl, NULL);
        bi_build_internal_object__(GlobalObject, gblinit);

        ParserError     = object_child(GlobalObject, "ParserError");
        RuntimeError    = object_child(GlobalObject, "RuntimeError");
        SystemError     = object_child(GlobalObject, "SystemError");
        if (!ParserError || !RuntimeError || !SystemError) {
                fail("Could not create error objects");
        }

        /* Set up gbl private data */
        strcpy(gbl.nl, "\n");
}

