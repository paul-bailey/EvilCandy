/*
 * iterator.h - Helpers for managing iterators.
 *
 * High-level functions (recommended):
 *      iterator_errget()
 *      iterator_foreach()
 *
 * Low-level functions (only if above is insufficient):
 *      iterator_get()
 *      ITERATOR_FOREACH()
 *
 * Not recommended, but still needed somewhere:
 *      iterator_get()
 *      iterator_next()
 */
#ifndef EVC_ITERATOR_H
#define EVC_ITERATOR_H

#include <objtypes.h>

/* This produces a reference for the return value */
static inline Object *
iterator_get(Object *obj)
{
        if (!obj->v_type->get_iter)
                return NULL;
        return obj->v_type->get_iter(obj);
}

/*
 * Get next iter.  Returns NULL if no more items, or ErrorVar if there
 * was an error (if, say, a generator threw an exception).  Since there
 * are TWO special return values to watch out for, it's best to use
 * ITERATOR_FOREACH() and check for the return value after the loop.
 */
static inline Object *
iterator_next(Object *iter)
{
        bug_on(!iter->v_type->iter_next);
        return iter->v_type->iter_next(iter);
}

#define ITERATOR_FOREACH(p, it) \
        for (p = iterator_next(it);  \
             p != NULL && p != ErrorVar; \
             p = iterator_next(it))

extern enum result_t iterator_foreach(
                Object *it,
                enum result_t (*action)(Object *, void *),
                void *data);
extern Object *iterator_errget(Object *obj, const char *fname);

#endif /* EVC_ITERATOR_H */
