#ifndef EVC_INC_INTERNAL_TYPES_INTERNAL_TYPES_H
#define EVC_INC_INTERNAL_TYPES_INTERNAL_TYPES_H

#include <evilcandy/typedefs.h>

/*
 * XXX namespace and empty are not actually "internal" -- they're visible
 * to the user -- but only a limited part of this source tree accesses
 * these functions.
 */

/* namespace.c */
Object *namespacevar_new(Object *dict, Object *name);

/* types/empty.c */
extern Object *emptyvar_new(void);

/* types/intl.c */
extern Object *uuidptrvar_new(char *uuid);
extern char *uuidptr_get_cstring(Object *v);
extern long long idvar_toll(Object *v);
extern Object *idvar_new(long long id);

#endif /* EVC_INC_INTERNAL_TYPES_INTERNAL_TYPES_H */
