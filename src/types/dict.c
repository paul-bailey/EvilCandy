/*
 * Definitions for the dictionary (ie. associative array) class of objects.
 *
 * JavaScript calls these "objects".  Python calls them "dictionaries".
 * Calling one kind of an object an 'object' to distinguish it from another
 * kind of object is kind of janky, so I'm going with Python on this one.
 *
 * Still, EvilCandy takes JavaScript's Middle Way:  Internal code which
 * accesses dictionaries using these API functions directly can treat a
 * dictionary like a pure associative array, while dictionaries accessed
 * by user code are assumed to be class instantiations.  The code in this
 * file plays dumb to either case.
 */
#include <evilcandy.h>

/**
 * struct dictvar_t - Descriptor for an object handle
 * @d_size:             Array size of d_keys/d_vals, always a power of 2
 * @d_used:             Active entries in hash table
 * @d_count:            Active + removed ('dead') entries in hash table
 * @d_grow_size:        Next threshold for expanding
 * @d_shrink_size:      Next threshold for shrinking
 * @d_keys:             Array of keys
 * @d_vals:             Array of values, whose indices match those of keys.
 *
 * d_keys and d_vals are allocated in one call each time the table resizes.
 * d_vals points at d_keys[d_size].
 */
struct dictvar_t {
        struct seqvar_t base;
        size_t d_size;
        size_t d_used;
        size_t d_count;
        size_t d_grow_size;
        size_t d_shrink_size;
        Object **d_keys;
        Object **d_vals;
};

#define V2D(v)          ((struct dictvar_t *)(v))
#define V2SQ(v)         ((struct seqvar_t *)(v))
#define OBJ_SIZE(v)     seqvar_size(v)


/* **********************************************************************
 *                      Hash table helpers
 ***********************************************************************/

#define BUCKET_DEAD     ((void *)-1)

/*
 * this initial size is small enough to not be a burden
 * but large enough that for 90% use-cases, no resizing
 * need occur.
 */
enum { INIT_SIZE = 16 };

/*
 * d_keys and d_vals will be clobbered, they're assumed to have
 * been saved.
 */
static void
bucket_alloc(struct dictvar_t *dict)
{
        /* x2 because keys and vals are alloc'd together */
        dict->d_keys = ecalloc(sizeof(Object *) * dict->d_size * 2);
        dict->d_vals = &dict->d_keys[dict->d_size];
}

static bool
str_key_match(Object *key1, Object *key2)
{
        bug_on(!isvar_string(key1) || !isvar_string(key2));

        /* fast-path "==", since these still sometimes are literal()'d */
        return key1 == key2
               || (string_hash(key1) == string_hash(key2)
                   && !strcmp(string_cstring(key1),
                              string_cstring(key2)));
}

static inline int
bucketi(struct dictvar_t *dict, hash_t hash)
{
        return hash & (dict->d_size - 1);
}

