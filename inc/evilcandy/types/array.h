#ifndef EVILCANDY_TYPES_ARRAY_H
#define EVILCANDY_TYPES_ARRAY_H

#include <evilcandy/typedefs.h>
#include <evilcandy/enums.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* types/array.c */
extern Object *arrayvar_new(int n_items);
extern Object *arrayvar_from_stack(Object **items, int n_items, bool consume);
extern enum result_t array_setitem(Object *array, size_t i, Object *child);
extern Object *array_getitem(Object *array, size_t idx);
extern enum result_t array_append(Object *array, Object *child);
extern enum result_t array_extend(Object *array, Object *seq);
extern void array_reverse(Object *array);
extern Object *array_borrowitem(Object *array, size_t idx);
extern enum result_t array_delete_chunk(Object *array,
                                        size_t at, size_t n_items);
extern ssize_t array_indexof(Object *arr, Object *item);
extern ssize_t array_rindexof(Object *arr, Object *item);
extern ssize_t array_indexof_strict(Object *arr, Object *item);
extern Object *array_getslice(Object *obj, ssize_t start,
                              ssize_t stop, ssize_t step);

#endif /* EVILCANDY_TYPES_ARRAY_H */
