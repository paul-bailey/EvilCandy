/* q-symbol.c - Code that looks for symbol from input */
#include "egq.h"
#include <string.h>

/**
 * qsymbol_walk - find descendant of @o from input in source
 * @o: An object to walk down
 *
 * Return: @o if there is no ".child.granchild..." after the PC
 *      Descendant of @o if there is, or NULL if the descendant
 *      does not exist.
 */
struct qvar_t *
qsymbol_walk(struct qvar_t *o)
{
        int t;
        struct qvar_t *v;

        /* Descend */
        while ((t = qlex()) == TO_DTOK(QD_PER)) {
                qlex();
                if (q_.t != 'u')
                        qsyntax("Malformed symbol name");
                v = qobject_child(v, q_.tok.s);
                if (!v)
                        return NULL;
                if (v->magic != QOBJECT_MAGIC)
                        return v;
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
        struct qvar_t *v;
        struct qvar_t *o = q_.fp;
        /* FIXME: Some objects don't trace all the way up to q_.gbl */
        while (o) {
                if ((v = qobject_child(o, s)) != NULL)
                        return v;
                o = o->o.owner;
        }
        return NULL;
}

/*
 * @s is first token of something.something.something...
 * 1. Look for first "something," in following order
 *    of precedence:
 *      a. if s==__gbl__, assume q_.gbl
 *      b. look in stack frame
 *      c. look in `this'
 *      d. look in `this'->owner, loop until found or NULL
 *         (careful, s may not be name of an ancestor)
 *      e. look in built-in function list
 *
 * 2. If first "something" is an object and next tok is '.',
 *    walk down the name until the final descendant is reached.
 */
struct qvar_t *
qsymbol_lookup(const char *s)
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
                return qsymbol_walk(v);

        return v;
}


