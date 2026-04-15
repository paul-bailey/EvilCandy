#ifndef EVC_INC_INTERNAL_BUILTIN_SYS_H
#define EVC_INC_INTERNAL_BUILTIN_SYS_H

#include <evilcandy/typedefs.h>

/* builtin/sys.c */
extern Object *sys_getitem(Object *key);
extern Object *sys_getitem_cstr(const char *key);


#endif /* EVC_INC_INTERNAL_BUILTIN_SYS_H */
