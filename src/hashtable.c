/* Implementation of an EvilCandy-specific hash table. */
#include <lib/hashtable.h>
#include <evilcandy.h>
#include <stdlib.h>
#include <string.h>

#define GROW_SIZE(x)    (((x) * 2) / 3)
#define BUCKET_DEAD ((void *)-1)
/*
 * this initial size is small enough to not be a burden
 * but large enough that for 90% use-cases, no resizing
 * need occur.
 */
enum { INIT_SIZE = 16 };

static struct mempool_t *bucket_mempool = NULL;

static struct bucket_t *
bucket_alloc(void)
{
        if (!bucket_mempool)
                bucket_mempool = mempool_new(sizeof(struct bucket_t));
        return mempool_alloc(bucket_mempool);
}

static void
bucket_free(struct bucket_t *b)
{
        mempool_free(bucket_mempool, b);
}

static inline int
bucketi(struct hashtable_t *htbl, hash_t hash)
{
        return hash & (htbl->size - 1);
}


static struct bucket_t *
seek_helper(struct hashtable_t *htbl, const void *key,
                        hash_t hash, unsigned int *idx)
{
        unsigned int i = bucketi(htbl, hash);
        struct bucket_t *b;
        unsigned long perturb = hash;
        /* this won't spinlock because we ensure table has
         * enough room.  See comment below.
         */
        while ((b = htbl->bucket[i]) != NULL) {
                if (b != BUCKET_DEAD && htbl->key_match(b->key, key))
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
                i = bucketi(htbl, i * 5 + perturb + 1);
        }
        *idx = i;
        return b;
}

static void
transfer_table(struct hashtable_t *htbl, size_t old_size)
{
        int i, j, count;
        struct bucket_t **old, **new;
        old = htbl->bucket;
        htbl->bucket = new = ecalloc(sizeof(void *) * htbl->size);
        count = 0;
        for (i = 0; i < old_size; i++) {
                unsigned long perturb;
                struct bucket_t *b = old[i];
                if (!b || b == BUCKET_DEAD)
                        continue;

                perturb = b->hash;
                j = bucketi(htbl, b->hash);
                while (new[j]) {
                        perturb >>= 5;
                        j = bucketi(htbl, j * 5 + perturb + 1);
                }
                new[j] = b;
                count++;
        }
        htbl->count = htbl->used = count;
        free(old);
}

static void
refresh_grow_markers(struct hashtable_t *htbl)
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
        htbl->grow_size = (htbl->size << 1) / 3;
        htbl->shrink_size = htbl->used <= INIT_SIZE
                            ? 0: htbl->grow_size / 3;
}

static void
maybe_grow_table(struct hashtable_t *htbl)
{
        size_t old_size = htbl->size;
        while (htbl->count > htbl->grow_size) {
                /*
                 * size must always be a power of 2 or else the
                 * perturbation algo could spinlock.
                 */
                htbl->size *= 2;
                refresh_grow_markers(htbl);
        }

        if (htbl->size != old_size)
                transfer_table(htbl, old_size);
}

static void
maybe_shrink_table(struct hashtable_t *htbl)
{
        size_t old_size = htbl->size;
        while (htbl->used < htbl->shrink_size) {
                htbl->size /= 2;
                refresh_grow_markers(htbl);
        }

        if (htbl->size < INIT_SIZE)
                htbl->size = INIT_SIZE;

        if (htbl->size != old_size)
                transfer_table(htbl, old_size);
}

static inline void
insert_common(struct hashtable_t *htbl, void *key,
              void *data, hash_t hash, unsigned int i)
{
        struct bucket_t *b = bucket_alloc();
        b->key = key;
        b->data = data;
        b->hash = hash;
        htbl->bucket[i] = b;
        htbl->count++;
        htbl->used++;
        maybe_grow_table(htbl);
}

/**
 * hashtable_put - Add a new entry to a hash table
 * @htbl:       Hash table to add to
 * @key:        Key for new entry
 * @data:       Data for new entry
 *
 * Return: 0 if new entry was added, -1 if not due to an entry
 * already existing for @key.  If calling code wants to clobber
 * the old data, it will have to first call hashtable_remove().
 */
