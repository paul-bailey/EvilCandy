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

struct var_t *
objectvar_new(void)
{
        struct var_t *o = var_new();
        o->magic = TYPE_DICT;

        o->o = type_handle_new(sizeof(*o->o), object_handle_reset);
        hashtable_init(&o->o->dict, fnv_hash,
                       str_key_match, var_bucket_delete);
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
 * object_getattr - Get object attribute
 * @o:  Me
 * @s:  Key
 *
 * Return: child matching @key, or NULL if not found.
 *      Calling code must decide whether NULL is an error or not
 */
struct var_t *
object_getattr(struct var_t *o, const char *s)
{
        if (!s)
                return NULL;
        bug_on(o->magic != TYPE_DICT);
        bug_on(!o->o);

        return hashtable_get(&o->o->dict, s);
}

/**
 * object_addattr - Append a child to an object
 * @parent: object to append a child to
 * @child: child to append to @parent
 * @name: Name of the child.
 *
 * Like object_setattr, except that it throws an error if the atttribute
 * already exists, and it takes into account that most callers have a
 * C string, not a TYPE_STRING var_t.
 */
enum result_t
object_addattr(struct var_t *parent,
                 struct var_t *child, const char *name)
{
        bug_on(parent->magic != TYPE_DICT);
        if (parent->o->lock) {
                err_locked();
                return RES_ERROR;
        }

        if (hashtable_put(&parent->o->dict, literal_put(name), child) < 0) {
                err_setstr(RuntimeError,
                           "Object already has element named %s", name);
                return RES_ERROR;
        }
        VAR_INCR_REF(child);
        parent->o->nchildren++;
        return RES_OK;
}

/**
 * object_delattr - Like object_delattr, but
 *                      @name is either NULL or known to be a
 *                      possible return value of literal()
 */
enum result_t
object_delattr(struct var_t *parent, const char *name)
{
        struct var_t *child;
        if (!name)
                return RES_OK;
        if (parent->o->lock) {
                err_locked();
                return RES_ERROR;
        }
        child = hashtable_remove(&parent->o->dict, name);
        /*
         * XXX REVISIT: If !child, should I throw a doesn't-exist error,
         * or should I silently ignore it like I'm doing now?
         */
        if (child) {
                VAR_DECR_REF(child);
                parent->o->nchildren--;
        }
        return RES_OK;
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
struct var_t *
do_object_foreach(struct vmframe_t *fr)
{
        struct var_t *self;
        struct var_t *func;
        unsigned int idx;
        struct hashtable_t *htbl;
        void *key, *val;
        int res, lock;
        int status = RES_OK;
        struct var_t *argv[2];

        self = get_this(fr);
        func = frame_get_arg(fr, 0);
        if (!func) {
                err_argtype("function");
                return ErrorVar;
        }
        argv[0] = var_new(); /* attribute */
        argv[1] = stringvar_new(NULL); /* name of attribute */

        bug_on(self->magic != TYPE_DICT);
        htbl = &self->o->dict;

        lock = self->o->lock;
        self->o->lock = 1;
        for (idx = 0, res = hashtable_iterate(htbl, &key, &val, &idx);
             res == 0; res = hashtable_iterate(htbl, &key, &val, &idx)) {
                struct var_t *retval;
                var_reset(argv[0]);
                if (qop_mov(argv[0], (struct var_t *)val) < 0) {
                        status = RES_ERROR;
                        break;
                }
                string_assign_cstring(argv[1], (char *)key);
                /*
                 * XXX REVISIT: should ``this'' in a foreach callback
                 * be the owner of the foreach method (us)?  Or should it
                 * be the object owning the calling function?  This is a
                 * philosophical conundrum, not a bug.
                 */
                retval = vm_reenter(fr, func, NULL, 2, argv);
                if (retval == ErrorVar) {
                        status = RES_ERROR;
                        break;
                }
                /* foreach throws away retval */
                if (retval)
                        VAR_DECR_REF(retval);
        }
        self->o->lock = lock;

        VAR_DECR_REF(argv[0]);
        VAR_DECR_REF(argv[1]);
        return status == RES_OK ? NULL: ErrorVar;
}


/*
 * len()  (no args)
 * returns number of elements in object
 */
static struct var_t *
do_object_len(struct vmframe_t *fr)
{
        struct var_t *v;
        int i = 0;

        v = frame_get_arg(fr, 0);
        if (!v) {
                v = get_this(fr);
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

        return intvar_new(i);
}

static struct var_t *
do_object_hasattr(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        struct var_t *name = frame_get_arg(fr, 0);
        struct var_t *child = NULL;
        char *s;

        bug_on(self->magic != TYPE_DICT);

        if (!name || name->magic != TYPE_STRING) {
                err_argtype("string");
                return ErrorVar;
        }

        if ((s = string_get_cstring(name)) != NULL)
                child = object_getattr(self, s);

        return intvar_new((int)(child != NULL));
}

/**
 * object_setattr - Insert an attribute to dictionary if it doesn't exist,
 *                  or change the existing attribute if it does.
 * @self:       Dictionary object
 * @name:       Name of attribute key
 * @attr:       Value to set
 *
 * This does not touch the type's built-in-method attributes.
 *
 * Return: res_ok or RES_ERROR.
 *
 * XXX REVISIT: When replacing old attribute, this performs a MOV operation.
 *       Good idea or bad idea?
 *       Consider the code
 *              let A = B.SomeAttr;
 *              B.SomeAttr = SomethingElseNow;
 *      What happens with handle A after changing B.SomeAttr?
 *      Isn't it better to VAR_DECR_REF(old B.SomeAttr), and let a's
 *      handle be independent of b when this happens?
 */
enum result_t
object_setattr(struct var_t *dict, struct var_t *name, struct var_t *attr)
{
        char *namestr;
        struct var_t *child;

        bug_on(dict->magic != TYPE_DICT);

        switch (name->magic) {
        case TYPE_STRPTR:
                namestr = name->strptr;
                break;
        case TYPE_STRING:
                namestr = string_get_cstring(name);
                break;
        default:
                err_argtype("name");
                return RES_ERROR;
        }

        /*
         * if exists, change it. Otherwise add it.
         * XXX: POLICY DECISION: qop_mov as below, or delte old
         * and replace it with new?
         */
        child = object_getattr(dict, namestr);
        if (child) {
                /* already exists */
                if (!qop_mov(child, attr))
                        return RES_ERROR;
        } else {
                if (object_addattr(dict, attr, namestr) != 0)
                        return RES_ERROR;
        }
        return RES_OK;
}

/* "obj.setattr('name', val)" is an alternative to "obj.name = val" */
static struct var_t *
do_object_setattr(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        struct var_t *name = frame_get_arg(fr, 0);
        struct var_t *value = frame_get_arg(fr, 1);

        bug_on(self->magic != TYPE_DICT);

        if (!name) {
                err_argtype("name");
                return ErrorVar;
        }
        if (!value) {
                err_argtype("value");
                return ErrorVar;
        }
        if (object_setattr(self, name, value) != RES_OK)
                return ErrorVar;

        return NULL;
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
static struct var_t *
do_object_getattr(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        struct var_t *name = frame_get_arg(fr, 0);
        struct var_t *attr, *ret;
        char *s;

        bug_on(self->magic != TYPE_DICT);
        if (arg_type_check(name, TYPE_STRING) != 0)
                return ErrorVar;

        s = string_get_cstring(name);
        if (!s) {
                err_setstr(RuntimeError, "getattr: name may not be empty");
                return ErrorVar;
        }

        /*
         * FIXME: better to just VAR_INCR_REF, and then have object_setattr
         * replace this pointer in the table with new attr, thereby
         * not clobbering this while it's being used by the caller.
         */
        ret = var_new();
        if ((attr = object_getattr(self, s)) != NULL) {
                if (qop_mov(ret, attr) < 0) {
                        VAR_DECR_REF(ret);
                        return ErrorVar;
                }
        }
        /* if no child, return empty var */
        return ret;
}

static struct var_t *
do_object_delattr(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        struct var_t *name = vm_get_arg(fr, 0);
        char *s;

        bug_on(self->magic != TYPE_DICT);
        if (arg_type_check(name, TYPE_STRING) != 0)
                return ErrorVar;

        s = string_get_cstring(name);
        if (object_delattr(self, s) != RES_OK)
                return ErrorVar;
        return NULL;
}

static const struct type_inittbl_t object_methods[] = {
        V_INITTBL("len",       do_object_len,       0, 0),
        V_INITTBL("foreach",   do_object_foreach,   1, 1),
        V_INITTBL("hasattr",   do_object_hasattr,   1, 1),
        V_INITTBL("setattr",   do_object_setattr,   2, 2),
        V_INITTBL("getattr",   do_object_getattr,   1, 1),
        V_INITTBL("delattr",   do_object_delattr,   1, 1),
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

