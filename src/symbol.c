/* symbol.c - Code that looks for symbol from input */
#include "uarg.h"
#include "egq.h"
#include <string.h>

/* Helper to symbol_seek - look in stack */
static struct var_t *
trystack(const char *s, bool gbl)
{
        return frame_get_var(s, gbl);
}

/* Helper to symbol_seek - walk up namespace */
static struct var_t *
trythis(const char *s)
{
        struct var_t *o = get_this();
        if (o && o->magic == QOBJECT_MAGIC)
                return object_child_l(o, s);
        return NULL;
}

/**
 * symbol_seek_stack - Look up a symbol on the stack only
 * @s: first token of "something.something.something..."
 */
struct var_t *
symbol_seek_stack(const char *s)
{
        s = literal(s);
        return s ? trystack(s, false) : NULL;
}

/**
 * symbol_seek_stack_l - Like symbol_seek_stack, but @s is known to be a
 *                       return value of literal()
 */
struct var_t *
symbol_seek_stack_l(const char *s)
{
        return trystack(s, false);
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
        static char *gbl = NULL;
        struct var_t *v;

        if (!s)
                return NULL;

        s = literal(s);
        if (!s)
                return NULL;

        if (!gbl)
                gbl = literal("__gbl__");

        if (s == gbl)
                return q_.gbl;
        if ((v = trystack(s, true)) != NULL)
                return v;
        if ((v = trythis(s)) != NULL)
                return v;
        return object_child_l(q_.gbl, s);
}

