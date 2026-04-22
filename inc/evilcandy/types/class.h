#ifndef EVILCANDY_TYPES_CLASS_H
#define EVILCANDY_TYPES_CLASS_H

#include <evilcandy/typedefs.h>
#include <evilcandy/enums.h>
#include <stdbool.h>

struct type_t;

/* types/class.c */
extern Object *typevar_new_intl(Object *bases, Object *dict,
                                Object *name);
extern bool type_issubclass(Object *type, Object *base);
extern Object *typevar_new_user(Object *bases, Object *dict,
                                Object *name, Object *priv_tup,
                                Object *delegate_name);
extern Object *type_instantiate_object(Object *type, Object *args,
                                       Object *kwargs);
extern void type_init_builtin(Object *type, bool isheap);
extern Object *type_get_bound_attr(struct type_t *tp,
                                   Object *obj, Object *key);
extern Object *instance_super_getattr(Object *instance,
                                      Object *attribute_name);
extern Object *instancevar_new(Object *class, Object *args,
                               Object *kwargs, bool call_init);
extern Object *instance_call(Object *instance, Object *method_name,
                             Object *args, Object *kwargs);
extern Object *instance_getattr(Frame *fr, Object *instance, Object *key);
extern enum result_t instance_setattr(Frame *fr, Object *instance,
                                      Object *key, Object *value);
extern Object *instance_dir(Object *instance);

#endif /* EVILCANDY_TYPES_CLASS_H */
