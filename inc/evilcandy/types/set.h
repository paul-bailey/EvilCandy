#ifndef EVILCANDY_TYPES_SET_H
#define EVILCANDY_TYPES_SET_H

#include <evilcandy/typedefs.h>
#include <evilcandy/enums.h>
#include <stdbool.h>

/* types/set.c */
extern Object *setvar_new(Object *seq);
extern enum result_t set_extend(Object *set, Object *seq);
extern enum result_t set_additem(Object *set, Object *child, Object **unique);
extern Object *set_unique(Object *set, Object *item);
extern Object *set_intern(Object *set, Object *item);
extern bool set_hasitem(Object *set, Object *item);

#endif /* EVILCANDY_TYPES_SET_H */
