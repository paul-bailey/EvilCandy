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
 * @d_map:              Array mapping entries in order to their indices
 *                      in @d_keys/@d_vals.  Used for iterating.
 * @d_lock:             Display lock
 *
 * d_keys, d_vals, and d_map are allocated in one call each time the
 * table resizes.  The allocation pointer is at d_keys.
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
        void *d_map;
        int d_lock;
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

static inline bool
valid_key_type(Object *key)
{
        return isvar_string(key) || isvar_int(key);
}

static void
index_write__(void *p, size_t dsize, size_t idx, ssize_t val)
{
        bug_on(dsize > INT_MAX);
        if (dsize > 0x7fff)
                ((int32_t *)p)[idx] = val;
        else if (dsize > 0x7f)
                ((int16_t *)p)[idx] = val;
        else
                ((int8_t *)p)[idx] = val;
}

static void
index_write(struct dictvar_t *d, size_t idx, ssize_t val)
{
        index_write__(d->d_map, d->d_size, idx, val);
}

static ssize_t
index_read__(void *p, size_t dsize, size_t idx)
{
        bug_on(dsize > INT_MAX);
        if (dsize > 0x7fff)
                return ((int32_t *)p)[idx];
        else if (dsize > 0x7f)
                return ((int16_t *)p)[idx];
        else {
                /* Not so sure 8-bit signed is universally supported */
                ssize_t ret = ((int8_t *)p)[idx];
                return ret >= 128 ? -1 : ret;
        }
}

static ssize_t
index_read(struct dictvar_t *d, size_t idx)
{
        return index_read__(d->d_map, d->d_size, idx);
}

static size_t
index_width(size_t dsize)
{
        bug_on(dsize > INT_MAX);
        if (dsize > 0x7fff)
                return sizeof(int32_t);
        else if (dsize > 0x7f)
                return sizeof(int16_t);
        else
                return sizeof(int8_t);
}

/*
 * d_keys and d_vals will be clobbered, they're assumed to have
 * been saved.
 */
static void
bucket_alloc(struct dictvar_t *dict)
{
        /* d_keys, d_vals, and d_map are all alloc'd together */
        size_t nelem = dict->d_size;
        size_t wid = index_width(nelem);
        dict->d_keys = emalloc(nelem * (sizeof(Object *) * 2 + wid));
        dict->d_vals = &dict->d_keys[nelem];
        dict->d_map = (void *)(&dict->d_vals[nelem]);
        memset(dict->d_keys, 0, sizeof(Object *) * 2 * nelem);
        /* Don't need to memset map, that's done in transfer_table() */
}

static bool
key_match(Object *key1, Object *key2)
{
        if (key1 == key2)
                return true;
        if (key1->v_type != key2->v_type)
                return false;
        if (isvar_string(key1)) {
                return string_hash(key1) == string_hash(key2)
                       && !strcmp(string_cstring(key1),
                                  string_cstring(key2));
        }
        if (isvar_int(key1))
                return intvar_toll(key1) == intvar_toll(key2);
        return false;
}

static void
append_to_map(struct dictvar_t *dict, int i)
{
        index_write(dict, dict->d_count, i);
}

static inline int
bucketi(struct dictvar_t *dict, hash_t hash)
{
        return hash & (dict->d_size - 1);
}

static inline hash_t
dictkey_hash(Object *key)
{
        if (isvar_string(key))
                return string_update_hash(key);
        bug_on(!isvar_int(key));
        return intvar_toll(key);
}

