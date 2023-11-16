/*
 * hashtable.c - Simple, most-cases version of a hash table
 *
 * This has very few tunable parameters, and is designed for just the
 * most commonplace usages:
 *    - it assumes that keys are always C strings
 *    - it cannot serialize out to a file; ie. it assumes that the
 *      program does not need to save the data when exiting.
 *    - likewise, it was not designed to work well with IPC.
 *    - it does not have any thread-safe locks
 *    - if HTBL_COPY_DATA is used, it assumes that any data being
 *      stored with the table may have the same alignment as
 *      long long int.
 *
 * This library is only useful for large quantities of key/value pairs.
 * For smaller amounts (say, a few dozen), it's just as well to store
 * key/value/hash-number sets in linked lists.
 *
 * Copyright (C) 2016 Paul Bailey <baileyp2012@gmail.com>
 */
#include "hashtable.h"
#include <string.h>
#include <stdlib.h>

/**
 * struct hashtable_t - Top-level structure of hash table
 * @size:       Array length of @bucket.  Always a power of 2
 *              (prime numbers are for eggheads, don't @ me!).
 * @count:      Current number of entries in the table
 * @flags:      User parameters
 * @bucket:     Array of entries
 * @algo:       User-selected algorithm to calculate hash
 * @grow_size:  Value of @count at which the table should grow
 * @shrink_size: Value of @count at which the table should shrink
 */
struct hashtable_t {
        size_t size;
        size_t count;
        unsigned int flags;
        struct bucket_t **bucket;
        unsigned long (*algo)(const char *);
        size_t grow_size;
        size_t shrink_size;
};

enum {
        HTBL_INITIAL_SIZE = 64,
};

/*
 * fnv_hash - The FNV-1a hash algorithm, our default if user
 *              does not select their own.
 * See Wikipedia article on this.
 * It could be made into 64-bit version with different consts.
 * Users may want to make a case-less version of this, for
 * things like case-insensitive databases.
 */
static unsigned long
fnv_hash(const char *s)
{
        unsigned int c;
        unsigned long hash = 0x811c9dc5;
        while ((c = (unsigned char)(*s++)) != '\0')
                hash = (hash * 0x01000193) ^ c;
        return hash;
}

static inline unsigned long
calc_hash(struct hashtable_t *tbl, const char *s)
{
        return tbl->algo ? tbl->algo(s) : fnv_hash(s);
}

static inline int bucketi(struct hashtable_t *tbl, unsigned long hashno)
        { return hashno & (tbl->size - 1); }

/* Update tbl->grow_size and tbl->shrink_size */
static void
refresh_grow_markers(struct hashtable_t *tbl)
{
        tbl->grow_size = tbl->size * 2 + 1;
        /*
         * Careful! Don't allow sweet spots where the table
         * keeps growing and shrinking back and forth.
         * The shrink size cannot just be the old grow size.
         * Instead it's new grow size divided by 3.
         */
        tbl->shrink_size = tbl->size <= HTBL_INITIAL_SIZE
                           ? 0 : tbl->grow_size / 3;
}

static int
hashtable_init(struct hashtable_t *tbl, unsigned int flags,
               unsigned long (*algo)(const char *))
{
        size_t nalloc;

        memset(tbl, 0, sizeof(*tbl));
        tbl->flags = flags;
        tbl->algo = algo;
        tbl->size = HTBL_INITIAL_SIZE;
        nalloc = tbl->size * sizeof(*tbl->bucket);
        tbl->bucket = malloc(nalloc);
        if (!tbl->bucket)
                return -1;
        memset(tbl->bucket, 0, nalloc);
        refresh_grow_markers(tbl);
        return 0;
}

static void
free_bucket(struct bucket_t **bucket, unsigned int size)
{
        unsigned int i;
        for (i = 0; i < size; i++) {
                struct bucket_t *b = bucket[i];
                while (b != NULL) {
                        struct bucket_t *tmp = b->next;
                        free(b);
                        b = tmp;
                }
        }
        free(bucket);
}

/*
 * Helper to hashtable_put: replace @old with @new in the
 * collision list and de-allocate @old.
 * If returning -1, this is a BUG.
 */
