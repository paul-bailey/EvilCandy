#ifndef EVILCANDY_TYPES_FUNCTION_H
#define EVILCANDY_TYPES_FUNCTION_H

#include <evilcandy/typedefs.h>

struct type_method_t;

/* types/function.c */
extern Object *funcvar_new_user(Object *ex, Object *argspec);
extern Object *funcvar_new_intl(Object *(*cb)(Frame *));
extern Object *funcvar_from_lut(const struct type_method_t *tbl);
extern Object *function_call(Frame *fr, Object *func, Object *args,
                             Object *kwargs);
extern void function_add_closure(Object *func, Object *clo);
extern Object *function_get_executable(Object *func);

#endif /* EVILCANDY_TYPES_FUNCTION_H */
