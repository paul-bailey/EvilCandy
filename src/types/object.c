#include "var.h"
#include <string.h>
#include <stdlib.h>

static inline size_t oh_nchildren(struct object_handle_t *oh)
        { return oh->children.p / sizeof(void *); }
static inline struct var_t **oh_children(struct object_handle_t *oh)
        { return (struct var_t **)oh->children.s; }

/* **********************************************************************
 *                              API functions
 ***********************************************************************/

/**
 * object_init - Convert an empty variable into an initialized
 *                      object type.
 * @v: An empty variable to turn into an object
 *
 * Return: @v
 */
struct var_t *
object_init(struct var_t *o)
{
        bug_on(o->magic != QEMPTY_MAGIC);
        o->magic = QOBJECT_MAGIC;

        o->o.h = ecalloc(sizeof(*o->o.h));
        buffer_init(&o->o.h->children);
        o->o.h->nref = 1;
        return o;
}

/**
 * object_set_priv - Set an object's private data
 * @o: Object
 * @priv: Private data to set
 * @cleanup: Cleanup method to clean up private data at garbage
 *           collection time, or NULL to let it be simply free'd
 */
void
object_set_priv(struct var_t *o, void *priv,
                void (*cleanup)(struct object_handle_t *, void *))
{
        bug_on(o->magic != QOBJECT_MAGIC);
        o->o.h->priv = priv;
        o->o.h->priv_cleanup = cleanup;
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
 * object_nth_child - Get the nth child of an object
 * @o: object to seek
 * @n: The "n" of "nth", indexed from zero
 *
 * Return: the nth child--which of course could be anything, since this
 * is an associative array--or NULL if @n is out of bounds.
 */
struct var_t *
object_nth_child(struct var_t *o, int n)
{
        struct var_t **ppvar;
        struct buffer_t *buf = &o->o.h->children;
        ssize_t byteoffs = n * sizeof(void *);

        byteoffs = index_translate(byteoffs, buffer_size(buf));
        if (byteoffs < 0)
                return NULL;

        ppvar = (struct var_t **)buf->s + byteoffs;
        return *ppvar;
}

/**
 * object_add_child - Append a child to an object
 * @parent: object to append a child to
 * @child: child to append to @parent
 */
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

/* **********************************************************************
 *              Built-in Operator Callbacks
 ***********************************************************************/

/* qop_mov callback for object */
static void
object_mov(struct var_t *to, struct var_t *from)
{
        to->o.owner = NULL;

        /* XXX is the bug this, or the fact that I'm not handling it? */
        bug_on(!!to->o.h && to->magic == QOBJECT_MAGIC);

        to->o.h = from->o.h;
        to->o.h->nref++;
}

static bool
object_cmpz(struct var_t *obj)
{
        return false;
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

static void
object_reset(struct var_t *o)
{
        struct object_handle_t *oh;
        bug_on(o->magic != QOBJECT_MAGIC);
        oh = o->o.h;
        oh->nref--;
        if (oh->nref <= 0)
                object_handle_reset(oh);
        o->o.h = NULL;
}

/* **********************************************************************
 *                      Built-in Methods
 ***********************************************************************/

/*
 * foreach(function)
 *      function may be user-defined or built-in (usu. the former).  Its
 *      argument is the specific object child, which is whatever type
 *      it happens to be.
 * Returns nothing
 */
void
object_foreach(struct var_t *ret)
{
        struct var_t *self = get_this();
        struct var_t *func = getarg(0);
        struct var_t **ppvar;
        int i, n;

        if (!func || !isfunction(func))
                syntax("Expected: function");
        bug_on(self->magic != QOBJECT_MAGIC);

        n = oh_nchildren(self->o.h);
        ppvar = oh_children(self->o.h);
        for (i = 0; i < n; i++) {
                if (!ppvar[i])
                        continue;
                call_function_from_intl(func, NULL, NULL, 1, &ppvar[i]);
        }
}


/*
 * len()  (no args)
 * returns number of elements in object
 */
static void
object_len(struct var_t *ret)
{
        struct var_t *v;
        int i = 0;

        v = getarg(0);
        if (!v) {
                v = get_this();
                bug_on(v->magic != QOBJECT_MAGIC);
        }
        switch (v->magic) {
        case QOBJECT_MAGIC:
                i = oh_nchildren(v->o.h);
                break;
        case QSTRING_MAGIC:
                i = string_length(v);
                break;
        default:
                i = 1;
        }
        qop_assign_int(ret, i);
}

static const struct type_inittbl_t object_methods[] = {
        V_INITTBL("len",    object_len,    0, 0),
        V_INITTBL("foreach", object_foreach, 1, 1),
        TBLEND,
};

/*
 * FIXME: Would be nice if we could do like Python and let objects have
 * user-defined operator callbacks
 */
static const struct operator_methods_t object_primitives = {
        .cmpz           = object_cmpz,
        .mov            = object_mov,
        .reset          = object_reset,
};

void
typedefinit_object(void)
{
        var_config_type(QOBJECT_MAGIC, "object",
                        &object_primitives, object_methods);
}