static int
insert_replacement(struct hashtable_t *tbl, int idx,
                struct bucket_t *new, struct bucket_t *old)
{
        struct bucket_t *p = tbl->bucket[idx];
        if (!p)
                return -1;

        new->next = old->next;
        if (p == old) {
                tbl->bucket[idx] = new;
        } else {
                while (p->next != NULL) {
                        if (p->next == old) {
                                p->next = new;
                                goto done;
                        }
                        p = p->next;
                }
                return -1;
        }
done:
        free(old);
        return 0;
}

/* Grow/shrink the hash table if it's getting too full/empty */
static int
maybe_resize_table(struct hashtable_t *tbl)
{
        size_t i, nalloc, old_size, newsize;
        struct bucket_t **old_bucket, **new_bucket;

        if (tbl->count >= tbl->grow_size)
                newsize = tbl->size * 2;
        else if (tbl->count <= tbl->shrink_size)
                newsize = tbl->size / 2;
        else
                return 0;

        if (newsize < HTBL_INITIAL_SIZE)
                newsize = HTBL_INITIAL_SIZE;

        nalloc = newsize * sizeof(void *);
        new_bucket = malloc(nalloc);
        if (!new_bucket)
                return -1;
        memset(new_bucket, 0, nalloc);

        old_size = tbl->size;
        old_bucket = tbl->bucket;
        tbl->bucket = new_bucket;
        tbl->size = newsize;
        refresh_grow_markers(tbl);
        for (i = 0; i < old_size; i++) {
                struct bucket_t *b = old_bucket[i];
                while (b != NULL) {
                        struct bucket_t *tmp = b->next;
                        size_t new_i = bucketi(tbl, b->hash);
                        b->next = tbl->bucket[new_i];
                        tbl->bucket[new_i] = b;
                        b = tmp;
                }
        }
        free(old_bucket);
        return 0;
}

/* Helper to find_entry - hashno already calc'd */
static struct bucket_t *
find_entry_helper(struct hashtable_t *tbl, const char *key,
                        unsigned int *i, unsigned long hashno)
{
        unsigned int idx = bucketi(tbl, hashno);
        struct bucket_t *b = tbl->bucket[idx];
        *i = idx;
        while (b != NULL) {
                if (b->hash == hashno && !strcmp(b->key, key))
                        break;
                b = b->next;
        }
        return b;
}

static struct bucket_t *
find_entry(struct hashtable_t *tbl, const char *key, unsigned int *i)
{
        return find_entry_helper(tbl, key, i, calc_hash(tbl, key));
}

/**
 * hashtable_put - Put a new entry into the hash table
 * @tbl: Hash table
 * @key: Key of the key/value pair.  If the table was created with
 *      HTBL_COPY_KEY set, then a copy of @key will be stored with
 *      the hash table.  If not, then you are expected to maintain
 *      (without changing) this exact pointer.
 * @data: Data associated with @key.  If the table was created with
 *      HTBL_COPY_DATA (not recommeneded), then copy this data into
 *      the hash table.
 * @datalen: Length of data. Necessary if HTBL_COPY_DATA is set or
 *      if you'll need this info from hashtable_get() later.
 *      Don't-care otherwise.
 * @flags: If HTBL_CLOBBER, then if some data for @key is already
 *      being stored, replace it with @data.  Otherwise, fail if
 *      new data for @key already exists.  (Keep unset if
 *      HTBL_COPY_DATA was not set during hashtable_create; see note
 *      below.)
 *
 * Return: -1 if error, 0 if success.  If ENOMEM is not set, then the
 *      error is due one of the following:
 *      - HTBL_UBUCKET is set.  Call hashtable_put_bucket instead.
 *      - @key is already stored and HTBL_CLOBBER is not set.
 *
 * Note: Using HTBL_CLOBBER for a table where HTBL_COPY_DATA was not
 *      set has the potential to zombify data and therefore leak memory.
 *      Instead, wrap your call to hashtable_put() with the following
 *      subroutine:
 *
 *      (1) Call hashtable_put() without HTBL_CLOBBER.
 *          If it succeeds, you're done.
 *          If it fails and ENOMEM is not set, continue...
 *      (2) Call hashtable_get() and save return value.
 *      (3) Call hashtable_remove().
 *      (4) Properly free or clean up the return value from (2).
 *      (5) Try hashtable_put() again.
 */
