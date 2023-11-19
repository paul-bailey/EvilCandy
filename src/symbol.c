/* symbol.c - Code that looks for symbol from input */
#include "egq.h"
#include <string.h>

/* either returns pointer to new parent, or NULL, meaning "wrap it up" */
static struct qvar_t *
walk_obj_helper(struct qvar_t *result, struct qvar_t *parent, bool expression)
{
        do {
                struct qvar_t *child;
                qlex();
                expect('u');
                if (parent->magic != QOBJECT_MAGIC) {
                        /*
                         * calling a typedef's built-in methods.
                         * All types are slightly objects, slightly not.
                         */
                        struct qvar_t *method;
                        method = ebuiltin_method(parent, cur_oc->s);
                        call_function(method, result, parent);
                        return NULL;
                }
                child = qobject_child(parent, cur_oc->s);
                if (!child) {
                        if (!expression)
                                qsyntax("symbol %s not found in %s",
                                        cur_oc->s, parent->name);
                        /*
                         * Parsing "alice.bob = something;", where
                         * "alice" exists and "bob" does not.
                         * Append "bob" as a new child of "alice",
                         * and evaluate the "something" of "bob".
                         */
                        child = qvar_new();
                        child->name = cur_oc->s;
                        qlex();
                        expect(OC_EQ);
                        q_eval(child);
                        qobject_add_child(parent, child);
                        qlex();
                        expect(OC_SEMI);
                        return NULL;
                }
                parent = child;
                qlex();
        } while (cur_oc->t == OC_PER);
        return parent;
}

/*
 * FIXME: "array[x]=y;" should be valid if x is out of bounds of the array.
 * It just means we have to grow the array.
 */
static struct qvar_t *
walk_arr_helper(struct qvar_t *result, struct qvar_t *parent, bool expression)
{
        struct qvar_t *idx;
        struct qvar_t *child;
        /* Don't try to evaluate associative-array indexes this way */
        if (parent->magic != QARRAY_MAGIC) {
                qsyntax("Cannot de-reference type %s with [",
                        q_typestr(parent->magic));
        }
        idx = qstack_getpush();
        q_eval(idx);
        if (idx->magic != QINT_MAGIC)
                qsyntax("Array index must be integer");
        child = qarray_child(parent, idx->i);
        if (!child)
                qsyntax("Array de-reference out of bounds");
        qstack_pop(NULL);
        return child;
}

/**
 * symbol_walk: walk down the ".child.grandchild..." path of a parent
 *              and take certain actions.
 * @result: If the symbol is assigned, this stores the result.  It may
 *          be a dummy, but it may not be NULL
 * @parent: Parent of the ".child.grandchild..."
 * @expression: true if called from expression(), false if called from
 *              eval(); This whole function doesn't belong here, it
 *              should be split between eval.c and exec.c, for it does
 *              VERY DIFFERENT things based on this flag.  Unfortunately,
 *              splitting the function between the two modules would
 *              result in an egregious DRY violation.
 *
 * Program counter must be before the first '.', if it exists.
 * Note, the operation might happen with the parent itself, if no
 * '.something' is expressed.
 */
void
symbol_walk(struct qvar_t *result, struct qvar_t *parent, bool expression)
{
        for (;;) {
                if (parent->magic == QFUNCTION_MAGIC
                    || parent->magic == QINTL_MAGIC) {
                        if (expression) {
                                call_function(parent, result, NULL);
                                qlex();
                                expect(OC_SEMI);
                                break;
                        } else {
                                int t = qlex(); /* need to peek */
                                q_unlex();
                                if (t == OC_LPAR) {
                                        /* it's a function call */
                                        call_function(parent, result, NULL);
                                        break;
                                }
                        }
                        /* else, it's a variable assignment, fall through */
                }
                qlex();
                if (cur_oc->t == OC_PER) {
                        parent = walk_obj_helper(result, parent, expression);
                        if (!parent)
                                break;
                        q_unlex();
                } else if (cur_oc->t == OC_LBRACK) {
                        parent = walk_arr_helper(result, parent, expression);
                } else {
                        if (expression) {
                                /*
                                 * we're at the "that" of "this = that;",
                                 * where @parent is the "this".
                                 */
                                expect(OC_EQ);
                                q_eval_safe(parent);
                                qlex();
                                expect(OC_SEMI);
                        } else {
                                /*
                                 * we've been evaluating an atomic,
                                 * and we found the end of it
                                 */
                                q_unlex();
                                qop_mov(result, parent);
                        }
                        break;
                }
        }
}

/* Helper to symbol_seek - look in stack */
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

/* Helper to symbol_seek - walk up namespace */
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
struct qvar_t *
symbol_seek(const char *s)
{
        struct qvar_t *v;

        if (!strcmp(s, "__gbl__"))
                return q_.gbl;
        if ((v = trystack(s)) != NULL)
                return v;
        if ((v = trythis(s)) != NULL)
                return v;
        return qobject_child(q_.gbl, s);
}

