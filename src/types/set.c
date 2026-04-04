/* set.c - Implementation of set data type */
#include <evilcandy.h>

struct setvar_t {
        struct seqvar_t base;
        size_t s_size;          /* size of entries, a power of two */
        size_t s_used;          /* # of active entries */
        size_t s_count;         /* # of active + # of removed entries */
        size_t s_growsize;      /* next threshold for expanding */
        size_t s_shrinksize;    /* next threshold for shrinking */
        Object **s_keys;        /* Actual entries */
};

#define BUCKET_DEAD             ((void *)-1)
#define SET_INITIAL_SIZE        16
#define KEY_ALLOC_SIZE(n_)      ((n_) * sizeof(Object *))

/* **********************************************************************
 *                      Local helpers
 ***********************************************************************/

/*
 * FIXME: A lot of these are near DRY violations with dict.c.  Make an
 * in-directory private header to inline common functionality.
 */

static void
bucket_alloc(struct setvar_t *sv, size_t size)
{
        sv->s_size = size;
        sv->s_keys = emalloc(KEY_ALLOC_SIZE(size));
        memset(sv->s_keys, 0, KEY_ALLOC_SIZE(size));
}

static Object *
setvar_instantiate(void)
{
        Object *ret;
        struct setvar_t *sv;

        ret = var_new(&SetType);
        sv = (struct setvar_t *)ret;

        sv->s_used = sv->s_count = 0;
        bucket_alloc(sv, SET_INITIAL_SIZE);
        seqvar_set_size(ret, 0);
        return ret;
}

static int
bucketi(struct setvar_t *sv, hash_t hash)
{
        return hash & (sv->s_size - 1);
}

static bool
key_match(Object *key1, Object *key2, hash_t key2_hash)
{
        /* XXX: DRY violation with 'key_match' in dict.c */
        if (isvar_string(key1)) {
                if (isvar_string(key2)) {
                        if (var_hash(key1) != key2_hash)
                                return false;
                        return string_eq(key1, key2);
                }
                return false;
        }
        return var_matches(key1, key2);
}

static int
seek_helper(struct setvar_t *sv, Object *key)
{
        Object *k;
        hash_t hash;
        unsigned long perturb;
        int i;

        /* XXX: why not store hash *with* key? */
        hash = var_hash(key);
        if (hash == RES_ERROR)
                return -1;

        perturb = hash;
        i = bucketi(sv, hash);
        while ((k = sv->s_keys[i]) != NULL) {
                if (k != BUCKET_DEAD && key_match(k, key, hash))
                        break;
                /*
                 * Collision or dead entry.  See dict.c for big long
                 * explanation what I'm doing here.  It's the same thing.
                 */
                perturb >>= 5;
                i = bucketi(sv, i * 5 + perturb + 1);
        }
        bug_on(i < 0 || i >= sv->s_size);
        return i;
}

static void
transfer_table(struct setvar_t *sv, size_t old_size)
{
        ssize_t i, j, n;
        Object **old_keys = sv->s_keys;

        bucket_alloc(sv, sv->s_size);

        n = 0;
        for (i = 0; i < old_size; i++) {
                unsigned long perturb;
                Object *k;
                hash_t hash;

                k = old_keys[i];
                if (k == NULL || k == BUCKET_DEAD)
                        continue;

                hash = var_hash(k);
                perturb = hash;
                j = bucketi(sv, hash);
                while (sv->s_keys[j] != NULL) {
                        perturb >>= 5;
                        j = bucketi(sv, j * 5 + perturb + 1);
                }
                sv->s_keys[j] = k;
                n++;
        }
        sv->s_count = sv->s_used = n;
        seqvar_set_size((Object *)sv, n);

        efree(old_keys);
}

static void
refresh_grow_markers(struct setvar_t *sv)
{
        sv->s_growsize = (sv->s_size << 1) / 3;
        sv->s_shrinksize = sv->s_used <= SET_INITIAL_SIZE
                        ? 0 : sv->s_growsize / 3;
}

