/* q-literal.c - Code for handling string literals in script */
#include "egq.h"
#include <string.h>

/*
 * Wrapper to estrdup().
 * If literal is already saved, use the saved version instead.
 * DO NOT FREE THE RETURN VALUE! Call q_literal_free() instead.
 *
 * Rationale: a lot of mallocing and freeing of stack variable
 * names would be necessary without this, but I so often re-use
 * the same names over and over again that.
 * This is also a handy baby-step towards the process of byte-coding
 * the script parser.
 */
char *
q_literal(const char *s)
{
        char *ret = hashtable_get(q_.literals, s, NULL);
        if (!ret) {
                ret = estrdup(s);
                hashtable_put(q_.literals, ret, ret, 0, 0);
        }
        return ret;
}

void
q_literal_free(char *s)
{
        /*
         * Do nothing.  I _should_ store a reference counter with the
         * string in the hash table and delete it when the reference
         * counter goes down to zero, but that seems like bad practice.
         * What if a literal is used once, freed, then used again?
         * Then it would be better if I didn't have these wrappers at
         * all.
         */
}
