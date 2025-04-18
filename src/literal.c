/*
 * literal.c - Code for storing and retrieving persistent string literals
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
 * Usage notes:
 *
 * 1. After things are up and running, generally, tokens and associative
 *    array member names have been passed to literal_put(), while most
 *    TYPE_STRING variables have not, and should for safety's sake be
 *    treated as if they have not.
 *
 * 2. Use the buffer.c API instead of literal() for strings that you intend
 *    to edit.
 *
 * 3. Careful when getting user-expressed names for associative arrays.
 *    The 'that' of the following expressions:
 *            this.that
 *            this['that']
 *    are token strings that have been literal'd.  But in the latter case,
 *    by the time we dereference it, we no longer know if 'that' was
 *    expressed as this literal or as something like
 *            x + y + z.something()
 *    which evaluates to 'that' and would have been put in a TYPE_STRING
 *    user-variable; see 1. above, these are not necessarily literal()'d.
 *
 * XXX: Some testing should be done for the average speed of matching
 * when one string has been literal()'d and one has not.  Is the
 * following...
 *              return !strcmp(s1, s2);
 * faster on average than...
 *              s1 = literal(s1);
 *              return s1 == s2;
 */
#include <evilcandy.h>

static struct var_t *literal_dict__ = NULL;

void
moduleinit_literal(void)
{
        literal_dict__ = objectvar_new();
}

char *
literal_put(const char *key)
{
        return object_unique(literal_dict__, key);
}

char *
literal(const char *key)
{
        struct var_t *vs = object_getattr(literal_dict__, key);
        if (!vs)
                return NULL;
        return string_get_cstring(vs);
}

