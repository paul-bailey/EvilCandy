/*
 * Definitions for the dictionary (ie. associative array) class of objects.
 *
 * JavaScript calls these "objects".  Python calls them "dictionaries".
 * Calling one kind of an object an 'object' to distinguish it from another
 * kind of object is kind of janky, so I'm going with Python on this one.
 */
#include <evilcandy.h>
#include <stringtype.h>

struct bucket_t {
        /*
         * TODO: move b_hash to string data type.  Strings are immutable,
         * so their hash should only need to be calculated once during
         * their lifetime.  Do this when we start storing string data types
         * instead of C strings for the keys
         */
        hash_t b_hash;
        Object *b_key;
        Object *b_data;
};

/**
 * struct dictvar_t - Descriptor for an object handle
 * @d_size:             Array size of d_bucket, always a power of 2
 * @d_used:             Active entries in hash table
 * @d_count:            Active + removed ('dead') entries in hash table
 * @d_grow_size:        Next threshold for expanding
 * @d_shrink_size:      Next threshold for shrinking
 * @d_bucket:           Array of bucket entries
 */
struct dictvar_t {
        struct seqvar_t base;
        size_t d_size;
        size_t d_used;
        size_t d_count;
        size_t d_grow_size;
        size_t d_shrink_size;
        struct bucket_t **d_bucket;
};

#define V2D(v)          ((struct dictvar_t *)(v))
#define V2SQ(v)         ((struct seqvar_t *)(v))
#define OBJ_SIZE(v)     seqvar_size(v)


/* **********************************************************************
 *                      Hash table helpers
 ***********************************************************************/

#define GROW_SIZE(x)    (((x) * 2) / 3)
#define BUCKET_DEAD     ((void *)-1)

/*
 * this initial size is small enough to not be a burden
 * but large enough that for 90% use-cases, no resizing
 * need occur.
 */
enum { INIT_SIZE = 16 };

static bool
str_key_match(Object *key1, Object *key2)
{
        bug_on(!isvar_string(key1) || !isvar_string(key2));

        /* fast-path "==", since these still sometimes are literal()'d */
        return key1 == key2
               || (string_hash(key1) == string_hash(key2)
                   && !strcmp(string_get_cstring(key1),
                              string_get_cstring(key2)));
}

static inline struct bucket_t *
bucket_alloc(void)
{
        return emalloc(sizeof(struct bucket_t));
}

static inline void
bucket_free(struct bucket_t *b)
{
        efree(b);
}

static inline int
bucketi(struct dictvar_t *dict, hash_t hash)
{
        return hash & (dict->d_size - 1);
}

static struct bucket_t *
seek_helper(struct dictvar_t *dict, Object *key,
                        hash_t hash, unsigned int *idx)
{
        unsigned int i = bucketi(dict, hash);
        struct bucket_t *b;
        unsigned long perturb = hash;
        while ((b = dict->d_bucket[i]) != NULL) {
                if (b != BUCKET_DEAD && str_key_match(b->b_key, key))
                        break;
                /*
                 * Collision or dead entry.
                 *
                 * Way to cope with a power-of-2-sized hash table, esp.
                 * an open-address one.  Idea & algo taken from Python.
                 * See cpython source code, "Object/dictobject.c" at
                 *
                 *      https://github.com/python/cpython.git
                 *
                 * Don't just seek the next adjacent empty slot.  For any
                 * non-trivial alpha, this quickly degenerates into a
                 * linear array search.  "Perturb it" instead.  This will
                 * not spinlock because:
                 *
                 * 1. There is always at least one blank entry
                 *
                 * 2. We will eventually hit an empty slot, even in the
                 *    worst-case scenario, because after floor(64/5)=12
                 *    iterations, perturb will become zero, and
                 *    (i*5+1) % SIZE will eventually hit every index at
                 *    least once when size is a power of 2.  (I don't
                 *    know the proof, but every which way I've tested it,
                 *    it turns out to be true.  Also, very smart people
                 *    claim to have proven it and they're smart.)
                 */
                perturb >>= 5;
                i = bucketi(dict, i * 5 + perturb + 1);
        }
        *idx = i;
        return b;
}

static void
transfer_table(struct dictvar_t *dict, size_t old_size)
{
        int i, j, count;
        struct bucket_t **old, **new;
        old = dict->d_bucket;
        dict->d_bucket = new = ecalloc(sizeof(void *) * dict->d_size);
        count = 0;
        for (i = 0; i < old_size; i++) {
                unsigned long perturb;
                struct bucket_t *b = old[i];
                if (!b || b == BUCKET_DEAD)
                        continue;

                perturb = b->b_hash;
                j = bucketi(dict, b->b_hash);
                while (new[j]) {
                        perturb >>= 5;
                        j = bucketi(dict, j * 5 + perturb + 1);
                }
                new[j] = b;
                count++;
        }
        dict->d_count = dict->d_used = count;
        efree(old);
}

