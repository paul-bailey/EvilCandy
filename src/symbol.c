/* q-symbol.c - Code that looks for symbol from input */
#include "egq.h"
#include <string.h>

/**
 * qsymbol_walk - walk down an object's "child.grandchild..." path
 *                as specified by the input tokens.
 * @o: The object to be the starting point.
 * @flags: Determines a corner case of return value, see below.
 *
 * Return:
 *    - If every descendant of n-size path exists, return nth descendant
 *    - If a non-object descendant is found first, return that.
 *    - If next descendant on path is not found, then...
 *            - if F_FORCE is set in @flags, return the parent.
 *              We may be appending a new child to the object.
 *            - if F_FORCE is not set, return NULL.  We're probably
 *              evaluating an object, not assigning it.
 */
struct qvar_t *
qsymbol_walk(struct qvar_t *o, unsigned int flags)
{
        int t;
        struct qvar_t *v;

        while ((t = qlex()) == OC_PER) {
                qlex();
                if (cur_oc->t != 'u')
                        qsyntax("Malformed symbol name");
                v = qobject_child(o, cur_oc->s);
                if (!v) {
                        if (!(flags & F_FORCE))
                                return NULL;
                        q_unlex();
                        q_unlex();
                        return o;
                }
                if (v->magic != QOBJECT_MAGIC)
                        return v;
                o = v;
        }
        q_unlex();
        return v;
}

/* Helper to qsymbol_lookup - look in stack */
static struct qvar_t *
trystack(const char *s)
{
        /* Args actually begin 1st after FP */
        struct qvar_t *p;
        for (p = q_.fp + 1; p < q_.sp; p++) {
                if (p->name && !strcmp(p->name, s))
                        return p;
        }
        return NULL;
}

/* Helper to qsymbol_lookup - walk up namespace */
static struct qvar_t *
trythis(const char *s)
{
        struct qvar_t *v, *o = q_.fp;
        /*
         * FIXME: Some objects don't trace all the way up to q_.gbl
         * and some functions' "this" could be such objects.
         */
        while (o) {
                if ((v = qobject_child(o, s)) != NULL)
                        return v;
                o = o->o.owner;
        }
        return NULL;
}

/**
 * symbol_lookup - Look up a symbol
 * @s: first token of "something.something.something..."
 * @flags: same as with qsymbol_walk, see comment there
 *
 * The process is...
 * 1. Look for first "something":
 *      a. if s==__gbl__, assume q_.gbl
 *      b. look in stack frame
 *      c. look in `this'
 *      d. look in `this'->owner, loop until found or NULL
 *         (careful, s may not be name of an ancestor)
 *      e. look in built-in function list
 *
 * 2. If first "something" is an object and next tok is '.',
 *    return result of "qsymbol_walk(result, @flags)"
 *
 * Return NULL if top-level @s can't be found, regardless
 * of flags.
 */
struct qvar_t *
qsymbol_lookup(const char *s, unsigned int flags)
{
        struct qvar_t *v = NULL;

        do {
                if (!strcmp(s, "__gbl__")) {
                        v = q_.gbl;
                        break;
                }
                if ((v = trystack(s)) != NULL)
                        break;
                if ((v = trythis(s)) != NULL)
                        break;
                if ((v = q_builtin_seek(s)) != NULL)
                        break;
                return NULL;
        } while (0);

        if (v->magic == QOBJECT_MAGIC)
                return qsymbol_walk(v, flags);

        return v;
}


