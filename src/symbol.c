/* symbol.c - Code that looks for symbol from input */
#include "egq.h"
#include <string.h>

/* Helper to symbol_seek - look in stack */
static struct var_t *
trystack(const char *s)
{
        /* Args actually begin 1st after FP */
        struct var_t *p;
        for (p = q_.fp + 1; p < q_.sp; p++) {
                if (p->name && !strcmp(p->name, s))
                        return p;
        }
        return NULL;
}

/* Helper to symbol_seek - walk up namespace */
static struct var_t *
trythis(const char *s)
{
        struct var_t *v, *o = get_this();

        if (!o || o->magic != QOBJECT_MAGIC) {
                /*
                 * FIXME: Is this a bug? if get_this() is not an object,
                 * we should be in a built-in function, which would not
                 * call symbol_seek().
                 */
                o = q_.gbl;
        }

        /*
         * FIXME: Some objects don't trace all the way up to q_.gbl,
         * and some functions' "this" could be such objects.  Need to
         * re-think how we figure out "ownership."
         */
        while (o && o->magic == QOBJECT_MAGIC) {
                if ((v = object_child(o, s)) != NULL)
                        return v;
                o = o->o.owner;
        }
        return NULL;
}

/**
 * symbol_seek - Look up a symbol
 * @s: first token of "something.something.something..."
 *
 * The process is...
 * 1. Look for first "something":
 *      a. if s==__gbl__, assume q_.gbl
 *      b. look in stack frame
 *      c. look in `this'
 *      d. look in `this'->owner, loop until found or NULL
 *         (careful, s may not be name of an ancestor)
 *      e. look __gbl__ (since in step d. we might not
 *         ascend all the way up to global)
 *
 * Return: token matching @s if found, NULL otherwise.
 *      Calling code must decide what to do if it's followed
 *      by ".child.grandchild...."
 */
struct var_t *
symbol_seek(const char *s)
{
        struct var_t *v;

        if (!s)
                return NULL;
        if (!strcmp(s, "__gbl__"))
                return q_.gbl;
        if ((v = trystack(s)) != NULL)
                return v;
        if ((v = trythis(s)) != NULL)
                return v;
        return object_child(q_.gbl, s);
}