static void
refresh_grow_markers(struct dictvar_t *dict)
{
        /*
         * XXX REVISIT: "/3" is arbitrary division
         *
         * alpha=75%, "(x*3)>>2" is quicker, but its near the poor-
         * performance range for open-address tables. alpha=50%, "x>>1",
         * is a lot of wasted real-estate, probably resulting in lots of
         * cache misses, killing the advantage that open-address has over
         * chaining.  I'm assuming that amortization is reason enough not
         * to care about any of this.
         *
         * TODO 4/2025: not necessary: if size * 3 > growsize * 2, grow it.
         * If size * 2 < shrinksize * 3, shrink it.  Then each time we
         * resize we multiply/divide all values by 2.  The division is
         * no longer arbitrary.
         * Do that instead of this below.
         */
        dict->d_grow_size = (dict->d_size << 1) / 3;
        dict->d_shrink_size = dict->d_used <= INIT_SIZE
                            ? 0: dict->d_grow_size / 3;
}

static void
maybe_grow_table(struct dictvar_t *dict)
{
        size_t old_size = dict->d_size;
        while (dict->d_count > dict->d_grow_size) {
                /*
                 * size must always be a power of 2 or else the
                 * perturbation algo could spinlock.
                 */
                dict->d_size *= 2;
                refresh_grow_markers(dict);
        }

        if (dict->d_size != old_size)
                transfer_table(dict, old_size);
}

static void
maybe_shrink_table(struct dictvar_t *dict)
{
        size_t old_size = dict->d_size;
        while (dict->d_used < dict->d_shrink_size) {
                dict->d_size /= 2;
                refresh_grow_markers(dict);
        }

        if (dict->d_size < INIT_SIZE)
                dict->d_size = INIT_SIZE;

        if (dict->d_size != old_size)
                transfer_table(dict, old_size);
}

static inline void
insert_common(struct dictvar_t *dict, Object *key,
              Object *data, hash_t hash, unsigned int i)
{
        struct bucket_t *b = bucket_alloc();
        b->b_key = key;
        b->b_data = data;
        b->b_hash = hash;
        dict->d_bucket[i] = b;
        dict->d_count++;
        dict->d_used++;
        maybe_grow_table(dict);
}


/* **********************************************************************
 *                              API functions
 ***********************************************************************/

/**
 * dict_keys - Get an alphabetically sorted list of all the keys
 *               currently in the dictionary.
 */
Object *
dict_keys(Object *obj)
{
        Object *keys;
        struct dictvar_t *d;
        unsigned int i;
        int array_i;

        bug_on(!isvar_dict(obj));
        d = V2D(obj);
        keys = arrayvar_new(OBJ_SIZE(obj));

        array_i = 0;

        for (i = 0; i < d->d_size; i++) {
                struct bucket_t *b = d->d_bucket[i];
                Object *ks;
                if (b == NULL || b == BUCKET_DEAD)
                        continue;

                ks = b->b_key;
                array_setitem(keys, array_i, ks);
                array_i++;
        }

        var_sort(keys);
        return keys;
}

/**
 * dictvar_new - Create a new empty dictionary
 */
Object *
dictvar_new(void)
{
        Object *o = var_new(&DictType);
        struct dictvar_t *d = V2D(o);
        seqvar_set_size(o, 0);

        d->d_size = INIT_SIZE;
        d->d_used = 0;
        d->d_count = 0;
        refresh_grow_markers(d);
        d->d_bucket = ecalloc(sizeof(void *) * INIT_SIZE);
        return o;
}

/**
 * dict_getitem - Get object attribute
 * @o:  Me
 * @s:  Key
 *
 * Return: child matching @key, or NULL if not found.
 *      Calling code must decide whether NULL is an error or not
 */
Object *
dict_getitem(Object *o, Object *key)
{
        struct dictvar_t *d;
        unsigned int i;
        hash_t hash;
        struct bucket_t *b;

        d = V2D(o);
        bug_on(!isvar_dict(o));
        bug_on(!isvar_string(key));

        hash = string_update_hash(key);
        b = seek_helper(d, key, hash, &i);
        if (!b)
                return NULL;

        VAR_INCR_REF(b->b_data);
        return b->b_data;
}

/*
 * Sloppy slow way to get an entry with only a C string.
 * Don't use this if you can help it.  It forces a hash calculation
 * every time.
 */
