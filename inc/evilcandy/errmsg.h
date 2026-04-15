#ifndef EVC_INC_EVILCANDY_ERRMSG_H
#define EVC_INC_EVILCANDY_ERRMSG_H

#include <evilcandy/typedefs.h>

/* errmsg.c */
extern void err_hashable(Object *obj, const char *fname);
extern void err_iterable(Object *obj, const char *fname);
extern void err_errno(const char *msg, ...);
extern void err_doublearg(const char *argname);
extern void err_maxargs(int nargs, int expect);

#endif /* EVC_INC_EVILCANDY_ERRMSG_H */