static void
maybe_grow_table(struct setvar_t *sv)
{
        size_t old_size = sv->s_size;
        while (sv->s_count > sv->s_growsize) {
                sv->s_size *= 2;
                refresh_grow_markers(sv);
        }
        if (sv->s_size != old_size)
                transfer_table(sv, old_size);
        seqvar_set_size((Object *)sv, sv->s_used);
}

static void
maybe_shrink_table(struct setvar_t *sv)
{
        size_t old_size = sv->s_size;
        while (sv->s_used < sv->s_shrinksize) {
                sv->s_size /= 2;
                refresh_grow_markers(sv);
        }
        if (sv->s_size < SET_INITIAL_SIZE)
                sv->s_size = SET_INITIAL_SIZE;

        if (sv->s_size != old_size)
                transfer_table(sv, old_size);
        seqvar_set_size((Object *)sv, sv->s_used);
}

static void
set_clear_noresize(struct setvar_t *sv)
{
        size_t i;
        for (i = 0; i < sv->s_size; i++) {
                Object *k = sv->s_keys[i];
                if (k == BUCKET_DEAD) {
                        sv->s_keys[i] = NULL;
                } else if (k != NULL) {
                        VAR_DECR_REF(k);
                        sv->s_keys[i] = NULL;
                }
        }
        sv->s_count = sv->s_used = 0;
        seqvar_set_size((Object *)sv, sv->s_used);
}

static bool
set_hasitem(Object *set, Object *item)
{
        int i;
        if (!item)
                return 0;
        bug_on(!isvar_set(set));
        i = seek_helper((struct setvar_t *)set, item);
        return i >= 0 && ((struct setvar_t *)set)->s_keys[i] != NULL;
}

static Object *
set_shallowcopy(Object *set)
{
        struct setvar_t *sv = (struct setvar_t *)set;
        size_t i, n = sv->s_size;
        Object *new = setvar_instantiate();
        for (i = 0; i < n; i++) {
                enum result_t res;
                Object *k = sv->s_keys[i];
                if (!k || k == BUCKET_DEAD)
                        continue;
                res = set_additem(new, k, NULL);
                bug_on(res == RES_ERROR);
                (void)res;
        }
        return new;
}

/* Discard @child from @set, no error if @child was not in @set */
static void
set_discard(Object *set, Object *child)
{
        struct setvar_t *sv = (struct setvar_t *)set;
        int i;

        i = seek_helper(sv, child);
        /* XXX: Exception if child is not hashable? */
        if (i < 0)
                return;

        if (sv->s_keys[i] == NULL)
                return;

        VAR_DECR_REF(sv->s_keys[i]);
        sv->s_keys[i] = BUCKET_DEAD;
        sv->s_used--;
        maybe_shrink_table(sv);
}

/* **********************************************************************
 *                      Type callbacks
 ***********************************************************************/

static int
do_set_hasitem(Object *set, Object *item)
{
        return set_hasitem(set, item);
}

static Object *
set_sub_op(Object *a, Object *b)
{
        struct setvar_t *sv = (struct setvar_t *)b;
        size_t i, n = sv->s_size;
        /* XXX: very non-optimal */
        Object *new = set_shallowcopy(a);

        for (i = 0; i < n; i++) {
                Object *k = sv->s_keys[i];
                if (!k || k == BUCKET_DEAD)
                        continue;
                set_discard(new, k);
        }
        return new;
}

static Object *
set_intersection_op(Object *a, Object *b)
{
        struct setvar_t *sv = (struct setvar_t *)a;
        size_t i, n = sv->s_size;
        Object *new = setvar_instantiate();

        for (i = 0; i < n; i++) {
                enum result_t res;
                Object *k = sv->s_keys[i];
                if (!k || k == BUCKET_DEAD)
                        continue;
                if (!set_hasitem(b, k))
                        continue;
                res = set_additem(new, k, NULL);
                bug_on(res == RES_ERROR);
                (void)res;
        }
        return new;
}