int
hashtable_put(struct hashtable_t *htbl, void *key, void *data)
{
        unsigned int i;
        hash_t hash = htbl->calc_hash(key);
        struct bucket_t *b = seek_helper(htbl, key, hash, &i);
        if (b)
                return -1;

        insert_common(htbl, key, data, hash, i);
        return 0;
}

/*
 *      Hack alert!!   Back door for literal.c code
 *
 * literal_put() would have a lot of unnecessary redundant steps
 * if it just used the hashtable_put|get API, so it needs some
 * special treatment, basically an alternative to hashtable_put().
 *
 * No one else should use this.
 */
char *
hashtable_put_literal(struct hashtable_t *htbl, const char *key)
{
        char *keycopy;
        unsigned int i;
        hash_t hash = htbl->calc_hash(key);
        struct bucket_t *b = seek_helper(htbl, key, hash, &i);
        if (b)
                return b->data;

        keycopy = estrdup(key);
        insert_common(htbl, keycopy, keycopy, hash, i);
        return keycopy;
}

/**
 * hashtable_get - Retrieve data from a hash table
 * @htbl:       Hash table to search
 * @key:        Key to the entry to get
 *
 * Return: pointer to data associated with @key, or NULL if
 * there is no entry for @key
 */
void *
hashtable_get(struct hashtable_t *htbl, const void *key)
{
        unsigned int i;
        hash_t hash = htbl->calc_hash(key);
        struct bucket_t *b = seek_helper(htbl, key, hash, &i);
        return b ? b->data : NULL;
}

/**
 * hashtable_remove - Delete an entry in a hash table
 * @htbl:       Hash table to remove entry from
 * @key:        Key of the entry to remove
 *
 * Return: Pointer to the data removed from the table, or NULL
 * if there was no entry for @key.  Unlike with
 * hashtable_clear_entries, the data itself has not been deleted.
 */
void *
hashtable_remove(struct hashtable_t *htbl, const void *key)
{
        unsigned int i;
        void *ret = NULL;
        hash_t hash = htbl->calc_hash(key);
        struct bucket_t *b = seek_helper(htbl, key, hash, &i);
        if (b) {
                ret = b->data;
                bucket_free(b);
                htbl->bucket[i] = BUCKET_DEAD;
        }
        htbl->used--;
        maybe_shrink_table(htbl);
        return ret;
}

/**
 * hashtable_init - Initialize a hash table
 * @htbl:       Hash table to initialize
 * @calc_hash:  Method for calculating the hash number
 * @key_match:  Method to compare two keys and return true if they match
 * @delete_data: Method delete data in table when calling
 *              hashtable_clear_entries()
 */
void
hashtable_init(struct hashtable_t *htbl,
                hash_t (*calc_hash)(const void *),
                bool (*key_match)(const void *, const void *),
                void (*delete_data)(void *))
{

        htbl->size = INIT_SIZE;
        htbl->used = 0;
        htbl->count = 0;
        refresh_grow_markers(htbl);
        htbl->bucket = ecalloc(sizeof(void *) * INIT_SIZE);
        htbl->calc_hash = calc_hash;
        htbl->key_match = key_match;
        htbl->delete_data = delete_data;
}

static void
hashtable_clear_entries_(struct hashtable_t *htbl)
{
        int i;
        /* not a full wipe, just get rid of entries */
        for (i = 0; i < htbl->size; i++) {
                if (htbl->bucket[i] == BUCKET_DEAD) {
                        htbl->bucket[i] = NULL;
                } else if (htbl->bucket[i] != NULL) {
                        htbl->delete_data(htbl->bucket[i]->data);
                        bucket_free(htbl->bucket[i]);
                        htbl->bucket[i] = NULL;
                }
        }
        htbl->count = htbl->used = 0;
}

/**
 * hashtable_clear_entries - Empty a hash table and clear all its data
 *
 * The deletions will use the @delete_data method passed to
 * hashtable_init().
 */
void
hashtable_clear_entries(struct hashtable_t *htbl)
{
        hashtable_clear_entries_(htbl);
        maybe_shrink_table(htbl);
}

/**
 * hashtable_destroy - Delete everything in a hash table, excpt for the
 *                     pointer to @htbl itself.
 *
 * Call this instead of hashtable_clear_entries() if the hash table is
 * never to be used again, eg. during a containing object's cleanup
 * method.
 */
