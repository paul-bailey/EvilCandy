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

#include <evilcandy/typedefs.h>
#include <evilcandy/enums.h>

#define ITERATOR_FOREACH(p, it) \
        for (p = iterator_next(it);  \
             p != NULL && p != ErrorVar; \
             p = iterator_next(it))

extern Object *iterator_next(Object *iter);
extern Object *iterator_get(Object *obj);

extern enum result_t iterator_foreach(
                Object *it,
                enum result_t (*action)(Object *, void *),
                void *data);
extern Object *iterator_errget(Object *obj, const char *fname);

#endif /* EVC_ITERATOR_H */