static Object *
set_union_op(Object *a, Object *b)
{
        struct setvar_t *sv[2];
        Object *new;
        size_t i, j;

        new = setvar_instantiate();
        sv[0] = (struct setvar_t *)a;
        sv[1] = (struct setvar_t *)b;

        for (i = 0; i < 2; i++) {
                for (j = 0; j < sv[i]->s_size; j++) {
                        enum result_t res;
                        Object *k = sv[i]->s_keys[j];
                        if (!k || k == BUCKET_DEAD)
                                continue;
                        res = set_additem(new, k, NULL);
                        bug_on(res == RES_ERROR);
                        (void)res;
                }
        }
        return new;
}

/* helper to set_exclusive_op */
static void
set_exclusive_from_one(Object *output, Object *this_set, Object *other_set)
{
        struct setvar_t *sv = (struct setvar_t *)this_set;
        size_t i;
        for (i = 0; i < sv->s_size; i++) {
                enum result_t res;
                Object *k = sv->s_keys[i];
                if (!k || k == BUCKET_DEAD)
                        continue;
                if (set_hasitem(other_set, k))
                        continue;
                res = set_additem(output, k, NULL);
                bug_on(res == RES_ERROR);
                (void)res;
        }
}

static Object *
set_exclusive_op(Object *a, Object *b)
{
        Object *new = setvar_instantiate();
        set_exclusive_from_one(new, a, b);
        set_exclusive_from_one(new, b, a);
        return new;
}

static Object *
set_str(Object *set)
{
        struct setvar_t *sv = (struct setvar_t *)set;
        struct string_writer_t wr;
        size_t i, count = 0, n = sv->s_size;

        if (seqvar_size(set) == 0)
                return VAR_NEW_REF(STRCONST_ID(emptyset));

        string_writer_init(&wr, 1);
        string_writer_appends(&wr, "{");
        for (i = 0; i < n; i++) {
                Object *repr;
                Object *k = sv->s_keys[i];
                if (!k || k == BUCKET_DEAD)
                        continue;
                if (count > 0)
                        string_writer_appends(&wr, ", ");
                repr = var_str(k);
                string_writer_append_strobj(&wr, repr);
                VAR_DECR_REF(repr);
                count++;
        }
        string_writer_appends(&wr, "}");
        return stringvar_from_writer(&wr);
}

static bool
set_cmpeq(Object *a, Object *b)
{
        struct setvar_t *sv;
        size_t i;

        if (seqvar_size(a) != seqvar_size(b))
                return (int)(seqvar_size(a) - seqvar_size(b));

        /* Sizes are equal, so if a is a subset of b then a == b */
        sv = (struct setvar_t *)a;
        for (i = 0; i < sv->s_size; i++) {
                Object *k = sv->s_keys[i];
                if (!k || k == BUCKET_DEAD)
                        continue;
                if (!set_hasitem(b, k))
                        return false;
        }
        return true;
}

static void
set_reset(Object *set)
{
        set_clear_noresize((struct setvar_t *)set);
        efree(((struct setvar_t *)set)->s_keys);
}

static Object *
set_create(Frame *fr)
{
        Object *seq = NULL;
        if (vm_getargs(fr, "[|<*>!]:set", &seq) == RES_ERROR)
                return ErrorVar;
        return setvar_new(seq);
}

struct setiter_t {
        Object base;
        Object *target;
        size_t i;
};

static Object *
set_iter_next(Object *it)
{
        struct setiter_t *sit = (struct setiter_t *)it;
        struct setvar_t *sv = (struct setvar_t *)(sit->target);
        size_t i = sit->i;
        if (!sv)
                return NULL;

        for (; i < sv->s_size; i++) {
                Object *k = sv->s_keys[i];
                if (!k || k == BUCKET_DEAD)
                        continue;
                sit->i = i + 1;
                return VAR_NEW_REF(k);
        }

        VAR_DECR_REF(sit->target);
        sit->target = NULL;
        return NULL;
}

static void
set_iter_reset(Object *it)
{
        struct setiter_t *sit = (struct setiter_t *)it;
        if (sit->target)
                VAR_DECR_REF(sit->target);
        sit->target = NULL;
}

struct type_t SetIterType = {
        .name           = "set_iterator",
        .reset          = set_iter_reset,
        .size           = sizeof(struct setiter_t),
        .iter_next      = set_iter_next,
};