int
hashtable_put(struct hashtable_t *tbl, const char *key,
                void *data, size_t datalen, unsigned int flags)
{
        unsigned int i;
        unsigned long hashno;
        size_t nalloc;
        struct bucket_t *b, *bsave;

        if (!!(tbl->flags & HTBL_UBUCKET))
                return -1;

        hashno = calc_hash(tbl, key);
        b = find_entry_helper(tbl, key, &i, hashno);
        if (b) {
                if (!(flags & HTBL_CLOBBER))
                        return -1;

                if (!(tbl->flags & HTBL_COPY_DATA)) {
                        /* clobber fast path: just update pointer */
                        b->data = data;
                        return 0;
                }
        }
        bsave = b;

        /*
         * Allocate just one block of memory, regardless of
         * flags.  This way cleanup is always just `free(b),'
         * and it reduces malloc()'s memory bookkeeping.
         *
         * FIXME: This is still a lot of memory overhead,
         * because it calls alloc() for every bucket entry.
         * In other circumstances we could reduce this by
         * allocating pool arrays of this struct at a time,
         * but we cannot do that here because of the
         * HTBL_COPY_... flags, making their size be variable.
         */
        nalloc = sizeof(*b);
        if (!!(tbl->flags & HTBL_COPY_DATA))
                nalloc += datalen;
        if (!!(tbl->flags & HTBL_COPY_KEY))
                nalloc += strlen(key) + 1;

        b = malloc(nalloc);
        if (!b)
                return -1;

        b->datalen = datalen;
        b->hash = hashno;
        switch (tbl->flags & (HTBL_COPY_DATA | HTBL_COPY_KEY)) {
        default:
        case 0:
                b->key = (char *)key;
                b->data = data;
                break;
        case HTBL_COPY_DATA:
                b->key = (char *)key;
                b->data = (void *)(&b[1]);
                memcpy(b->data, data, datalen);
                break;
        case HTBL_COPY_KEY:
                b->data = data;
                b->key = (void *)(&b[1]);
                strcpy(b->key, key);
                break;
        case HTBL_COPY_DATA | HTBL_COPY_KEY:
                b->data = (void *)(&b[1]);
                b->key = (char *)b->data + datalen;
                memcpy(b->data, data, datalen);
                strcpy(b->key, key);
                break;
        }

        if (bsave) {
                /* Clobber old entry */
                return insert_replacement(tbl, i, b, bsave);
        } else {
                /* Added new entry */
                b->next = tbl->bucket[i];
                tbl->bucket[i] = b;
                tbl->count++;
                maybe_resize_table(tbl);
        }
        return 0;
}

/**
 * hashtable_put_bucket - HTBL_UBUCKET version of hashtable_put
 * @tbl: Hash table.
 * @bucket: Bucket to install.  User sets .key, .data, and (if the
 *      user would lose this info otherwise) .datalen fields.
 *      This library will maintain the other fields.  @bucket must
 *      remain in memory for as long as you intend to be able to access
 *      it via this library; I.E. a copy will not be made here.
 *
 * Return: Zero if success, -1 if error.  Errors include @key already
 *      existing in the table, or HTBL_UBUCKET flags not being set in
 *      the table.
 *
 * TODO: HTBL_CLOBBER not supported for this function yet.  It would
 *      result in a memory leak.
 */
int
hashtable_put_bucket(struct hashtable_t *tbl, struct bucket_t *bucket)
{
        unsigned int i;
        unsigned long hashno;
        struct bucket_t *b, *bsave = NULL;

        if (!(tbl->flags & HTBL_UBUCKET))
                return -1;

        hashno = calc_hash(tbl, bucket->key);
        b = find_entry_helper(tbl, bucket->key, &i, hashno);
        if (b) {
                if (!(tbl->flags & HTBL_CLOBBER))
                        return -1;

                /*
                 * TODO: Fall through so that insert_replacement
                 * below can be called.  Right now we can't do
                 * that, because there's no way hand old @b back
                 * to user for RAM cleanup.
                 */
                return -1;
        }
        bsave = b;

        /* .data, .key, and .datalen already set by user */

        bucket->hash = hashno;
        if (bsave) {
                /* Clobber old entry */
                return insert_replacement(tbl, i, bucket, bsave);
        } else {
                /* Add new entry */
                bucket->next = tbl->bucket[i];
                tbl->bucket[i] = bucket;
                tbl->count++;
                maybe_resize_table(tbl);
        }
        return 0;
}

