#include "egq.h"
#include <string.h>
#include <stdlib.h>

/* only called from op.c */
void
object_mov__(struct var_t *to, struct var_t *from)
{
        to->o.owner = NULL;

        /* XXX is the bug this, or the fact that I'm not handling it? */
        bug_on(!!to->o.h && to->magic == QOBJECT_MAGIC);

        to->o.h = from->o.h;
        to->o.h->nref++;
}

static void
object_handle_reset(struct object_handle_t *oh)
{
        struct buffer_t *buf = &oh->children;
        int i, n = oh_nchildren(oh);
        struct var_t **ppvar = oh_children(oh);
        bug_on(oh->nref < 0);
        if (oh->priv) {
                if (oh->priv_cleanup)
                        oh->priv_cleanup(oh, oh->priv);
                else
                        free(oh->priv);
        }
        for (i = 0; i < n; i++)
                var_delete(ppvar[i]);
        buffer_free(buf);
        free(oh);
}

/* only called from var_reset() */
void
object_reset__(struct var_t *o)
{
        struct object_handle_t *oh;
        bug_on(o->magic != QOBJECT_MAGIC);
        oh = o->o.h;
        oh->nref--;
        if (oh->nref <= 0)
                object_handle_reset(oh);
        o->o.h = NULL;
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
object_from_empty(struct var_t *o)
{
        bug_on(o->magic != QEMPTY_MAGIC);
        o->magic = QOBJECT_MAGIC;

        o->o.h = ecalloc(sizeof(*o->o.h));
        buffer_init(&o->o.h->children);
        o->o.h->nref = 1;
        return o;
}

/**
 * object_child_l - Like object_child, but @s is already known be a
 *                  return value of literal()
 */
struct var_t *
object_child_l(struct var_t *o, const char *s)
{
        int i, n;
        struct var_t **ppvar;

        bug_on(o->magic != QOBJECT_MAGIC);
        bug_on(!o->o.h);

        n = oh_nchildren(o->o.h);
        ppvar = oh_children(o->o.h);

        for (i = 0; i < n; i++) {
                if (ppvar[i] && ppvar[i]->name == s)
                        return ppvar[i];
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
        struct var_t **ppvar;
        struct buffer_t *buf = &o->o.h->children;
        size_t byteoffs = n * sizeof(void *);
        if (byteoffs >= buf->p)
                return NULL;

        ppvar = (struct var_t **)buf->s + byteoffs;
        return *ppvar;
}

void
object_add_child(struct var_t *parent, struct var_t *child)
{
        if (child->magic == QOBJECT_MAGIC) {
                child->o.owner = parent;
        } else if (child->magic == QFUNCTION_MAGIC) {
                child->fn.owner = parent;
        }
        buffer_putd(&parent->o.h->children, &child, sizeof(void *));
}

