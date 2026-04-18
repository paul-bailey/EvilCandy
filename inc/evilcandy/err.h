#ifndef EVC_INC_EVILCANDY_ERR_H
#define EVC_INC_EVILCANDY_ERR_H

#include <evilcandy/typedefs.h>
#include <stdio.h>
#include <stdbool.h>

/* err.c */
extern void fail(const char *msg, ...);
extern void err_setstr(Object *exc, const char *msg, ...);
extern void err_set_from_user(Object *exc);
extern Object *err_get(void);
extern void err_print_last(FILE *fp);
extern bool err_occurred(void);
extern void err_clear(void);
extern bool exception_has_trace(void);
extern void exception_add_trace(Object *call_trace);

#endif /* EVC_INC_EVILCANDY_ERR_H */
