/*
 * q-literal.c - Code for storing and retrieving persistent string
 *               literals
 *
 * literal_put(s)       copies s and puts it in a hash table, unless
 *                      the entry already exists.  Either way, it
 *                      returns the pointer stored in the hash table.
 * literal(s)           gets the stored copy of @s, or NULL if it isn't
 *                      found.
 *
 * This serves a few purposes:
 * 1. A script is likely going to repeat a lot of tokens (such as
 *    identifiers or literal expressions).  At prescan time, we save
 *    all of these tokens, but we don't want to fill up memory with
 *    duplicates.  So instead of calling strdup for every such token,
 *    we let literal_put() wrap that call, preventing the buildup of
 *    duplicates.
 * 2. Since the same pointer will be returned for matching strings, even
 *    if they're from different loaded scripts, certain lookups elsewhere
 *    can be reduced to ptr1 == ptr2 checks.
 * 3. Keeping track of how certain strings were allocated in what way,
 *    trying to figure out the garbage collection, trying to find ways to
 *    avoid excessive malloc/free calls, etc., is all so error-prone and
 *    so full of overhead, that it's better to just save them all and
 *    never delete them.  This library is a compromise, in that they stay
 *    in persistent memory but they don't get duplicated and zombified
 *    all over the place.
 *
 * Notes:
 * 1. When parsing prescanned byte code, do not call literal() for
 *    "cur_oc->s".  That is already a return value of literal(), so calling
 *    it again will be a redundant waste of compute cycles; you already got
 *    your answer.
 *
 * 2. Use the buffer.c API instead of literal() for strings that you intend
 *    to edit.
 *
 * 3. When building built-in attachments to the global object at init
 *    time, use literal_put() when setting variable names.  This should
 *    be the only time besides prescan() time when literal_put() is used
 *    instead of just literal().
 *
 * 4. Corrollary to note 1:
 *    Don't call literal() when searching for the 'that' of
 *
 *            this.that
 *
 *    DO call literal if it was expressed as
 *
 *            this['that']
 *
 *    even though 'that' is a hard-coded token, since array indexes
 *    between brackets are eval()'d and put in a struct buffer_t (*not*
 *    the same pointer as a literal() return value).  By that point, we
 *    won't know if it was written as a literal 'that' or as something
 *    dynamic like
 *
 *            this[x + y + z.something()]
 *
 *    where "x+y+z.something()" evaluates to 'that'.  (I probably should
 *    not even allow such a thing, but I do.)
 */
#include <evilcandy.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* For literal(), key is its own value */
struct lbucket_t {
        char *key;
        unsigned long hash;
};

/**
 * struct oai_hashtable_t -     "oai" stands for "open addressing,
 *                              insertion-only"
 * @size:       Array length of @bucket.  Always a power of 2
 * @count:      Current number of entries in the table
 * @bucket:     Array of entries
 * @grow_size:  Value of @count at which the table should grow
 */
struct oai_hashtable_t {
        size_t size;
        size_t count;
        struct lbucket_t **bucket;
        size_t grow_size;
};

static struct  oai_hashtable_t *htab;

static inline unsigned int
bucketi(unsigned long hash)
{
        return hash & (htab->size - 1);
}

static void
oai_grow(void)
{
        struct lbucket_t **b_old, **b_new;
        size_t old_size = htab->size;
        int i, j;

        htab->count++;
        while (htab->count > htab->grow_size) {
                /*
                 * XXX REVISIT: Arbitrary division done here.
                 * (x*3)>>2 is quicker, but alpha=75% is getting close
                 * to poor performance range for open-address tables,
                 * x>>1 (alpha=50%) is a lot of wasted real-estate,
                 * probably resulting in lots of cache misses, killing
                 * the advantage that open-address has over chaining.
                 * I'm assuming that amortization is reason enough not
                 * to care about any of this.
                 */
                htab->size *= 2;
                htab->grow_size = (htab->size << 1) / 3;
        }

        if (htab->size == old_size)
                return;

        /* resized, need new bucket array */
        b_old = htab->bucket;
        htab->bucket = b_new = ecalloc(sizeof(void *) * htab->size);
        for (i = 0; i < old_size; i++) {
                unsigned long perturb;
                struct lbucket_t *b = b_old[i];
                if (!b)
                        continue;
                perturb = b->hash;
                j = bucketi(b->hash);
                while (b_new[j]) {
                        perturb >>= 5;
                        j = bucketi(j * 5 + perturb + 1);
                }
                b_new[j] = b;
        }
        free(b_old);
}

/*
 * Note, we do open addressing, because everyone seems to think that's
 * faster than chaining.
 */
static struct lbucket_t *
seek_helper(const char *key, unsigned long hash, unsigned int *idx)
{
        unsigned int i = bucketi(hash);
        struct lbucket_t *b;
        unsigned long perturb = hash;
        while ((b = htab->bucket[i]) != NULL) {
                if (b->hash == hash && !strcmp(b->key, key)) {
                        *idx = i;
                        return b;
                }

                /*
                 * Way to cope with a power-of-2-sized hash table, esp.
                 * an open-address one.
                 *
                 * Idea & algo taken from Python.  See cpython source
                 * code, "Object/dictobject.c" at
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
                 *    it turns out to be true.)
                 */
                perturb >>= 5;
                i = bucketi(i * 5 + perturb + 1);
        }
        *idx = i;
        return NULL;
}

/**
 * literal_put - Store key if it isn't stored already
 * @key: Text to store
 *
 * Return: copied version of key
 */
char *
literal_put(const char *key)
{
        unsigned int i;
        unsigned long hash = fnv_hash(key);
        struct lbucket_t *b = seek_helper(key, hash, &i);
        /* if match, don't insert, return that */
        if (!b) {
                /* no match, insert */
                b = emalloc(sizeof(*b));
                b->hash = hash;
                b->key = strdup(key);
                htab->bucket[i] = b;
                oai_grow();
        }

        return b->key;
}

char *
literal(const char *key)
{
        unsigned int dummy;
        unsigned long hash = fnv_hash(key);
        struct lbucket_t *b = seek_helper(key, hash, &dummy);
        return b ? b->key : NULL;
}

void
moduleinit_literal(void)
{
        htab = emalloc(sizeof(*htab));
        htab->grow_size = 0;
        htab->count = 0;
        htab->size = 2;
        htab->bucket = ecalloc(sizeof(void *) * htab->size);
}