Object *
dict_getitem_cstr(Object *o, const char *cstr_key)
{
        Object *key = stringvar_new(cstr_key);
        Object *res = dict_getitem(o, key);
        VAR_DECR_REF(key);
        return res;
}

enum {
        /* throw error if key does not exist */
        DF_SWAP = 1,
        /* throw error if key does exist */
        DF_EXCL = 2,
};

static enum result_t
dict_insert(Object *dict, Object *key,
              Object *attr, unsigned int flags)
{
        unsigned int i;
        struct dictvar_t *d;
        hash_t hash;
        struct bucket_t *b;

        bug_on(!!(flags & DF_EXCL) && attr == NULL);
        bug_on(!!(flags & DF_SWAP) && attr == NULL);
        bug_on((flags & (DF_SWAP|DF_EXCL)) == (DF_SWAP|DF_EXCL));
        bug_on(!isvar_dict(dict));
        bug_on(!isvar_string(key));

        d = V2D(dict);

        hash = string_update_hash(key);
        b = seek_helper(d, key, hash, &i);

        /* @child is either the former entry replaced by @attr or NULL */
        if (attr) {
                /* insert */
                if (b) {
                        /* replace old, don't grow table */
                        if (!!(flags & DF_EXCL))
                                return RES_ERROR;

                        VAR_DECR_REF(b->b_data);
                        VAR_INCR_REF(attr);
                        VAR_INCR_REF(b->b_key);
                        b->b_data = attr;
                } else {
                        /* put */
                        if (!!(flags & DF_SWAP))
                                return RES_ERROR;
                        VAR_INCR_REF(key);
                        VAR_INCR_REF(attr);
                        insert_common(d, key, attr, hash, i);
                        bug_on(d->d_used != seqvar_size(dict) + 1);
                        seqvar_set_size(dict, d->d_used);
                }
        } else if (b) {
                /* remove */
                VAR_DECR_REF(b->b_data);
                VAR_DECR_REF(b->b_key);
                bucket_free(b);
                d->d_bucket[i] = BUCKET_DEAD;
                d->d_used--;
                maybe_shrink_table(d);
                bug_on(d->d_used != seqvar_size(dict) - 1);
                seqvar_set_size(dict, d->d_used);
        }
        /*
         * XXX: If !attr and !b, trying to remove something that doesn't
         * exist.  Throw error and print msg?
         */
        return RES_OK;
}

/**
 * dict_setitem - Insert an attribute to dictionary if it doesn't exist,
 *                  or change the existing attribute if it does.
 * @self:       Dictionary object
 * @key:        Name of attribute key
 * @attr:       Value to set.  NULL means 'delete the entry'
 *
 * This does not touch the type's built-in-method attributes.
 *
 * Return: RES_OK or RES_ERROR.
 */
enum result_t
dict_setitem(Object *dict, Object *key, Object *attr)
{
        return dict_insert(dict, key, attr, 0);
}

/*
 * like dict_setitem, but throw error if @key already exists.
 * Used by the symbol table to prevent duplicate declarations.
 * @attr may not be NULL this time.
 */
enum result_t
dict_setitem_exclusive(Object *dict,
                         Object *key, Object *attr)
{
        return dict_insert(dict, key, attr, DF_EXCL);
}

/**
 * used for specific purpose, so de-duplicate @key.
 * See comments in literal.c, that's where this is used.
 */
char *
dict_unique(Object *dict, const char *key)
{
        Object *keycopy;
        unsigned int i;
        hash_t hash;
        struct bucket_t *b;
        struct dictvar_t *d;

        d = V2D(dict);
        bug_on(!isvar_dict(dict));

        /*
         * XXX only done at load time, but is it still
         * time consuming?  This is for **every** token!
         */
        keycopy = stringvar_new(key);
        hash = string_update_hash(keycopy);
        b = seek_helper(d, keycopy, hash, &i);
        if (b) {
                VAR_DECR_REF(keycopy);
                return string_get_cstring(b->b_key);
        }

        insert_common(d, keycopy, keycopy, hash, i);
        seqvar_set_size(dict, d->d_used);

        /* additional incref since it's stored twice */
        VAR_INCR_REF(keycopy);
        return string_get_cstring(keycopy);
}

/*
 * like dict_setitem, but throw error if @key does not exist.
 * Used by the symbol table to change global variable values.
 * @attr may not be NULL.
 */
enum result_t
dict_setitem_replace(Object *dict,
                       Object *key, Object *attr)
{
        return dict_insert(dict, key, attr, DF_SWAP);
}