void
hashtable_destroy(struct hashtable_t *htbl)
{
        hashtable_clear_entries_(htbl);
        free(htbl->bucket);
}

/**
 * hashtable_iterate - iterate through a hash table.
 * @htbl: Hash table to iterate through
 * @key:  Pointer to variable to get the key for the item
 * @val:  Pointer to get the value of the item
 * @context: Pointer to store the next item.  Must be set to
 *        zero on first call, treated as opaque thereafter.
 *
 * Return:
 * zero if @key and @val were updated, -1 if there are no more entries
 * in the hash table.
 *
 * This is not safe for calls to hashtable_put/hashtable_remove during
 * the iteration steps.  Calling code should use some kind of
 * reentrance lock to prevent that.
 *
 * None of the pointer arguments may be NULL
 */
int
hashtable_iterate(struct hashtable_t *htbl, void **key,
                  void **val, unsigned int *idx)
{
        unsigned int i = *idx;
        if (!htbl->bucket)
                return -1;
        while (i < htbl->size) {
                if (htbl->bucket[i] != NULL &&
                    htbl->bucket[i] != BUCKET_DEAD) {
                        break;
                }
                i++;
        }
        if (i >= htbl->size)
                return -1;

        *idx = i+1;
        *key = htbl->bucket[i]->key;
        *val = htbl->bucket[i]->data;
        return 0;
}

/*
 * Some hash and match algos useful for EvilCandy.
 *
 * These take advantage of the fact that literal() has already removed
 * duplicates at parse-time, and so lots of hash tables in this program
 * have matching pointers for matching strings.  For tables where this
 * isn't the case, use something like fnv_hash in helpers.c and a memcmp
 * for the matching algo.
 */

/* For array indexes, if we use this, I guess */
hash_t
idx_hash(const void *key)
{
        return (hash_t)(*(unsigned int *)key);
}

/*
 * Only use this if the @key used in the hashtable search algos are known
 * to be return values of literal(), eg. @key is a user token, otherwise
 * matching strings could be at different locations, and therefore get
 * different hashes.
 *
 * Rationale: "Just return the pointer" is less likely to put an entry
 * into a "random" spot in a table than a more involved hash algo, but
 * the 80/20 rule applies here: A more involved hash algo will only
 * marginally reduce the number of times we hit the collision algo in
 * seek_helper(), and that itself is not involved enough to be worth
 * trying to avoid; appropriate table resizing does a better job of
 * avoiding it anyway.
 */
hash_t
ptr_hash(const void *key)
{
        /*
         * A pointer to a known string in memory probably has a few
         * trailing zeros.  Shift those out, so we don't keep colliding
         * on our first modulo in the hash table.
         */
        enum {
                HASH_RSHIFT = 4,
                HASH_LSHIFT = (8 * sizeof(hash_t)) - HASH_RSHIFT
        };
        return ((hash_t)key >> HASH_RSHIFT) | ((hash_t)key << HASH_LSHIFT);
}

/*
 * fnv_hash - The FNV-1a hash algorithm
 *
 * See Wikipedia article "Fowler-Noll-Vo hash function"
 */
hash_t
fnv_hash(const void *key)
{
        /* 64-bit version */
#define FNV_PRIME      0x00000100000001B3LL
#define FNV_OFFSET     0xCBF29CE484222325LL

        const unsigned char *s = key;
        unsigned int c;
        unsigned long hash = FNV_PRIME;

        bug_on(sizeof(hash_t) != 8);

        /*
         * since C string has no zeros in the part that gets
         * hashed, don't worry about sticky state.
         */
        while ((c = *s++) != '\0')
                hash = (hash * FNV_OFFSET) ^ c;
        return (hash_t)hash;
}

/*
 * matching algo used when both keys are known to be return values
 * of literal(), forgoing need for a strcmp.
 */
bool
ptr_key_match(const void *key1, const void *key2)
{
        return key1 == key2;
}

bool
str_key_match(const void *key1, const void *key2)
{
        /* fast-path "==", since these still sometimes are literal()'d */
        return key1 == key2 || !strcmp((char *)key1, (char *)key2);
}