static int
seek_helper(struct dictvar_t *dict, Object *key)
{
        Object *k;
        hash_t hash = dictkey_hash(key);
        unsigned long perturb = hash;
        int i = bucketi(dict, hash);
        while ((k = dict->d_keys[i]) != NULL) {
                if (k != BUCKET_DEAD && key_match(k, key))
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
        ssize_t i;  /* Old keys/vals index */
        ssize_t j;  /* New keys/vals index */
        ssize_t m;  /* Old map index */
        ssize_t n;  /* New map index */
        ssize_t iwid;
        Object **old_keys = dict->d_keys;
        Object **old_vals = dict->d_vals;
        void *old_map = dict->d_map;

        bucket_alloc(dict);

        n = 0;
        for (m = 0; m < old_size; m++) {
                unsigned long perturb;
                Object *k;
                hash_t hash;

                i = index_read__(old_map, old_size, m);
                if (i < 0)
                        continue;

                k = old_keys[i];
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
                dict->d_keys[j] = k; /* ie 'old_keys[i]' */
                dict->d_vals[j] = old_vals[i];
                bug_on(index < 0);
                index_write(dict, n, j);
                n++;
        }

        dict->d_count = dict->d_used = n;

        /* Default remaining d_map to -1 */
        iwid = index_width(dict->d_size);
        memset(dict->d_map + n * iwid,
               -1, (dict->d_size - n) * iwid);
        /*
         * old_vals, old_map were alloc'd with old_keys, so they're
         * freed here too.
         */
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

static void
insert_common(struct dictvar_t *dict, Object *key,
              Object *data, int i)
{
        bug_on(!valid_key_type(key));
        dict->d_keys[i] = key;
        dict->d_vals[i] = data;
        dict->d_count++;
        dict->d_used++;
        maybe_grow_table(dict);
}

static void
dict_clear_noresize(struct dictvar_t *dict)
{
        int i;
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
        seqvar_set_size((Object *)dict, dict->d_used);
}

static void
dict_clear(struct dictvar_t *dict)
{
        dict_clear_noresize(dict);
        maybe_shrink_table(dict);
}

static enum result_t
dict_lock(struct dictvar_t *dict)
{
        if (dict->d_lock)
                return RES_ERROR;
        dict->d_lock++;
        return dict->d_lock;
}

static void
dict_unlock(struct dictvar_t *dict)
{
        dict->d_lock--;
        bug_on(dict->d_lock < 0);
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

                bug_on(!valid_key_type(k));
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
        memset(d->d_map, -1, INIT_SIZE);
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
        if (!valid_key_type(key))
                return NULL;

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

        if (!valid_key_type(key)) {
                err_setstr(TypeError, "Invalid type for dict key: '%s'",
                           typestr(key));
                return RES_ERROR;
        }

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
                        append_to_map(d, i);
                        insert_common(d, key, attr, i);
                        bug_on(d->d_used != seqvar_size(dict) + 1);
                        seqvar_set_size(dict, d->d_used);
                }
        } else {
                /* remove */
                if (d->d_keys[i] == NULL)
                        return RES_ERROR;

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
                /* dict_unique must be used only on string-only dicts */
                bug_on(!isvar_string(d->d_keys[i]));
                VAR_DECR_REF(keycopy);
                return (char *)string_cstring(d->d_keys[i]);
        }

        append_to_map(d, i);
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

/**
 * dict_iter - Iterate through a dictionary in the order that
 *             entries were inserted.
 * @dict: Dictionary to traverse
 * @iter: Set this to zero for first item, then use return value of
 *        dict_iter for remaining items.
 * @k:    Variable to store next key
 * @v:    Variable to store next value
 *
 * Return: Next value to pass as @iter, or -1 if the dictionary has been
 * fully traversed.
 *
 * Note: References will be produced for @k and @v if they are not NULL.
 *
 * FIXME: .d_map preserves insertion ORDER, but it does not preserve
 * insertion INDEX number.  That will change for a given entry if an
 * earlier insertion (lower .d_map index) is removed and the table
 * resizes.  This means that while table growth doesn't mess up this
 * function, table shrinking does, because certain key/value pairs might
 * get skipped over.
 */
ssize_t
dict_iter(Object *dict, ssize_t iter, Object **k, Object **v)
{
        struct dictvar_t *d;

        /* Only bad internal code could cause this, not user script */
        bug_on(iter < 0);
        /* Surely caller wants at least one of these */
        bug_on(!k && !v);

        d = V2D(dict);
        for (; iter < d->d_size; iter++) {
                Object *key;
                ssize_t index = index_read(d, iter);
                if (index < 0)
                        continue;
                key = d->d_keys[index];
                if (key == NULL || key == BUCKET_DEAD)
                        continue;
                if (k)
                        *k = VAR_NEW_REF(key);
                if (v)
                        *v = VAR_NEW_REF(d->d_vals[index]);
                return iter + 1;
        }
        return -1;
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
        struct dictvar_t *dict = V2D(o);
        bug_on(!isvar_dict(o));
        dict_clear_noresize(dict);
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

        bug_on(!isvar_dict(o));
        d = V2D(o);

        if (dict_lock(d) == RES_ERROR)
                return VAR_NEW_REF(STRCONST_ID(locked_dict_str));

        buffer_init(&b);
        buffer_putc(&b, '{');

        count = 0;
        for (i = 0; i < d->d_size; i++) {
                Object *k = d->d_keys[i];
                Object *vstr, *kstr;
                if (k == NULL || k == BUCKET_DEAD)
                        continue;

                if (count > 0)
                        buffer_puts(&b, ", ");

                kstr = var_str(d->d_keys[i]);
                vstr = var_str(d->d_vals[i]);
                buffer_puts(&b, string_cstring(kstr));
                buffer_puts(&b, ": ");
                buffer_puts(&b, string_cstring(vstr));
                VAR_DECR_REF(vstr);
                VAR_DECR_REF(kstr);

                count++;
        }

        buffer_putc(&b, '}');
        ret = stringvar_from_buffer(&b);

        dict_unlock(d);
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
        Object *ret;

        if (arg_type_check(self, &DictType) == RES_ERROR)
                return ErrorVar;

        bug_on(!kw || !isvar_dict(kw));

        dict_unpack(kw, STRCONST_ID(sorted), &sorted, gbl.zero, NULL);
        if (arg_type_check(sorted, &IntType) == RES_ERROR) {
                ret = ErrorVar;
        } else {
                ret = dict_keys(self, !!intvar_toll(sorted));
        }

        VAR_DECR_REF(sorted);
        return ret;
}

static Object *
do_dict_values(Frame *fr)
{
        Object *ret;
        Object *self = vm_get_this(fr);
        struct dictvar_t *dict = V2D(self);
        int i, j;

        if (arg_type_check(self, &DictType) == RES_ERROR)
                return ErrorVar;

        ret = arrayvar_new(seqvar_size(self));
        j = 0;
        for (i = 0; i < dict->d_size; i++) {
                Object *k = dict->d_keys[i];
                if (k == NULL || k == BUCKET_DEAD)
                        continue;
                array_setitem(ret, j, dict->d_vals[i]);
                bug_on(j >= seqvar_size(self));
                j++;
        }
        return ret;
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
                        Object *kstr = var_str(key);
                        err_setstr(KeyError,
                                   "Cannot purloin %s: does not exist",
                                   kstr);
                        VAR_DECR_REF(kstr);
                        return ErrorVar;
                }
                purloin_one(&d->d_vals[i]);
        }
        return NULL;
}

static Object *
do_dict_clear(Frame *fr)
{
        Object *self = vm_get_this(fr);
        bug_on(!isvar_dict(self));
        dict_clear(V2D(self));
        return NULL;
}

static Object *
dict_getprop_length(Object *self)
{
        bug_on(!isvar_dict(self));
        return intvar_new(seqvar_size(self));
}

static const struct type_inittbl_t dict_cb_methods[] = {
        V_INITTBL("clear",     do_dict_clear,       0, 0, -1, -1),
        V_INITTBL("copy",      do_dict_copy,        0, 0, -1, -1),
        V_INITTBL("delitem",   do_dict_delitem,     1, 1, -1, -1),
        V_INITTBL("foreach",   var_foreach_generic, 1, 2, -1, -1),
        V_INITTBL("keys",      do_dict_keys,        1, 1, -1,  0),
        V_INITTBL("purloin",   do_dict_purloin,     0, 1, -1, -1),
        V_INITTBL("values",    do_dict_values,      0, 0, -1, -1),
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
        .flags  = 0,
        .name   = "dictionary",
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


