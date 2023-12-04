#include "var.h"
#include <string.h>
#include <stdlib.h>

static inline size_t oh_nchildren(struct object_handle_t *oh)
        { return oh->nchildren; }

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

        o->o = ecalloc(sizeof(*o->o));
        hashtable_init(&o->o->dict, ptr_hash,
                       ptr_key_match, var_bucket_delete);
        o->o->nref = 1;
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
        o->o->priv = priv;
        o->o->priv_cleanup = cleanup;
}

/**
 * object_child_l - Like object_child, but @s is already known be a
 *                  return value of literal()
 */
struct var_t *
object_child_l(struct var_t *o, const char *s)
{
        bug_on(o->magic != QOBJECT_MAGIC);
        bug_on(!o->o);

        return hashtable_get(&o->o->dict, s);
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
        /* FIXME: This needs a hashtable nth child or something */
#if 1
        return NULL;
#else
        struct var_t **ppvar;
        struct buffer_t *buf = &o->o->children;
        ssize_t byteoffs = n * sizeof(void *);

        byteoffs = index_translate(byteoffs, buffer_size(buf));
        if (byteoffs < 0)
                return NULL;

        ppvar = (struct var_t **)buf->s + byteoffs;
        return *ppvar;
#endif
}

/**
 * object_add_child - Append a child to an object
 * @parent: object to append a child to
 * @child: child to append to @parent
 * @name: Name of the child.
 */
void
object_add_child(struct var_t *parent, struct var_t *child, char *name)
{
        if (hashtable_put(&parent->o->dict, name, child) < 0)
                syntax("Object already has element named %s", name);
        parent->o->nchildren++;
}


/* **********************************************************************
 *              Built-in Operator Callbacks
 ***********************************************************************/

/* qop_mov callback for object */
static void
object_mov(struct var_t *to, struct var_t *from)
{
        /* XXX is the bug this, or the fact that I'm not handling it? */
        bug_on(!!to->o && to->magic == QOBJECT_MAGIC);

        to->o = from->o;
        to->o->nref++;
}

static bool
object_cmpz(struct var_t *obj)
{
        return false;
}

static void
object_handle_reset(struct object_handle_t *oh)
{
        bug_on(oh->nref < 0);
        if (oh->priv) {
                if (oh->priv_cleanup)
                        oh->priv_cleanup(oh, oh->priv);
                else
                        free(oh->priv);
        }
        hashtable_destroy(&oh->dict);
        free(oh);
}

static void
object_reset(struct var_t *o)
{
        struct object_handle_t *oh;
        bug_on(o->magic != QOBJECT_MAGIC);
        oh = o->o;
        oh->nref--;
        if (oh->nref <= 0)
                object_handle_reset(oh);
        o->o = NULL;
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
        struct var_t *func = frame_get_arg(0);
        unsigned int idx;
        struct hashtable_t *htbl;
        void *key, *val;
        int res;

        struct var_t *argv[2];
        argv[0] = var_new(); /* attribute */
        argv[1] = var_new(); /* name of attribute */

        if (!func)
                syntax("Expected: function");
        bug_on(self->magic != QOBJECT_MAGIC);
        htbl = &self->o->dict;

        for (idx = 0, res = hashtable_iterate(htbl, &key, &val, &idx);
             res == 0; res = hashtable_iterate(htbl, &key, &val, &idx)) {
                qop_clobber(argv[0], (struct var_t *)val);
                qop_assign_cstring(argv[1], (char *)key);
                /*
                 * XXX REVISIT: should ``this'' in a foreach callback
                 * be the owner of the foreach method (us)?  Or should it
                 * be the object owning the calling function?  This is a
                 * philosophical conundrum, not a bug.
                 */
                vm_reenter(func, NULL, 2, argv);
        }
        var_delete(argv[0]);
        var_delete(argv[1]);
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

        v = frame_get_arg(0);
        if (!v) {
                v = get_this();
                bug_on(v->magic != QOBJECT_MAGIC);
        }
        switch (v->magic) {
        case QOBJECT_MAGIC:
                i = oh_nchildren(v->o);
                break;
        case QSTRING_MAGIC:
                i = string_length(v);
                break;
        default:
                i = 1;
        }
        qop_assign_int(ret, i);
}

static void
object_haschild(struct var_t *ret)
{
        struct var_t *self = get_this();
        struct var_t *name = frame_get_arg(0);
        struct var_t *child = NULL;
        char *s;

        bug_on(self->magic != QOBJECT_MAGIC);

        if (!name || name->magic != QSTRING_MAGIC)
                syntax("Expected arg: 'name'");

        if ((s = string_get_cstring(name)) != NULL)
                child = object_child(self, s);

        qop_assign_int(ret, (int)(child != NULL));
}

static const struct type_inittbl_t object_methods[] = {
        V_INITTBL("len",       object_len,       0, 0),
        V_INITTBL("foreach",   object_foreach,   1, 1),
        V_INITTBL("haschild",  object_haschild,  1, 1),
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
        var_config_type(QOBJECT_MAGIC, "dictionary",
                        &object_primitives, object_methods);
}