static int
dict_hasitem(Object *dict, Object *key)
{
        unsigned int i;
        if (!key)
                return 0;
        bug_on(!isvar_dict(dict));

        hash_t hash = string_update_hash(key);
        return seek_helper(V2D(dict), key, hash, &i) != NULL;
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
dict_add_to_globals(Object *dict)
{
        unsigned int i;
        struct dictvar_t *d = V2D(dict);
        bug_on(!isvar_dict(dict));

        for (i = 0; i < d->d_size; i++) {
                struct bucket_t *b = d->d_bucket[i];
                if (b == NULL || b == BUCKET_DEAD)
                        continue;

                vm_add_global(b->b_key, b->b_data);
        }
}

/* **********************************************************************
 *              Built-in Operator Callbacks
 ***********************************************************************/

static int
dict_cmp(Object *a, Object *b)
{
        if (isvar_dict(b))
                return 0;
        /* FIXME: need to recurse here */
        return 1;
}

static bool
dict_cmpz(Object *obj)
{
        return seqvar_size(obj) == 0;
}

static void
dict_reset(Object *o)
{
        struct dictvar_t *dict;
        int i;

        bug_on(!isvar_dict(o));
        dict = V2D(o);

        for (i = 0; i < dict->d_size; i++) {
                if (dict->d_bucket[i] == BUCKET_DEAD) {
                        dict->d_bucket[i] = NULL;
                } else if (dict->d_bucket[i] != NULL) {
                        VAR_DECR_REF(dict->d_bucket[i]->b_data);
                        VAR_DECR_REF(dict->d_bucket[i]->b_key);
                        bucket_free(dict->d_bucket[i]);
                        dict->d_bucket[i] = NULL;
                }
        }
        dict->d_count = dict->d_used = 0;
        efree(dict->d_bucket);
}

static Object *
dict_str(Object *o)
{
        struct dictvar_t *d;
        struct buffer_t b;
        unsigned int i;
        int count;
        Object *ret;

        bug_on(!isvar_dict(o));

        d = V2D(o);
        buffer_init(&b);
        buffer_putc(&b, '{');

        count = 0;
        for (i = 0; i < d->d_size; i++) {
                struct bucket_t *bk = d->d_bucket[i];
                Object *item;
                if (bk == NULL || bk == BUCKET_DEAD)
                        continue;

                if (count > 0)
                        buffer_puts(&b, ", ");

                buffer_putc(&b, '\'');
                buffer_puts(&b, string_get_cstring(bk->b_key));
                buffer_puts(&b, "': ");

                item = var_str(bk->b_data);
                buffer_puts(&b, string_get_cstring(item));
                VAR_DECR_REF(item);

                count++;
        }

        buffer_putc(&b, '}');
        ret = stringvar_from_buffer(&b);
        return ret;
}

static int
dict_copyto(Object *to, Object *from)
{
        unsigned int i;
        struct dictvar_t *d = V2D(from);

        for (i = 0; i < d->d_size; i++) {
                struct bucket_t *b = d->d_bucket[i];
                if (b == NULL || b == BUCKET_DEAD)
                        continue;
                if (dict_setitem(to, b->b_key, b->b_data) != RES_OK)
                        return RES_ERROR;
        }
        return RES_OK;
}

static Object *
dict_union(Object *a, Object *b)
{
        Object *c = dictvar_new();

        if (dict_copyto(c, a) != RES_OK)
                goto err;
        if (dict_copyto(c, b) != RES_OK)
                goto err;
        return c;

err:
        if (!err_occurred())
                err_setstr(RuntimeError, "Failed to copy dict");
        VAR_DECR_REF(c);
        return ErrorVar;
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
Object *
do_dict_foreach(Frame *fr)
{
        Object *keys, *func, *self, *priv;
        int i, len, status;

        self = get_this(fr);

        if (arg_type_check(self, &DictType) == RES_ERROR)
                return ErrorVar;

        func = frame_get_arg(fr, 0);
        if (!func) {
                err_argtype("function");
                return ErrorVar;
        }
        priv = frame_get_arg(fr, 1);
        if (!priv)
                priv = NullVar;

        keys = dict_keys(self);
        len = var_len(keys);
        bug_on(len < 0);

        status = RES_OK;
        for (i = 0; i < len; i++) {
                Object *key, *val, *argv[3], *cbret;

                key = array_getitem(keys, i);
                bug_on(!key || key == ErrorVar);
                val = dict_getitem(self, key);
                if (!val) /* user shenanigans in foreach loop */
                        continue;

                argv[0] = val;
                argv[1] = key;
                argv[2] = priv;
                cbret = vm_exec_func(fr, func, 3, argv, false);

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
static Object *
do_dict_len(Frame *fr)
{
        Object *v;
        int i;

        v = vm_get_this(fr);
        if (arg_type_check(v, &DictType) == RES_ERROR)
                return ErrorVar;

        i = OBJ_SIZE(v);
        return intvar_new(i);
}

static Object *
do_dict_delitem(Frame *fr)
{
        Object *self = get_this(fr);
        Object *name = vm_get_arg(fr, 0);

        if (arg_type_check(self, &DictType) == RES_ERROR)
                return ErrorVar;

        if (arg_type_check(name, &StringType) != 0)
                return ErrorVar;

        if (dict_setitem(self, name, NULL) != RES_OK)
                return ErrorVar;
        return NULL;
}

static Object *
do_dict_keys(Frame *fr)
{
        Object *self = vm_get_this(fr);
        if (arg_type_check(self, &DictType) == RES_ERROR)
                return ErrorVar;
        return dict_keys(self);
}

/*
 * .copy()      Duplicate myself
 *
 * This is not recursive.  If any of obj2's items are lists
 * or dictionaries, then they will still be copied by-reference.
 */
static Object *
do_dict_copy(Frame *fr)
{
        Object *self = get_this(fr);
        Object *ret = dictvar_new();

        if (arg_type_check(self, &DictType) == RES_ERROR)
                return ErrorVar;

        if (dict_copyto(ret, self) != RES_OK) {
                VAR_DECR_REF(ret);
                return ErrorVar;
        }
        return ret;
}

static void
purloin_one(struct bucket_t *b)
{
        if (isvar_method(b->b_data)) {
                Object *func, *owner;
                methodvar_tofunc(b->b_data, &func, &owner);
                VAR_DECR_REF(owner);
                VAR_DECR_REF(b->b_data);
                b->b_data = func;
        }
}

/*
 * .purloin()
 * .purloin(key)        Reclaim all method-object entries contained
 *                      within a dictionary.
 *
 * The next retrieval of a certain function from the dictionary will
 * be plain functions, not method objects pointing somewhere else.
 * The retrieval process (if done by user, ie. via var_getattr), will
 * put the function into a new method object pointing at this
 * dictionary.
 *
 * This is not safe unless done with extreme care.  It only exists
 * because there is bound to be some rare scenario where inheritance
 * via the union operator just won't be enough.
 */
static Object *
do_dict_purloin(Frame *fr)
{
        unsigned int i;
        struct bucket_t *b;
        Object *self = get_this(fr);
        Object *key  = vm_get_arg(fr, 0);
        struct dictvar_t *d = V2D(self);

        if (arg_type_check(self, &DictType) == RES_ERROR)
                return ErrorVar;

        if (!key) {
                for (i = 0; i < d->d_size; i++) {
                        b = d->d_bucket[i];
                        if (b == NULL || b == BUCKET_DEAD)
                                continue;

                        purloin_one(b);
                }
        } else {
                hash_t hash;

                if (arg_type_check(key, &StringType) == RES_ERROR)
                        return ErrorVar;

                hash = string_update_hash(key);
                b = seek_helper(d, key, hash, &i);
                if (!b) {
                        err_setstr(KeyError,
                                "Cannot purloin %s: does not exist",
                                string_get_cstring(key));
                        return ErrorVar;
                }
                purloin_one(b);
        }
        return NULL;
}

static const struct type_inittbl_t dict_cb_methods[] = {
        V_INITTBL("len",       do_dict_len,       0, 0, -1, -1),
        V_INITTBL("foreach",   do_dict_foreach,   1, 2, -1, -1),
        V_INITTBL("delitem",   do_dict_delitem,   1, 1, -1, -1),
        V_INITTBL("purloin",   do_dict_purloin,   0, 1, -1, -1),
        V_INITTBL("keys",      do_dict_keys,      0, 0, -1, -1),
        V_INITTBL("copy",      do_dict_copy,      0, 0, -1, -1),
        TBLEND,
};

static const struct map_methods_t dict_map_methods = {
        .getitem = dict_getitem,
        .setitem = dict_setitem,
        .hasitem = dict_hasitem,
        .mpunion = dict_union,
};

struct type_t DictType = {
        .name = "dictionary",
        .opm    = NULL,
        .cbm    = dict_cb_methods,
        .mpm    = &dict_map_methods,
        .sqm    = NULL,
        .size   = sizeof(struct dictvar_t),
        .str    = dict_str,
        .cmp    = dict_cmp,
        .cmpz   = dict_cmpz,
        .reset  = dict_reset,
};


