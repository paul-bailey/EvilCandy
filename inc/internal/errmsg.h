#ifndef EVC_INC_INTERNAL_ERRMSG_H
#define EVC_INC_INTERNAL_ERRMSG_H

#include <evilcandy/typedefs.h>

extern void err_subscript(const char *getorset,
                          Object *key, Object *obj);
extern void err_index(Object *index);
extern void err_locked(void);
extern void err_va_minargs(Object *varargs, int expect);
extern void err_ord(int codec, long ord);
extern void err_attribute(const char *getorset,
                          Object *key, Object *obj);
extern void err_argtype(const char *what);
extern void err_minargs(int nargs, int expect);

/* TODO: Are these used anywhere? */
extern void err_notreal(const char *tpname);
extern void err_exactargs(int nargs, int expect);
extern void err_frame_minargs(Frame *fr, int expect);

#endif /* EVC_INC_INTERNAL_ERRMSG_H */
