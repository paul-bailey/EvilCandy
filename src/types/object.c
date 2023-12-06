#include "var.h"
#include <string.h>
#include <stdlib.h>

static inline size_t oh_nchildren(struct object_handle_t *oh)
        { return oh->nchildren; }

/* **********************************************************************
 *                              API functions
 ***********************************************************************/

static void
object_handle_reset(void *h)
{
        struct object_handle_t *oh = h;
        if (oh->priv) {
                if (oh->priv_cleanup)
                        oh->priv_cleanup(oh, oh->priv);
                else
                        free(oh->priv);
        }
        hashtable_destroy(&oh->dict);
}

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
        bug_on(o->magic != TYPE_EMPTY);
        o->magic = TYPE_DICT;

        o->o = type_handle_new(sizeof(*o->o), object_handle_reset);
        hashtable_init(&o->o->dict, ptr_hash,
                       ptr_key_match, var_bucket_delete);
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
        bug_on(o->magic != TYPE_DICT);
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
        bug_on(o->magic != TYPE_DICT);
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
        return NULL;
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
        to->o = from->o;
        TYPE_HANDLE_INCR_REF(to->o);
        to->magic = TYPE_DICT;
}

static bool
object_cmpz(struct var_t *obj)
{
        return false;
}

static void
object_reset(struct var_t *o)
{
        bug_on(o->magic != TYPE_DICT);
        TYPE_HANDLE_DECR_REF(o->o);
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
        string_init(argv[1], NULL);

        if (!func)
                syntax("Expected: function");
        bug_on(self->magic != TYPE_DICT);
        htbl = &self->o->dict;

        for (idx = 0, res = hashtable_iterate(htbl, &key, &val, &idx);
             res == 0; res = hashtable_iterate(htbl, &key, &val, &idx)) {
                var_reset(argv[0]);
                qop_mov(argv[0], (struct var_t *)val);
                string_assign_cstring(argv[1], (char *)key);
                /*
                 * XXX REVISIT: should ``this'' in a foreach callback
                 * be the owner of the foreach method (us)?  Or should it
                 * be the object owning the calling function?  This is a
                 * philosophical conundrum, not a bug.
                 */
                vm_reenter(func, NULL, 2, argv);
        }
        VAR_DECR_REF(argv[0]);
        VAR_DECR_REF(argv[1]);
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
                bug_on(v->magic != TYPE_DICT);
        }
        switch (v->magic) {
        case TYPE_DICT:
                i = oh_nchildren(v->o);
                break;
        case TYPE_STRING:
                i = string_length(v);
                break;
        default:
                i = 1;
        }
        integer_init(ret, i);
}

static void
object_hasattr(struct var_t *ret)
{
        struct var_t *self = get_this();
        struct var_t *name = frame_get_arg(0);
        struct var_t *child = NULL;
        char *s;

        bug_on(self->magic != TYPE_DICT);

        if (!name || name->magic != TYPE_STRING)
                syntax("hasattr expected arg: string");

        if ((s = string_get_cstring(name)) != NULL)
                child = object_child(self, s);

        integer_init(ret, (int)(child != NULL));
}

/* "obj.setattr('name', val)" is an alternative to "obj.name = val" */
static void
object_setattr(struct var_t *ret)
{
        struct var_t *self = get_this();
        struct var_t *name = frame_get_arg(0);
        struct var_t *value = frame_get_arg(1);
        struct var_t *attr;
        char *s;

        bug_on(self->magic != TYPE_DICT);
        arg_type_check(name, TYPE_STRING);

        if (!value)
                syntax("setattr expected: value");

        s = string_get_cstring(name);
        if (!s)
                syntax("setattr: name may not be empty");

        attr = object_child(self, s);
        if (attr) {
                /* could throw a type-mismatch error */
                qop_mov(attr, value);
        } else {
                object_add_child(self, value, literal_put(s));
        }
}

/*
 *      let x = obj.getattr('name')"
 *
 * is a faster alternative to:
 *
 *      let x;
 *      if (obj.hasattr('name'))
 *              x = obj.name;
 *
 * The difference is that in the case of "x = obj.name", an error
 * will be thrown if 'name' does not exist, but in the case of
 * "x = obj.getattr('name')", x will be set to the empty variable
 * if 'name' does not exist.
 */
static void
object_getattr(struct var_t *ret)
{
        struct var_t *self = get_this();
        struct var_t *name = frame_get_arg(0);
        struct var_t *attr;
        char *s;

        bug_on(self->magic != TYPE_DICT);
        arg_type_check(name, TYPE_STRING);

        s = string_get_cstring(name);
        if (!s)
                syntax("setattr: name may not be empty");

        attr = object_child(self, s);
        if (attr)
                qop_mov(ret, attr);
}

static const struct type_inittbl_t object_methods[] = {
        V_INITTBL("len",       object_len,       0, 0),
        V_INITTBL("foreach",   object_foreach,   1, 1),
        V_INITTBL("hasattr",   object_hasattr,   1, 1),
        V_INITTBL("setattr",   object_setattr,   2, 2),
        V_INITTBL("getattr",   object_getattr,   1, 1),
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
        var_config_type(TYPE_DICT, "dictionary",
                        &object_primitives, object_methods);
}