static int
seek_helper(struct dictvar_t *dict, Object *key)
{
        Object *k;
        hash_t hash = string_update_hash(key);
        unsigned long perturb = hash;
        int i = bucketi(dict, hash);
        while ((k = dict->d_keys[i]) != NULL) {
                if (k != BUCKET_DEAD && str_key_match(k, key))
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
        bug_on(i < 0 || i >= dict->d_size);
        return i;
}

static void
transfer_table(struct dictvar_t *dict, size_t old_size)
{
        int i, j, count;
        Object **old_keys = dict->d_keys;
        Object **old_vals = dict->d_vals;

        bucket_alloc(dict);

        count = 0;
        for (i = 0; i < old_size; i++) {
                unsigned long perturb;
                Object *k = old_keys[i];
                hash_t hash;

                if (k == NULL || k == BUCKET_DEAD)
                        continue;

                hash = string_hash(k);
                bug_on(!hash);
                perturb = hash;
                j = bucketi(dict, hash);
                while (dict->d_keys[j] != NULL) {
                        perturb >>= 5;
                        j = bucketi(dict, j * 5 + perturb + 1);
                }
                dict->d_keys[j] = old_keys[i];
                dict->d_vals[j] = old_vals[i];
                count++;
        }

        dict->d_count = dict->d_used = count;
        efree(old_keys);
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
              Object *data, int i)
{
        bug_on(!isvar_string(key));
        dict->d_keys[i] = key;
        dict->d_vals[i] = data;
        dict->d_count++;
        dict->d_used++;
        maybe_grow_table(dict);
}


/* **********************************************************************
 *                              API functions
 ***********************************************************************/

/**
 * dict_unpack - Unpack a dictionary's contents into args; intended for
 *               keyword-argument unpacking.
 * @obj: Dictionary to unpack from
 *
 * Args are key followed by a pointer to a value followed by a default
 * value to set if the key is not found.  A NULL in the key's place
 * terminates the args. value and default may not be NULL.
 *
 * This is best shown by example...
 *
 *      Object *key1 = stringvar_new("alice");
 *      Object *key2 = stringvar_new("bob");
 *      Object *val1;
 *
 *      Object *deflt1 = intvar_new(1);
 *      Object *val2;
 *      Object *deflt2 = intvar_new(2);
 *
 *      dict_unpack(mydict,
 *                  key1, &val1, deflt1,
 *                  key2, &val2, deflt2,
 *                  NULL);
 *
 * Now val1 stores value for 'alice' and val2 stores value for 'bob'.
 * A reference was produced for each of these values.
 *
 * No exceptions will be thrown.  Malformed key/value arguments may
 * trigger a bug trap.
 *
 * XXX: Specialized, shouldn't be a 'dict' function at all.
 */
void
dict_unpack(Object *obj, ...)
{
        va_list ap;
        Object **ppv;
        ssize_t n;

        bug_on(!isvar_dict(obj));
        va_start(ap, obj);

        /*
         * @n keeps track of the number of unpacked items in @obj.  The
         * caller should not pass duplicate keys in their argument list,
         * so decrement a temporary size variable for every found arg in
         * @obj.  When it hits zero, save time by just using the defaults
         * for the remaining args.
         */
        n = seqvar_size(obj);
        for (;;) {
                Object *k, *v, *deflt;

                k = va_arg(ap, Object *);
                if (!k)
                        break;
                ppv = va_arg(ap, Object **);
                deflt = va_arg(ap, Object *);

                bug_on(!isvar_string(k));
                bug_on(!ppv);
                bug_on(!deflt);

                if (n > 0) {
                        v = dict_getitem(obj, k);
                        if (v) {
                                n--;
                                *ppv = v;
                                continue;
                        }
                }
                VAR_INCR_REF(deflt);
                *ppv = deflt;
        }

        va_end(ap);
}

/**
 * dict_keys - Get an alphabetically sorted list of all the keys
 *               currently in the dictionary.
 */
Object *
dict_keys(Object *obj, bool sorted)
{
        Object *keys;
        struct dictvar_t *d;
        int i;
        int array_i;

        bug_on(!isvar_dict(obj));
        d = V2D(obj);
        keys = arrayvar_new(OBJ_SIZE(obj));

        array_i = 0;

        for (i = 0; i < d->d_size; i++) {
                Object *ks = d->d_keys[i];
                if (ks == NULL || ks == BUCKET_DEAD)
                        continue;

                array_setitem(keys, array_i, ks);
                array_i++;
        }

        if (sorted)
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
        bucket_alloc(d);
        return o;
}

/**
 * dictvar_from_methods - Create a dictionary from a methods lookup table
 * @parent: Dictionary to add methods to, or NULL to create a new dictionary
 * @tbl: A methods initialization table.
 *
 * Return: @parent, or if it was NULL, a pointer to the new dictionary
 *
 * Used for early-initialization stuff and module initialization.
 */
Object *
dictvar_from_methods(Object *parent, const struct type_inittbl_t *tbl)
{
        const struct type_inittbl_t *t;
        Object *ret;
        if (parent)
                ret = parent;
        else
                ret = dictvar_new();

        if (!tbl)
                return ret;

        for (t = tbl; t->name != NULL; t++) {
                Object *func, *key;
                func = funcvar_from_lut(t);
                key = stringvar_new(t->name);
                dict_setitem(ret, key, func);
                VAR_DECR_REF(func);
                VAR_DECR_REF(key);
        }
        return ret;
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
        int i;

        d = V2D(o);
        bug_on(!isvar_dict(o));
        bug_on(!isvar_string(key));

        i = seek_helper(d, key);
        if (d->d_keys[i] == NULL)
                return NULL;

        VAR_INCR_REF(d->d_vals[i]);
        return d->d_vals[i];
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
        int i;
        struct dictvar_t *d;

        bug_on(!!(flags & DF_EXCL) && attr == NULL);
        bug_on(!!(flags & DF_SWAP) && attr == NULL);
        bug_on((flags & (DF_SWAP|DF_EXCL)) == (DF_SWAP|DF_EXCL));
        bug_on(!isvar_dict(dict));
        bug_on(!isvar_string(key));

        d = V2D(dict);

        i = seek_helper(d, key);

        /* @child is either the former entry replaced by @attr or NULL */
        if (attr) {
                /* insert */
                if (d->d_keys[i] != NULL) {
                        /* replace old, don't grow table */
                        if (!!(flags & DF_EXCL))
                                return RES_ERROR;

                        VAR_INCR_REF(attr);
                        VAR_INCR_REF(key);
                        VAR_DECR_REF(d->d_vals[i]);
                        VAR_DECR_REF(d->d_keys[i]);
                        d->d_keys[i] = key;
                        d->d_vals[i] = attr;
                } else {
                        /* put */
                        if (!!(flags & DF_SWAP))
                                return RES_ERROR;

                        VAR_INCR_REF(key);
                        VAR_INCR_REF(attr);
                        insert_common(d, key, attr, i);
                        bug_on(d->d_used != seqvar_size(dict) + 1);
                        seqvar_set_size(dict, d->d_used);
                }
        } else if (d->d_keys[i] != NULL) {
                /* remove */
                VAR_DECR_REF(d->d_vals[i]);
                VAR_DECR_REF(d->d_keys[i]);
                d->d_keys[i] = BUCKET_DEAD;
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
        int i;
        struct dictvar_t *d;

        d = V2D(dict);
        bug_on(!isvar_dict(dict));

        /*
         * XXX only done at load time, but is it still
         * time consuming?  This is for **every** token!
         */
        keycopy = stringvar_new(key);
        i = seek_helper(d, keycopy);
        if (d->d_keys[i] != NULL) {
                VAR_DECR_REF(keycopy);
                return (char *)string_cstring(d->d_keys[i]);
        }

        insert_common(d, keycopy, keycopy, i);
        seqvar_set_size(dict, d->d_used);

        /* additional incref since it's stored twice */
        VAR_INCR_REF(keycopy);
        return (char *)string_cstring(keycopy);
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
        int i;
        if (!key)
                return 0;
        bug_on(!isvar_dict(dict));

        i = seek_helper(V2D(dict), key);
        return V2D(dict)->d_keys[i] != NULL;
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
        int i;
        struct dictvar_t *d = V2D(dict);
        bug_on(!isvar_dict(dict));

        for (i = 0; i < d->d_size; i++) {
                Object *k = d->d_keys[i];
                if (k == NULL || k == BUCKET_DEAD)
                        continue;

                vm_add_global(k, d->d_vals[i]);
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
                if (dict->d_keys[i] == BUCKET_DEAD) {
                        dict->d_keys[i] = NULL;
                } else if (dict->d_keys[i] != NULL) {
                        VAR_DECR_REF(dict->d_vals[i]);
                        VAR_DECR_REF(dict->d_keys[i]);
                        dict->d_keys[i] = NULL;
                        dict->d_vals[i] = NULL;
                }
        }
        dict->d_count = dict->d_used = 0;
        efree(dict->d_keys);
}

static Object *
dict_str(Object *o)
{
        struct dictvar_t *d;
        struct buffer_t b;
        int i;
        int count;
        Object *ret;

        RECURSION_DECLARE_FUNC();
        RECURSION_START_FUNC(RECURSION_MAX);

        bug_on(!isvar_dict(o));

        d = V2D(o);
        buffer_init(&b);
        buffer_putc(&b, '{');

        count = 0;
        for (i = 0; i < d->d_size; i++) {
                Object *k = d->d_keys[i];
                Object *item;
                if (k == NULL || k == BUCKET_DEAD)
                        continue;

                if (count > 0)
                        buffer_puts(&b, ", ");

                buffer_putc(&b, '\'');
                buffer_puts(&b, string_cstring(k));
                buffer_puts(&b, "': ");

                item = var_str(d->d_vals[i]);
                buffer_puts(&b, string_cstring(item));
                VAR_DECR_REF(item);

                count++;
        }

        buffer_putc(&b, '}');
        ret = stringvar_from_buffer(&b);

        RECURSION_END_FUNC();
        return ret;
}

static int
dict_copyto(Object *to, Object *from)
{
        int i;
        struct dictvar_t *d = V2D(from);

        for (i = 0; i < d->d_size; i++) {
                Object *k = d->d_keys[i];
                if (k == NULL || k == BUCKET_DEAD)
                        continue;
                if (dict_setitem(to, k, d->d_vals[i]) != RES_OK)
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

        keys = dict_keys(self, true);
        len = seqvar_size(keys);
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
        Object *sorted;
        Object *self = vm_get_this(fr);
        Object *kw = vm_get_arg(fr, 0);

        if (arg_type_check(self, &DictType) == RES_ERROR)
                return ErrorVar;

        bug_on(!kw || !isvar_dict(kw));

        dict_unpack(kw, STRCONST_ID(sorted), &sorted, gbl.zero, NULL);
        if (arg_type_check(sorted, &IntType) == RES_ERROR)
                return ErrorVar;

        return dict_keys(self, !!intvar_toll(sorted));
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
purloin_one(Object **x)
{
        if (isvar_method(*x)) {
                Object *func, *owner;
                methodvar_tofunc(*x, &func, &owner);
                VAR_DECR_REF(owner);
                VAR_DECR_REF(*x);
                *x = func;
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
        int i;
        Object *self = get_this(fr);
        Object *key  = vm_get_arg(fr, 0);
        struct dictvar_t *d = V2D(self);

        if (arg_type_check(self, &DictType) == RES_ERROR)
                return ErrorVar;

        if (!key) {
                for (i = 0; i < d->d_size; i++) {
                        Object *k = d->d_keys[i];
                        if (k == NULL || k == BUCKET_DEAD)
                                continue;

                        purloin_one(&d->d_vals[i]);
                }
        } else {
                if (arg_type_check(key, &StringType) == RES_ERROR)
                        return ErrorVar;

                i = seek_helper(d, key);
                if (d->d_keys[i] == NULL) {
                        err_setstr(KeyError,
                                   "Cannot purloin %s: does not exist",
                                   string_cstring(key));
                        return ErrorVar;
                }
                purloin_one(&d->d_vals[i]);
        }
        return NULL;
}

static Object *
dict_getprop_length(Object *self)
{
        bug_on(!isvar_dict(self));
        return intvar_new(seqvar_size(self));
}

static const struct type_inittbl_t dict_cb_methods[] = {
        V_INITTBL("foreach",   do_dict_foreach,   1, 2, -1, -1),
        V_INITTBL("delitem",   do_dict_delitem,   1, 1, -1, -1),
        V_INITTBL("purloin",   do_dict_purloin,   0, 1, -1, -1),
        V_INITTBL("keys",      do_dict_keys,      1, 1, -1,  0),
        V_INITTBL("copy",      do_dict_copy,      0, 0, -1, -1),
        TBLEND,
};

static const struct type_prop_t dict_prop_getsets[] = {
        { .name = "length", .getprop = dict_getprop_length, .setprop = NULL },
        { .name = NULL },
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
        .prop_getsets = dict_prop_getsets,
};


