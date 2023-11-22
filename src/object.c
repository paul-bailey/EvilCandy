#include "egq.h"
#include <string.h>

/* only called from var_reset() */
void
object_reset(struct var_t *o)
{
        struct list_t *child, *tmp;
        bug_on(o->magic != QOBJECT_MAGIC);
        list_foreach_safe(child, tmp, &o->o.h->children)
                var_delete(list2var(child));
}

/**
 * object_new - Create a new object and set its owner and name
 * @owner: Owning object
 * @name: Name of the object
 *
 * Return: New object
 */
struct var_t *
object_new(struct var_t *owner, const char *name)
{
        struct var_t *o = object_from_empty(var_new());
        o->name = literal(name);
        if (owner)
                object_add_child(owner, o);
        return o;
}

/**
 * object_from_empty - Convert an empty variable into an initialized
 *                      object type.
 * @v: variable
 *
 * Return: @v
 *
 * This is an alternative to object_new();
 */
struct var_t *
object_from_empty(struct var_t *v)
{
        struct var_t *o = (struct var_t*)v;

        bug_on(o->magic != QEMPTY_MAGIC);
        o->magic = QOBJECT_MAGIC;

        o->o.h = emalloc(sizeof(*o->o.h));
        list_init(&o->o.h->children);
        o->o.h->nref = 1;
        o->o.owner = NULL;
        return o;
}

/**
 * object_child_l - Like object_child, but @s is already known be a
 *                  return value of literal()
 */
struct var_t *
object_child_l(struct var_t *o, const char *s)
{
        struct list_t *child, *parent = &o->o.h->children;
        bug_on(!parent);
        bug_on(o->magic != QOBJECT_MAGIC);

        /*
         * TODO: This is still a linear search--O(#children) to be an
         * egghead about it--Maybe objects should have their own built-in
         * hashtables or tries, since some objects' lists can get quite
         * long.
         */
        list_foreach(child, parent) {
                struct var_t *v = list2var(child);
                if (v->name == s)
                        return v;
        }
        return builtin_method(o, s);
}

/**
 * object_child - Return an object's child
 * @o:  Object to seek the child of.
 * @s:  Name of the child
 *
 * Return:    - the child if found
 *            - the built-in method matching @s if the child is not found
 *            - NULL if neither are found.
 */
struct var_t *
object_child(struct var_t *o, const char *s)
{
        return object_child_l(o, literal(s));
}

/* n begins at zero, not one */
struct var_t *
object_nth_child(struct var_t *o, int n)
{
        int i = 0;
        struct list_t *child, *parent = &o->o.h->children;
        list_foreach(child, parent) {
                if (i == n)
                        return list2var(child);
                i++;
        }
        return NULL;
}

void
object_add_child(struct var_t *parent, struct var_t *child)
{
        if (child->magic == QOBJECT_MAGIC) {
                child->o.owner = parent;
        } else if (child->magic == QFUNCTION_MAGIC) {
                child->fn.owner = parent;
        }
        list_add_tail(&child->siblings, &parent->o.h->children);
}

