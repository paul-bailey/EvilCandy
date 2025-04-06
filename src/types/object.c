/*
 * Definitions for the dictionary (ie. associative array) class of objects.
 *
 * JavaScript calls these "objects".  Python calls them "dictionaries".
 * I should have done like Python, since *all* these classes are
 * tecnhically objects, and the documentation calls them dictionaries,
 * too.  Too late...
 */
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
        if (!s)
                return NULL;
        bug_on(o->magic != TYPE_DICT);
        bug_on(!o->o);

        return hashtable_get(&o->o->dict, s);
}

/**
 * object_add_child - Append a child to an object
 * @parent: object to append a child to
 * @child: child to append to @parent
 * @name: Name of the child.
 */
int
object_add_child(struct var_t *parent, struct var_t *child, char *name)
{
        bug_on(parent->magic != TYPE_DICT);
        if (parent->o->lock) {
                syntax_noexit("Dictionary add/remove locked");
                return -1;
        }
        if (hashtable_put(&parent->o->dict, name, child) < 0) {
                syntax_noexit("Object already has element named %s", name);
                return -1;
        }
        VAR_INCR_REF(child);
        parent->o->nchildren++;
        return 0;
}

/* if @child is known to be a direct child of @parent */
static void
object_remove_child_(struct var_t *parent, struct var_t *child)
{
        VAR_DECR_REF(child);
        parent->o->nchildren--;
}

/**
 * object_remove_child_l - Like object_remove_child, but
 *                      @name is either NULL or known to be a
 *                      possible return value of literal()
 */
int
object_remove_child_l(struct var_t *parent, const char *name)
{
        struct var_t *child;
        if (!name)
                return 0;
        if (parent->o->lock) {
                syntax_noexit("Dictionary add/remove locked");
                return -1;
        }
        child = hashtable_remove(&parent->o->dict, name);
        /*
         * XXX REVISIT: If !child, should I throw a doesn't-exist error,
         * or should I silently ignore it like I'm doing now?
         */
        if (child)
                object_remove_child_(parent, child);
        return 0;
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

static int
object_cmp(struct var_t *a, struct var_t *b)
{
        if (b->magic == TYPE_DICT && b->o == a->o)
                return 0;
        return 1;
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
int
object_foreach(struct var_t *ret)
{
        struct var_t *self;
        struct var_t *func;
        unsigned int idx;
        struct hashtable_t *htbl;
        void *key, *val;
        int res, lock;
        int status = 0;
        struct var_t *argv[2];

        self = get_this();
        func = frame_get_arg(0);
        if (!func) {
                syntax_noexit("Expected: function");
                return -1;
        }
        argv[0] = var_new(); /* attribute */
        argv[1] = var_new(); /* name of attribute */
        string_init(argv[1], NULL);

        bug_on(self->magic != TYPE_DICT);
        htbl = &self->o->dict;

        lock = self->o->lock;
        self->o->lock = 1;
        for (idx = 0, res = hashtable_iterate(htbl, &key, &val, &idx);
             res == 0; res = hashtable_iterate(htbl, &key, &val, &idx)) {
                var_reset(argv[0]);
                if (qop_mov(argv[0], (struct var_t *)val) < 0) {
                        status = -1;
                        break;
                }
                string_assign_cstring(argv[1], (char *)key);
                /*
                 * XXX REVISIT: should ``this'' in a foreach callback
                 * be the owner of the foreach method (us)?  Or should it
                 * be the object owning the calling function?  This is a
                 * philosophical conundrum, not a bug.
                 */
                status = vm_reenter(func, NULL, 2, argv);
                if (status)
                        break;
        }
        self->o->lock = lock;

        VAR_DECR_REF(argv[0]);
        VAR_DECR_REF(argv[1]);
        return status;
}


/*
 * len()  (no args)
 * returns number of elements in object
 */
static int
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
        return 0;
}

static int
object_hasattr(struct var_t *ret)
{
        struct var_t *self = get_this();
        struct var_t *name = frame_get_arg(0);
        struct var_t *child = NULL;
        char *s;

        bug_on(self->magic != TYPE_DICT);

        if (!name || name->magic != TYPE_STRING) {
                syntax_noexit("hasattr expected arg: string");
                return -1;
        }

        if ((s = string_get_cstring(name)) != NULL)
                child = object_child(self, s);

        integer_init(ret, (int)(child != NULL));
        return 0;
}

/* "obj.setattr('name', val)" is an alternative to "obj.name = val" */
static int
object_setattr(struct var_t *ret)
{
        struct var_t *self = get_this();
        struct var_t *name = frame_get_arg(0);
        struct var_t *value = frame_get_arg(1);
        struct var_t *attr;
        char *s;

        bug_on(self->magic != TYPE_DICT);
        arg_type_check(name, TYPE_STRING);

        if (!value) {
                syntax_noexit("setattr expected: value");
                return -1;
        }

        s = string_get_cstring(name);
        if (!s) {
                syntax_noexit("setattr: name may not be empty");
                return -1;
        }

        attr = object_child(self, s);
        if (attr) {
                /* could throw a type-mismatch error */
                if (qop_mov(attr, value) < 0)
                        return -1;
        } else {
                if (object_add_child(self, value, literal_put(s)) != 0)
                        return -1;
        }
        return 0;
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
static int
object_getattr(struct var_t *ret)
{
        struct var_t *self = get_this();
        struct var_t *name = frame_get_arg(0);
        struct var_t *attr;
        char *s;

        bug_on(self->magic != TYPE_DICT);
        arg_type_check(name, TYPE_STRING);

        s = string_get_cstring(name);
        if (!s) {
                syntax_noexit("setattr: name may not be empty");
                return -1;
        }

        attr = object_child(self, s);
        if (attr) {
                if (qop_mov(ret, attr) < 0)
                        return -1;
        }
        return 0;
}

static int
object_delattr(struct var_t *ret)
{
        struct var_t *self = get_this();
        struct var_t *name = vm_get_arg(0);
        char *s;

        bug_on(self->magic != TYPE_DICT);
        arg_type_check(name, TYPE_STRING);

        s = string_get_cstring(name);
        return object_remove_child(self, s);
}

static const struct type_inittbl_t object_methods[] = {
        V_INITTBL("len",       object_len,       0, 0),
        V_INITTBL("foreach",   object_foreach,   1, 1),
        V_INITTBL("hasattr",   object_hasattr,   1, 1),
        V_INITTBL("setattr",   object_setattr,   2, 2),
        V_INITTBL("getattr",   object_getattr,   1, 1),
        V_INITTBL("delattr",   object_delattr,   1, 1),
        TBLEND,
};

/*
 * FIXME: Would be nice if we could do like Python and let objects have
 * user-defined operator callbacks
 */
static const struct operator_methods_t object_primitives = {
        .cmp            = object_cmp,
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

