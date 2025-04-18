/*
 * Definitions for the dictionary (ie. associative array) class of objects.
 *
 * JavaScript calls these "objects".  Python calls them "dictionaries".
 * I should have done like Python, since *all* these classes are
 * tecnhically objects, and the documentation calls them dictionaries,
 * too.  Too late...
 */
#include <evilcandy.h>

/**
 * struct dictvar_t - Descriptor for an object handle
 * @priv:       Internal private data, used by some built-in object types
 * @priv_cleanup: Way to clean up @priv if destroying this object handle.
 *              If this is NULL and @priv is not NULL, @priv will be
 *              simply freed.  Used by C accelerator modules, not scripts.
 * @dict:       Hash table of attributes
 * @lock:       Prevent SETATTR, GETATTR during an iterable cycle, such as
 *              foreach.
 */
struct dictvar_t {
        struct seqvar_t base;
        void *priv;
        void (*priv_cleanup)(struct var_t *, void *);
        struct hashtable_t dict;
        unsigned int lock;
};

#define V2D(v)          ((struct dictvar_t *)(v))
#define V2SQ(v)         ((struct seqvar_t *)(v))
#define OBJ_SIZE(v)     seqvar_size(v)


/* **********************************************************************
 *                              API functions
 ***********************************************************************/

/**
 * object_keys - Get an alphabetically sorted list of all the keys
 *               currently in the dictionary.
 */
struct var_t *
object_keys(struct var_t *obj)
{
        struct var_t *keys;
        struct hashtable_t *d;
        void *k, *v; /* v is unused dummy */
        int res;
        unsigned int i;
        int array_i;

        bug_on(!isvar_object(obj));
        d = &V2D(obj)->dict;
        keys = arrayvar_new(OBJ_SIZE(obj));

        array_i = 0;
        for (i = 0, res = hashtable_iterate(d, &k, &v, &i);
             res == 0; res = hashtable_iterate(d, &k, &v, &i)) {
                struct var_t *ks = stringvar_new((char *)k);
                array_setitem(keys, array_i, ks);
                VAR_DECR_REF(ks);
                array_i++;
        }
        var_sort(keys);
        return keys;
}

/**
 * objectvar_new - Create a new empty dictionary
 */
struct var_t *
objectvar_new(void)
{
        struct var_t *o = var_new(&ObjectType);
        V2D(o)->priv = NULL;
        V2D(o)->priv_cleanup = NULL;
        seqvar_set_size(o, 0);
        hashtable_init(&V2D(o)->dict, fnv_hash,
                       str_key_match, var_bucket_delete);
        return o;
}

/*
 * XXX REVISIT: Consider removing object_set/get_priv.
 * The private data is unused for user-defined dictionaries, and there's
 * no reason built-in modules need to be dictionaries at all, as opposed
 * to their own types.  True for IO module, which so far is the only
 * module which uses this.
 */

/**
 * object_set_priv - Set an object's private data
 * @o: Object
 * @priv: Private data to set
 * @cleanup: Cleanup method to clean up private data at garbage
 *           collection time, or NULL to let it be simply free'd
 */
void
object_set_priv(struct var_t *o, void *priv,
                void (*cleanup)(struct var_t *, void *))
{
        bug_on(!isvar_object(o));
        V2D(o)->priv = priv;
        V2D(o)->priv_cleanup = cleanup;
}

/**
 * object_get_priv - Get the object's private data,
 *                      or NULL if none exists.
 */
void *
object_get_priv(struct var_t *o)
{
        return V2D(o)->priv;
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
        struct var_t *ret;
        if (!s)
                return NULL;
        bug_on(!isvar_object(o));

        ret = hashtable_get(&V2D(o)->dict, s);
        if (ret)
                VAR_INCR_REF(ret);
        return ret;
}

/**
 * object_setattr - Insert an attribute to dictionary if it doesn't exist,
 *                  or change the existing attribute if it does.
 * @self:       Dictionary object
 * @name:       Name of attribute key
 * @attr:       Value to set.  NULL means 'delete the entry'
 *
 * This does not touch the type's built-in-method attributes.
 *
 * Return: res_ok or RES_ERROR.
 */
