/*
 * q-literal.c - Code for storing and retrieving persistent string
 *               literals
 *
 * literal() gets a copy of its argument from a hash table (or bitwise
 * trie if TRIE_IT is set).  If a copy is found, that copy is returned.
 * Otherwise a copy is made and stored in the hash table.
 *
 * This serves a few purposes:
 * 1. Duplicates of the same string are prevented from being stored in
 *    persistent memory, since they're wrapped by literal()
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
 * 1. When executing byte code, do not call literal() for "cur_oc->s".
 *    That is already a return value of literal(), so calling it again
 *    will be a redundant waste of compute cycles.
 * 2. Use the buffer.c API instead of this for strings that you want to
 *    edit.  For that matter, do not call literal on a string-type
 *    variable's string buffer unless you have to, e.g. it is a
 *    dynamically-produced associate-array key, therefore you need to
 *    check builtin_method() with it.
 * 3. This is not for the cryptographically squeamish.  Don't use this
 *    for your private diary.
 * 4. Corrollary to notes 1 & 2:
 *    Don't call literal for
 *            this.that...
 *    DO call literal for
 *            this['that']...
 *    even though "'that'" is hard-coded, since the way we evaluate
 *    it involves putting it in a temporary struct var_t.  At the end
 *    of evaluation we won't know if it was written as above or as
 *    something like
 *            this[x+y+z.something()]...
 *    where "x+y+z.something()" evaluate to "'that'"
 *
 * FIXME: Note 4 implies we should forgo this altogether in favor of
 *
 */
#include "egq.h"
#include <string.h>
#include <stdio.h>


/* For literal(), key is its own value */
struct bucket_t {
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
        struct bucket_t **bucket;
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
        struct bucket_t **b_old, **b_new;
        size_t old_size = htab->size;
        int i, j;

        htab->count++;
        while (htab->count > htab->grow_size) {
                /*
                 * XXX REVISIT: Arbitrary division done here.
                 * (x*3)>>2 is quicker, but alpha=75% is getting close
                 * to the poor performance for open-address tables,
                 * and alpha=50% is a lot of wasted real-estate,
                 * probably resulting in lots of cache misses, killing
                 * the advantage that open-address has over chaining.
                 * I'm assuming that amortization is reason enough not
                 * to care about this.
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
                struct bucket_t *b = b_old[i];
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

}

/*
 * Note, we do open addressing, because everyone seems to think that's
 * faster than chaining.
 */
static struct bucket_t *
seek_helper(const char *key, unsigned long hash, unsigned int *idx)
{
        unsigned int i = bucketi(hash);
        struct bucket_t *b;
        unsigned long perturb = hash;
        while ((b = htab->bucket[i]) != NULL) {
                if (b->hash == hash && !strcmp(b->key, key)) {
                        *idx = i;
                        return b;
                }

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
        struct bucket_t *b = seek_helper(key, hash, &i);
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
        struct bucket_t *b = seek_helper(key, hash, &dummy);
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

