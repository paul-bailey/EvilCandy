#ifndef EVILCANDY_TYPES_TUPLE_H
#define EVILCANDY_TYPES_TUPLE_H

#include <evilcandy/typedefs.h>
#include <evilcandy/enums.h>
#include <stddef.h>
#include <stdbool.h>

/* types/tuple.c */
extern enum result_t tuple_validate(Object *tup, const char *descr,
                                    bool map_function);
extern Object *tuplevar_from_stack(Object **items, int n_items, bool consume);
extern Object *tuplevar_new(int n_items);
extern Object *tuple_getitem(Object *tup, size_t idx);
extern Object *tuple_borrowitem(Object *tup, size_t idx);


#endif /* EVILCANDY_TYPES_TUPLE_H */
