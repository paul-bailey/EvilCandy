#ifndef EVILCANDY_TYPES_CLASS_H
#define EVILCANDY_TYPES_CLASS_H

#include <evilcandy/typedefs.h>
#include <evilcandy/enums.h>
#include <stdbool.h>

/* types/class.c */
extern Object *typevar_new_intl(Object *bases, Object *dict,
                                Object *name);
extern bool type_issubclass(Object *type, Object *base);
extern Object *typevar_new_user(Object *bases, Object *dict,
                                Object *name, Object *priv_tup);
extern Object *instance_super_getattr(Object *instance,
                                      Object *attribute_name);
extern Object *instancevar_new(Object *class, Object *args,
                               Object *kwargs, bool call_init);
extern void *instance_get_priv(Object *instance);
extern void instance_set_priv(Object *instance,
                              void (*cleanup)(void *), void *priv);
extern Object *instance_call(Object *instance, Object *method_name,
                             Object *args, Object *kwargs);
extern Object *instance_getattr(Frame *fr, Object *instance, Object *key);
extern enum result_t instance_setattr(Frame *fr, Object *instance,
                                      Object *key, Object *value);
extern Object *instance_dir(Object *instance);
extern bool instance_instanceof(Object *instance, Object *class);

#endif /* EVILCANDY_TYPES_CLASS_H */
