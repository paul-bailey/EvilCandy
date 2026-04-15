#ifndef EVC_INC_EVILCANDY_ERR_H
#define EVC_INC_EVILCANDY_ERR_H

#include <stdio.h>

/* err.c */
extern void fail(const char *msg, ...);
extern void err_setstr(Object *exc, const char *msg, ...);
extern void err_set_from_user(Object *exc);
extern Object *err_get(void);
extern void err_print_last(FILE *fp);
extern bool err_occurred(void);
extern void err_clear(void);

#endif /* EVC_INC_EVILCANDY_ERR_H */
