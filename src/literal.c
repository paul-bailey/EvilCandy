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
 */
#include "egq.h"
#include <string.h>
#include <stdio.h>

/**
 * literal - Get the "official" copy of a string
 * @s: String to get a copy of
 *
 * Return:      Copy of @s (which might be @s exactly if @s is a previous
 *              return value from this function).
 *
 * Warning!     DO NOT FREE THE RETURN VALUE!
 *              DO NOT EDIT THE RETURN VALUE!
 */

/*
 * TODO: When enough testing has been done for this, pick one and remove
 * the macro.
 *
 * Reson why HASHTABLE:
 *      This is larger than any other collection of strings, and
 *      hashtables are more memory-efficient, even with bitwise tries.
 *
 * Reason why TRIE:
 *      This is used more often than the other hashtables, and tries
 *      are probably faster, even bitwise ones, since there are no
 *      collisions.
 */
#define TRIE_IT 0

#if TRIE_IT

static struct trie_t *lit_trie;

char *
literal(const char *s)
{
        char *ret = trie_get(lit_trie, s);
        if (!ret) {
                int sts;
                ret = estrdup(s);
                sts = trie_insert(lit_trie, ret, (void *)ret, false);
                bug_on(sts < 0);
                (void)sts; /* in case NDEBUG, else compiler gripes */
        }
        return ret;
}

/* for debugging */
void
literal_diag(void)
{
        size_t size = trie_size(lit_trie);
        printf("Size of lit_trie is %lu\n", (unsigned long)size);
}

void
moduleinit_literal(void)
{
        lit_trie = trie_new();
}

#else /* !TRIE_IT */
#include "hashtable.h"

static struct hashtable_t *literal_htbl = NULL;

char *
literal(const char *s)
{
        char *ret = hashtable_get(literal_htbl, s, NULL);
        if (!ret) {
                ret = estrdup(s);
                if (hashtable_put(literal_htbl, ret, ret, 0, 0) < 0)
                        fail("OOM");
        }
        return ret;
}

/* for debugging */
void
literal_diag(void)
{
        printf("Literal Hash Table Diagnostics:\n");
        hashtable_diag(literal_htbl);
}

void
moduleinit_literal(void)
{
        literal_htbl = hashtable_create(0, NULL);
        if (!literal_htbl)
                fail("hashtable_create failed");
}

#endif /* !TRIE_IT */