enum result_t
object_setattr(struct var_t *dict, const char *key, struct var_t *attr)
{
        struct var_t *child;
        struct dictvar_t *d = V2D(dict);

        bug_on(!isvar_object(dict));

        /* @child is either the former entry replaced by @attr or NULL */
        if (attr) {
                /*
                 * XXX REVISIT: literal_put immortalizes a key in an object that
                 * could later be destroyed.  More often than not @name is
                 * already immortal (it was most likely derived in some way from
                 * a literal in the source code), so this does nothing.  However,
                 * @name could have been constructed from something that the
                 * source never expresses literally.  Consider something weird
                 * like...
                 *
                 *      my_obj = (function(a, key_prefix) {
                 *              let o = {};
                 *              a.foreach(function(e, idx) {
                 *                      o[key_prefix + idx.tostr()] = e;
                 *              });
                 *              return o;
                 *      })(my_arr, 'my_key_');
                 *
                 * Here, 'my_key_' is a hard-coded literal, but 'my_key_0' is not.
                 * For a program with a long lifecycle, this could result in the
                 * build-up of a non-trivial amount of zombified strings.
                 */
                key = literal_put(key);

                child = hashtable_put_or_swap(&d->dict, (void *)key, attr);
                if (child) {
                        VAR_DECR_REF(child);
                } else {
                        seqvar_set_size(dict, seqvar_size(dict) + 1);
                }
                VAR_INCR_REF(attr);
        } else {
                /* XXX REVISIT: If !child, maybe throw error and print msg */
                child = hashtable_remove(&d->dict, key);
                if (child) {
                        VAR_DECR_REF(child);
                        seqvar_set_size(dict, seqvar_size(dict) - 1);
                }
        }
        return RES_OK;
}

static int
object_hasattr(struct var_t *o, const char *key)
{
        struct var_t *child;

        bug_on(!isvar_object(o));

        child = hashtable_get(&V2D(o)->dict, key);
        return child != NULL;
}

/*
 * early-initialization function called from moduleinit_builtin.
 * Hacky way to not require loading an EvilCandy script every
 * time that says something like
 *      let print = __gbl__._builtins.print;
 *      let len   = __gbl__._builtins.len;
 *      ...
 *  ...and so on.
 */
void
object_add_to_globals(struct var_t *obj)
{
        bug_on(!obj);

        unsigned int i;
        int res;
        void *k, *v;
        struct hashtable_t *h = &V2D(obj)->dict;
        for (i = 0, res = hashtable_iterate(h, &k, &v, &i);
             res == 0; res = hashtable_iterate(h, &k, &v, &i)) {
                vm_add_global((char *)k, (struct var_t *)v);
        }
}

/* **********************************************************************
 *              Built-in Operator Callbacks
 ***********************************************************************/

static int
object_cmp(struct var_t *a, struct var_t *b)
{
        if (isvar_object(b))
                return 0;
        /* FIXME: need to recurse here */
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
        struct dictvar_t *oh;

        bug_on(!isvar_object(o));
        oh = V2D(o);
        if (oh->priv) {
                if (oh->priv_cleanup)
                        oh->priv_cleanup(o, oh->priv);
                else
                        efree(oh->priv);
        }
        hashtable_destroy(&oh->dict);
}

static struct var_t *
object_str(struct var_t *o)
{
        struct dictvar_t *oh;
        struct buffer_t b;
        void *k, *v;
        unsigned int i;
        int res, count;
        struct hashtable_t *h;
        struct var_t *ret;

        bug_on(!isvar_object(o));

        oh = V2D(o);
        h = &oh->dict;
        buffer_init(&b);
        buffer_putc(&b, '{');
        for (i = 0, count = 0, res = hashtable_iterate(h, &k, &v, &i);
             res == 0; res = hashtable_iterate(h, &k, &v, &i), count++) {
                struct var_t *item;
                if (count > 0)
                        buffer_puts(&b, ", ");

                buffer_putc(&b, '\'');
                buffer_puts(&b, (char *)k);
                buffer_puts(&b, "': ");

                item = var_str((struct var_t *)v);
                buffer_puts(&b, string_get_cstring(item));
                VAR_DECR_REF(item);
        }
        buffer_putc(&b, '}');
        ret = stringvar_new(b.s);
        buffer_free(&b);
        return ret;
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
        struct var_t *keys, *func, *self, *priv;
        int i, len, status;

        self = get_this(fr);
        bug_on(!isvar_object(self));
        func = frame_get_arg(fr, 0);
        if (!func) {
                err_argtype("function");
                return ErrorVar;
        }
        priv = frame_get_arg(fr, 1);
        if (!priv)
                priv = NullVar;

        keys = object_keys(self);
        len = var_len(keys);
        bug_on(var_len < 0);

        status = RES_OK;
        for (i = 0; i < len; i++) {
                struct var_t *key, *val, *argv[3], *cbret;

                key = array_getitem(keys, i);
                bug_on(!key || key == ErrorVar);
                val = object_getattr(self, string_get_cstring(key));
                if (!val) /* user shenanigans in foreach loop */
                        continue;

                argv[0] = val;
                argv[1] = key;
                argv[2] = priv;
                cbret = vm_exec_func(fr, func, NULL, 3, argv);

                VAR_DECR_REF(key);
                VAR_DECR_REF(val);

                if (cbret == ErrorVar) {
                        status = RES_ERROR;
                        break;
                }
                if (cbret)
                        VAR_DECR_REF(cbret);
        }
        VAR_DECR_REF(keys);
        return status == RES_OK ? NULL : ErrorVar;
}


