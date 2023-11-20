/* q-literal.c - Code for handling string literals in script */
#include "egq.h"
#include <string.h>

/*
 * Wrapper to estrdup().
 * If literal is already saved, use the saved version instead.
 * DO NOT FREE THE RETURN VALUE!
 *
 * Rationale: a lot of mallocing and freeing of stack variable
 * names would be necessary without this, but I so often re-use
 * the same names over and over again that.
 * This is also a handy baby-step towards the process of byte-coding
 * the script parser.
 */
char *
literal(const char *s)
{
        char *ret = hashtable_get(q_.literals, s, NULL);
        if (!ret) {
                ret = estrdup(s);
                hashtable_put(q_.literals, ret, ret, 0, 0);
        }
        return ret;
}

