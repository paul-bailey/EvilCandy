/* q-literal.c - Code for handling string literals in script */
#include "egq.h"
#include "hashtable.h"
#include <string.h>

/*
 * XXX REVISIT: Is a trie a better option here?
 *
 * Reson why NO:
 *      This is larger than any other collection of strings, and
 *      hashtables are more memory-efficient
 *
 * Reason why YES:
 *      This is used more often than the other hashtables, and tries
 *      are definitely faster, given how small most strings will be,
 *      therefore how frequent the collisions will be.
 */
static struct hashtable_t *literal_htbl = NULL;

/**
 * literal - Get a copy of a string that can be ignored w/r/t garbage
 *           collection, and which you can trust will persist in memory
 * @s: String to get a copy of
 *
 * Return:      Copy of @s (which might be @s exactly if @s is a previous
 *              return value from this function).
 *
 * Warning!     DO NOT FREE THE RETURN VALUE!
 *              DO NOT EDIT THE RETURN VALUE!
 *
 * Rationale:   Keeping track of how certain strings were allocated in
 *              what way, trying to determine if it's safe to free them,
 *              trying to find ways to avoid excessive malloc/free calls,
 *              etc., is all so error-prone and so full of overhead, that
 *              it's better to just save them all and never delete them.
 *
 *              This also means that all tokens of all loaded scripts
 *              that match each other will have the same pointer, so
 *              certain lookups are pointer comparisons rather than
 *              string compares.
 *
 * Operation:   To allay RAM from run-amok duplicates (eg. keywords and
 *              symbol names which will certainly appear more than once
 *              in the script), literals are stored in a hash table.  If
 *              there is already a match for @s, the match is returned.
 *              Otherwise @s will be strdup'd, and the duped version will
 *              be stored in the hash table and returned.
 *
 * Notes:       Do not call this for "cur_oc->s".  That is already a
 *              return value of this function, so calling it again will
 *              be a redundant waste of compute cycles complete with the
 *              overhead of a hash table lookup.
 *
 *              Use the buffer.c API instead of this for strings that you
 *              want to edit.  For that matter, do not call literal on a
 *              string-type variable's string buffer unless you have to,
 *              e.g. it is a dynamically-produced associate-array key,
 *              therefore you need to check builtin_method() with it.
 */
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

void
moduleinit_literal(void)
{
        literal_htbl = hashtable_create(0, NULL);
        if (!literal_htbl)
                fail("hashtable_create failed");
}

