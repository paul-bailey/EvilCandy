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
        if (key1 == key2)
                return true;
        /* XXX: what about int vs float? */
        if (key1->v_type != key2->v_type)
                return false;
        if (key2_hash != var_hash(key1))
                return false;
        if (isvar_string(key1))
                return string_eq(key1, key2);
        if (!key1->v_type->cmp)
                return false;
        return key1->v_type->cmp(key1, key2) == 0;
}

static int
seek_helper(struct setvar_t *sv, Object *key)
{
        Object *k;
        hash_t hash;
        unsigned long perturb;
        int i;

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

/* **********************************************************************
 *                      Type callbacks
 ***********************************************************************/

static int
do_set_hasitem(Object *set, Object *item)
{
        int i;
        if (!item)
                return 0;
        bug_on(!isvar_set(set));
        i = seek_helper((struct setvar_t *)set, item);
        return i >= 0 && ((struct setvar_t *)set)->s_keys[i] != NULL;
}

static Object *
set_sub_op(Object *a, Object *b)
{
        err_setstr(NotImplementedError, "'-' not yet implemented for sets");
        return ErrorVar;
}

static Object *
set_intersection_op(Object *a, Object *b)
{
        err_setstr(NotImplementedError, "'&' not yet implemented for sets");
        return ErrorVar;
}

static Object *
set_union_op(Object *a, Object *b)
{
        err_setstr(NotImplementedError, "'|' not yet implemented for sets");
        return ErrorVar;
}

static Object *
set_exclusive_op(Object *a, Object *b)
{
        err_setstr(NotImplementedError, "'^' not yet implemented for sets");
        return ErrorVar;
}

static Object *
set_str(Object *set)
{
        struct setvar_t *sv = (struct setvar_t *)set;
        struct string_writer_t wr;
        size_t i, count = 0, n = sv->s_size;
        string_writer_init(&wr, 1);
        string_writer_appends(&wr, "set([");
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
        string_writer_appends(&wr, "])");
        return stringvar_from_writer(&wr);
}

static int
set_cmp(Object *a, Object *b)
{
#warning "not implemented"
        return 1;
}

static bool
set_cmpz(Object *set)
{
        return seqvar_size(set) == 0;
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
        Object *seq;
        if (vm_getargs(fr, "[<*>!]:set", &seq) == RES_ERROR)
                return ErrorVar;
        return setvar_new(seq);
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
        .mpunion = set_union_op,
};

static const struct type_inittbl_t set_cb_methods[] = {
        TBLEND,
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
        .cmp            = set_cmp,
        .cmpz           = set_cmpz,
        .reset          = set_reset,
        .prop_getsets   = NULL,
        .create         = set_create,
        .hash           = NULL,
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
 * Important!!  Only one reference will be produced by this function.
 * A non-NULL @unique is interpret as 'get' query, so...
 * 1. If @unique is NULL, a reference will be produced for @child, unless
 *    there was an error. Otherwise...
 * 2. If @unique does not match @child, a reference will be produced for
 *    @unique but not for child.
 * 3. If @unique does match @child, a refernece will be produced for
 *    @child, which just happens to be unique.
 */
enum result_t
set_additem(Object *set, Object *child, Object **unique)
{
        struct setvar_t *sv = (struct setvar_t *)set;
        int i;

        i = seek_helper(sv, child);
        if (i < 0) {
                err_setstr(KeyError, "%s key is not hashable");
                return RES_ERROR;
        }

        if (sv->s_keys[i] != NULL) {
                if (unique)
                        *unique = VAR_NEW_REF(sv->s_keys[i]);
                else
                        VAR_INCR_REF(child);
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
 * setvar_new - Create a new set
 * @seq: List to create set out of
 */
Object *
setvar_new(Object *seq)
{
        size_t i, n;
        Object *ret;

        if (!isvar_seq_readable(seq)) {
                err_setstr(TypeError, "create expects an iterable array");
                return ErrorVar;
        }
        ret = setvar_instantiate();
        n = seqvar_size(seq);
        for (i = 0; i < n; i++) {
                Object *child = seqvar_getitem(seq, i);
                bug_on(!child);
                if (set_additem(ret, child, NULL) == RES_ERROR) {
                        err_setstr(TypeError, "Cannot add unhashable %s",
                                   typestr(child));
                        VAR_DECR_REF(child);
                        VAR_DECR_REF(ret);
                        return ErrorVar;
                }
                VAR_DECR_REF(child);
        }
        return ret;
}


