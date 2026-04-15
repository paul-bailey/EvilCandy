#ifndef EVILCANDY_TYPES_PROPERTY_H
#define EVILCANDY_TYPES_PROPERTY_H

#include <evilcandy/typedefs.h>
#include <evilcandy/enums.h>

struct type_prop_t;

/* types/property.c */
extern Object *propertyvar_new_intl(const struct type_prop_t *props);
extern Object *propertyvar_new_user(Object *setter, Object *getter);
extern Object *property_get(Object *prop, Object *owner, Object *name);
extern enum result_t property_set(Object *prop, Object *owner,
                                  Object *value, Object *name);

#endif /* EVILCANDY_TYPES_PROPERTY_H */