static Object *
set_get_iter(Object *set)
{
        struct setiter_t *ret;

        bug_on(!isvar_set(set));
        ret = (struct setiter_t *)var_new(&SetIterType);
        ret->target = VAR_NEW_REF(set);
        ret->i = 0;
        return (Object *)ret;
}

static const struct operator_methods_t set_op_methods = {
        .sub            = set_sub_op,
        .bit_and        = set_intersection_op,
        .bit_or         = set_union_op,
        .xor            = set_exclusive_op,
};

/* XXX: is this enough to cheat var.c into thinking it's a sequential object? */
static const struct map_methods_t set_map_methods = {
        .getitem = NULL,
        .setitem = NULL,
        .hasitem = do_set_hasitem,
};

static const struct type_method_t set_cb_methods[] = {
        {NULL, NULL},
};

struct type_t SetType = {
        .flags          = 0,
        .name           = "set",
        .opm            = &set_op_methods,
        .cbm            = set_cb_methods,
        .mpm            = &set_map_methods,
        .sqm            = NULL,
        .size           = sizeof(struct setvar_t),
        .str            = set_str,
        .cmp            = NULL,
        .cmpz           = NULL,
        .cmpeq          = set_cmpeq,
        .reset          = set_reset,
        .prop_getsets   = NULL,
        .create         = set_create,
        .hash           = NULL,
        .get_iter       = set_get_iter,
};

/* **********************************************************************
 *                      API functions
 ***********************************************************************/

/**
 * set_additem - Add an item to a set
 * @set: Set to add to
 * @child: Object to add
 * @unique: if not NULL, the actual instance being stored in the set.
 *          Useful for swapping @child with something globally unique.
 *
 * Return: RES_OK or RES_ERROR. If RES_ERROR, @unique will not be touched,
 * and no references will be produced.
 *
 * Important!!  A non-NULL @unique is interpret as a request to get a
 * replacement for @child which has been 'de-duplicated'. The call might
 * look like
 *
 *      status = set_additem(set, child, &child);
 * so...
 *
 * 1. If @unique is NULL, a reference will be produced for @child only if
 *    it did not previously exist in the set.
 * 2. If @unique is non-NULL and @child has a match already in the set,
 *    then the reference for @child will be consumed.
 */
enum result_t
set_additem(Object *set, Object *child, Object **unique)
{
        struct setvar_t *sv = (struct setvar_t *)set;
        int i;

        i = seek_helper(sv, child);
        if (i < 0) {
                err_hashable(child, NULL);
                return RES_ERROR;
        }

        if (sv->s_keys[i] != NULL) {
                if (unique) {
                        VAR_DECR_REF(child);
                        *unique = VAR_NEW_REF(sv->s_keys[i]);
                }
        } else {
                sv->s_keys[i] = VAR_NEW_REF(child);
                if (unique)
                        *unique = child;

                sv->s_count++;
                sv->s_used++;
                maybe_grow_table(sv);
        }
        return RES_OK;
}

/**
 * set_extend - Append all of an iterable object's items into a set
 * @set: Set to extend
 * @seq: Iterable object
 *
 * Return: RES_OK or RES_ERROR
 */
enum result_t
set_extend(Object *set, Object *seq)
{
        Object *it, *item;

        if (!seq)
                return RES_OK;

        it = iterator_get(seq);
        if (!it) {
                err_iterable(seq, "set");
                return RES_ERROR;
        }
        ITERATOR_FOREACH(item, it) {
                if (set_additem(set, item, NULL) == RES_ERROR) {
                        err_hashable(item, NULL);
                        VAR_DECR_REF(item);
                        VAR_DECR_REF(it);
                        return RES_ERROR;
                }
                VAR_DECR_REF(item);
        }
        VAR_DECR_REF(it);
        return item == ErrorVar ? RES_ERROR : RES_OK;
}

/**
 * setvar_new - Create a new set
 * @seq: List to create set out of
 */
Object *
setvar_new(Object *seq)
{
        Object *ret = setvar_instantiate();
        if (set_extend(ret, seq) == RES_ERROR) {
                VAR_DECR_REF(ret);
                return ErrorVar;
        }
        return ret;
}