/**
 * hashtable_get - Get an entry from the hash table
 * @tbl: Hash table
 * @key: Key to look up
 * @len: If not NULL, the length of found data will be written here
 *
 * Return: Pointer to new data, or NULL if no match found.
 */
void *
hashtable_get(struct hashtable_t *tbl, const char *key, size_t *len)
{
        unsigned int dummy;
        struct bucket_t *b = find_entry(tbl, key, &dummy);
        if (!b)
                return NULL;
        if (len)
                *len = b->datalen;
        return b->data;
}

/**
 * hashtable_getbucket - Get an entry from the hash table
 * @tbl: Hash table
 * @key: Key to look up.
 *
 * Return: Struct bucket_t handle to the data.
 */
struct bucket_t *
hashtable_get_bucket(struct hashtable_t *tbl, const char *key)
{
        unsigned int dummy = 0;
        return find_entry(tbl, key, &dummy);
}

/**
 * hashtable_remove - Remove an entry from the hash table
 * @tbl: Hash table
 * @key: Key to look up for removal
 *
 * Return: true if item removed, false if item not found for removal
 * If return value is true, also check for ENOMEM, in case the table
 * needed to resize but couldn't.
 *
 * Warning! If HTBL_COPY_DATA not set, data could be zombified by this.
 * If you have the key but lost any other way to access the data, then
 * the proper use of this is:
 *
 *      (1) call hashtable_get(key) and save pointer
 *      (2) call hahstable_remove(key)
 *      (3) properly clean up data returned from (1)
 */
bool
hashtable_remove(struct hashtable_t *tbl, const char *key)
{
        unsigned int idx;
        struct bucket_t *b = find_entry(tbl, key, &idx);
        if (b != NULL) {
                if (tbl->bucket[idx] == b) {
                        tbl->bucket[idx] = b->next;
                } else {
                        struct bucket_t *p = tbl->bucket[idx];
                        while (p->next != NULL) {
                                if (p->next == b) {
                                        p->next = b->next;
                                        break;
                                }
                                p = p->next;
                        }
                }
                free(b);
                tbl->count--;
                maybe_resize_table(tbl);
                return true;
        }
        return false;
}

/**
 * hashtable_free - Clean up and free a hash table.
 * @tbl: Table to free.  Do not use this pointer again after
 *      returning from this call.
 * @cleanup: Callback to clean up private data, or NULL to not use.  This
 *      is in case one of HTBL_COPY_KEY or HTBL_COPY_DATA is not set, and
 *      you have no way of accessing the data to clean it up except
 *      through the hashtable functions.  If HTBL_COPY_KEY is set, then
 *      the first argument is NULL, else it's the key.  If HTBL_COPY_DATA
 *      is set, then the second argument is NULL, else it's the data.
 */
void
hashtable_free(struct hashtable_t *tbl,
                void (*cleanup)(char *, void *))
{
        if (cleanup != NULL
            && !(tbl->flags & (HTBL_COPY_KEY | HTBL_COPY_DATA))) {
                int i;
                bool kcopy = !!(tbl->flags & HTBL_COPY_KEY);
                bool dcopy = !!(tbl->flags & HTBL_COPY_DATA);
                for (i = 0; i < tbl->size; i++) {
                        struct bucket_t *b = tbl->bucket[i];
                        while (b != NULL) {
                                struct bucket_t *tmp = b->next;
                                cleanup(kcopy ? NULL : (char *)b->key,
                                        dcopy ? NULL : b->data);
                                b = tmp;
                        }
                }
        }
        free_bucket(tbl->bucket, tbl->size);
        tbl->bucket = NULL;
        tbl->count = tbl->size = 0;
        free(tbl);
}