/*
 * len()  (no args)
 * returns number of elements in object
 */
static struct var_t *
do_object_len(struct vmframe_t *fr)
{
        struct var_t *v;
        int i;

        v = vm_get_this(fr);
        bug_on(!v || !isvar_object(v));
        i = OBJ_SIZE(v);
        return intvar_new(i);
}

static struct var_t *
do_object_hasattr(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        struct var_t *name = frame_get_arg(fr, 0);
        struct var_t *child = NULL;
        char *s;

        bug_on(!isvar_object(self));

        if (!name || !isvar_string(name)) {
                err_argtype("string");
                return ErrorVar;
        }

        if ((s = string_get_cstring(name)) != NULL)
                child = object_getattr(self, s);

        /* TODO: if child == NULL, check built-in methods */

        return intvar_new((int)(child != NULL));
}

/* "obj.setattr('name', val)" is an alternative to "obj.name = val" */
static struct var_t *
do_object_setattr(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        struct var_t *name = frame_get_arg(fr, 0);
        struct var_t *value = frame_get_arg(fr, 1);

        bug_on(!isvar_object(self));

        if (!name || !isvar_string(name)) {
                err_argtype("name");
                return ErrorVar;
        }
        if (!value) {
                err_argtype("value");
                return ErrorVar;
        }
        if (object_setattr(self, string_get_cstring(name), value) != RES_OK)
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
        struct var_t *ret;
        char *s;

        bug_on(!isvar_object(self));
        if (arg_type_check(name, &StringType) != 0)
                return ErrorVar;

        s = string_get_cstring(name);
        if (!s) {
                err_setstr(RuntimeError, "getattr: name may not be empty");
                return ErrorVar;
        }

        ret = object_getattr(self, s);
        /* XXX: If NULL, check built-in methods */
        /* XXX: VAR_INCR_REF? Who's taking this? */
        if (!ret)
                ret = ErrorVar;
        return ret;
}

static struct var_t *
do_object_delattr(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        struct var_t *name = vm_get_arg(fr, 0);
        char *s;

        bug_on(!isvar_object(self));
        if (arg_type_check(name, &StringType) != 0)
                return ErrorVar;

        s = string_get_cstring(name);
        if (object_setattr(self, s, NULL) != RES_OK)
                return ErrorVar;
        return NULL;
}

static struct var_t *
do_object_keys(struct vmframe_t *fr)
{
        return object_keys(get_this(fr));
}

/*
 * .copy()      Duplicate myself
 *
 * This is not recursive.  If any of obj2's items are lists
 * or dictionaries, then they will still be copied by-reference.
 */
static struct var_t *
do_object_copy(struct vmframe_t *fr)
{
        struct var_t *self = get_this(fr);
        void *k, *v;
        int res;
        unsigned int i;
        struct hashtable_t *d;
        struct var_t *ret = objectvar_new();

        bug_on(!isvar_object(self));

        d = &V2D(self)->dict;
        for (i = 0, res = hashtable_iterate(d, &k, &v, &i);
             res == 0; res = hashtable_iterate(d, &k, &v, &i)) {
                struct var_t *vv = (struct var_t *)v;
                /*
                 * object_setattr will produce another reference
                 * to @vv
                 */
                if (object_setattr(ret, (char *)k, vv) != RES_OK) {
                        VAR_DECR_REF(ret);
                        return ErrorVar;
                }
                /* object_addattr already incremented ref */
        }
        return ret;
}

static const struct type_inittbl_t object_cb_methods[] = {
        V_INITTBL("len",       do_object_len,       0, 0),
        V_INITTBL("foreach",   do_object_foreach,   1, 1),
        V_INITTBL("hasattr",   do_object_hasattr,   1, 1),
        V_INITTBL("setattr",   do_object_setattr,   2, 2),
        V_INITTBL("getattr",   do_object_getattr,   1, 1),
        V_INITTBL("delattr",   do_object_delattr,   1, 1),
        V_INITTBL("keys",      do_object_keys,      0, 0),
        V_INITTBL("copy",      do_object_copy,      0, 0),
        TBLEND,
};

static const struct map_methods_t object_map_methods = {
        .getitem = object_getattr,
        .setitem = object_setattr,
        .hasitem = object_hasattr,
};

struct type_t ObjectType = {
        .name = "dictionary",
        .opm    = NULL,
        .cbm    = object_cb_methods,
        .mpm    = &object_map_methods,
        .sqm    = NULL,
        .size   = sizeof(struct dictvar_t),
        .str    = object_str,
        .cmp    = object_cmp,
        .cmpz   = object_cmpz,
        .reset  = object_reset,
};

