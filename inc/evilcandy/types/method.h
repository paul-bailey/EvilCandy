#ifndef EVILCANDY_TYPES_METHOD_H
#define EVILCANDY_TYPES_METHOD_H

#include <evilcandy/typedefs.h>

/* types/method.c */
extern int methodvar_tofunc(Object *meth,
                            Object **func, Object **owner);
extern Object *methodvar_new(Object *func, Object *owner);
extern Object *method_peek_self(Object *meth);


#endif /* EVILCANDY_TYPES_METHOD_H */