/**
 * hashtable_create - Create and initialize a hashtable struct
 * @flags: Bitmask of HTBL_... parameters; see note below.
 * @algo: If not NULL, use this hash function instead of the built-in
 *      default (the FNV-1a algorithm).
 *
 * Return: Newly created hash table, or NULL if out of memory.
 *
 * If HTBL_COPY_KEY is set in @flags, then a copy of the @key
 * argument to hashtable_put() will be made and stored with
 * the hash table.  This may be useful if user code cannot easily
 * maintain all its known keys, for example if some @key arguments
 * are literals while others are declared on the stack, or if the
 * @key arguments would have to be dynamically allocated and later
 * freed.  In the last case, this library has slightly improved
 * memory allocation for that.
 *
 * If HTBL_COPY_DATA is set in @flags, then a copy of the @data
 * argument to hashtable_put() will be made and stored with
 * the hash table.  Later calls to hashtable_get() will return
 * the copy rather than the original.
 *
 * If HTBL_UBUCKET is set in @flags, then disregard the other flags
 * altogether and assume that user is declaring and managing its own
 * bucket_t structs.
 */
struct hashtable_t *
hashtable_create(unsigned int flags, unsigned long (*algo)(const char *))
{
        struct hashtable_t *ret = malloc(sizeof(*ret));
        if (ret)
                hashtable_init(ret, flags, algo);
        return ret;
}

/**
 * hashtable_for_each - Act on every item in a hash table.
 * @tbl: Hash table
 * @action: Callback to perform the action.  If it returns any value
 *      other than zero, hashtable_for_each() will quit early.
 *
 * Return: Last return value of @cb before quitting, or zero if
 *      the callback completed for every item stored in @tbl
 *
 * This may be handy for things like serializing the data to a file.
 */
int
hashtable_for_each(struct hashtable_t *tbl,
                int (*action)(const char *, void *, size_t))
{
        size_t i;
        for (i = 0; i < tbl->size; i++) {
                struct bucket_t *b = tbl->bucket[i];
                while (b != NULL) {
                        /*
                         * Use intermediate variable, in case @cb
                         * will call hashtable_remove(key).  Not
                         * sure why someone would do that this way,
                         * but I won't judge.
                         */
                        struct bucket_t *tmp = b->next;
                        int res = action(b->key, b->data, b->datalen);
                        if (res != 0)
                                return res;
                        b = tmp;
                }
        }
        return 0;
}

/**
 * hashtable_for_each_bucket - Like hashtable_for_each, but meant for
 *              hashtables created with HTBL_UBUCKET.
 */
int
hashtable_for_each_bucket(struct hashtable_t *tbl,
                int (*action)(struct bucket_t *))
{
        size_t i;
        for (i = 0; i < tbl->size; i++) {
                struct bucket_t *b = tbl->bucket[i];
                while (b != NULL) {
                        /* Cf. comment in hashtable_for_each() */
                        struct bucket_t *tmp = b->next;
                        int res = action(b);
                        if (res != 0)
                                return res;
                        b = tmp;
                }
        }
        return 0;
}

#ifdef TEST_HASHTABLE__
# include <time.h>
void
hashtable_diag(const struct test_data_t *test,
               struct hashtable_t *tbl, FILE *fp)
{
        int i;
        int collisions = 0;
        int max_collisions = 0;
        for (i = 0; i < tbl->size; i++) {
                int these_collisions = 0;
                struct bucket_t *b = tbl->bucket[i];
                if (!b)
                        continue;
                while (b->next != NULL) {
                        ++these_collisions;
                        ++collisions;
                        b = b->next;
                }
                if (these_collisions > max_collisions)
                        max_collisions = these_collisions;
        }
        fprintf(fp, "   %d collisions out of %d entries\n",
                collisions, (int)tbl->count);
        fprintf(fp, "   Longest collision list has %d collisions\n",
                max_collisions);
        fprintf(fp, "   Table size is %d\n", (int)tbl->size);
        if (test->num_lookups > 0) {
                double max = test->max_lookup_time * 1e6;
                double sum = test->sum_lookup_time * 1e6;
                double ave = sum / (double)test->num_lookups;
                fprintf(fp, "    Lookup times out of %d times:\n",
                        test->num_lookups);
                fprintf(fp, "    Max: %G microsec\n", max);
                fprintf(fp, "    Ave: %G microsec\n", ave);
                fprintf(fp, "    Precision: %G microsec\n",
                        1e6 / (double)CLOCKS_PER_SEC);
        }
}

#endif /* TEST_HASHTABLE__ */

