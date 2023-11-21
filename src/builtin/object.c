/* object.c - builtin methods for object data types */
#include "builtin.h"
#include <string.h>

/*
 * foreach(function)
 *      function may be user-defined or built-in (usu. the former).  Its
 *      argument is the specific object child, which is whatever type
 *      it happens to be.
 * Returns nothing
 */
static void
object_foreach(struct var_t *ret)
{
        struct list_t *child, *tmp;
        struct var_t *self = get_this();
        struct var_t *fn = getarg(0);

        if (!fn || !isfunction(fn))
                syntax("Expected: function");
        bug_on(self->magic != QOBJECT_MAGIC);

        list_foreach_safe(child, tmp, &self->o.h->children) {
                struct var_t *elem = list2var(child);
                call_function_from_intl(fn, NULL, NULL, 1, &elem);
        }
}

/*
 * len()  (no args)
 * returns number of elements in object
 */
static void
object_len(struct var_t *ret)
{
        struct list_t *list;
        struct var_t *v;
        int i = 0;

        v = getarg(0);
        if (!v) {
                v = get_this();
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

/*
 * append(object)
 *      Copy the arg's children over to self.
 *      Could have been called "inherit"
 * returns nothing
 */
static void
object_append(struct var_t *ret)
{
        warning("object .append method not supported yet");
}

static const struct inittbl_t object_methods[] = {
        TOFTBL("len",    object_len,    0, 0),
        TOFTBL("append", object_append, 0, 0),
        TOFTBL("foreach", object_foreach, 1, 1),
        TBLEND,
};

void
bi_moduleinit_object__(void)
{
        bi_init_type_methods__(object_methods, QOBJECT_MAGIC);
}

