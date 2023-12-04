/* Implementation of an egq-specific hash table. */
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
         * enough room.  See comment in literal.c about
         * why the perturbation algo won't spinlock.
         */
        while ((b = htbl->bucket[i]) != NULL) {
                if (b != BUCKET_DEAD && htbl->key_match(b->key, key))
                        break;
                /*
                 * Collision or dead entry.
                 * See big comment in seek_helper in literal.c
                 * I'm doing the same thing here.
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
        htbl->grow_size = (htbl->size << 1) / 3;
        htbl->shrink_size = htbl->used <= INIT_SIZE
                            ? 0: htbl->grow_size / 3;
}

static void
maybe_grow_table(struct hashtable_t *htbl)
{
        size_t old_size = htbl->size;
        while (htbl->count > htbl->grow_size) {
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

int
hashtable_put(struct hashtable_t *htbl, void *key, void *data)
{
        unsigned int i;
        hash_t hash = htbl->calc_hash(key);
        struct bucket_t *b = seek_helper(htbl, key, hash, &i);
        if (b)
                return -1;

        b = bucket_alloc();
        b->key = key;
        b->data = data;
        b->hash = hash;
        htbl->bucket[i] = b;
        htbl->count++;
        htbl->used++;
        maybe_grow_table(htbl);
        return 0;
}

void *
hashtable_get(struct hashtable_t *htbl, const void *key)
{
        unsigned int i;
        hash_t hash = htbl->calc_hash(key);
        struct bucket_t *b = seek_helper(htbl, key, hash, &i);
        return b ? b->data : NULL;
}

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

void
hashtable_clear_entries(struct hashtable_t *htbl)
{
        hashtable_clear_entries_(htbl);
        maybe_shrink_table(htbl);
}

/* call only when the containing object will be destroyed */
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

hash_t
idx_hash(const void *key)
{
        /* For array indexes, if we use this, I guess */
        return (hash_t)(*(unsigned int *)key);
}

hash_t
ptr_hash(const void *key)
{
        /*
         * A pointer to a known string in memory probably has a few
         * trailing zeros.  Shift those out, so we don't keep colliding
         * on our first modulo in the hash table.
         */
        enum { HASH_RSHIFT = (8 * sizeof(hash_t)) - 4 };
        return ((hash_t)key >> 4) | ((hash_t)key << HASH_RSHIFT);
}

bool
ptr_key_match(const void *key1, const void *key2)
{
        return key1 == key2;
}

