#ifndef EVILCANDY_STRTO_H
#define EVILCANDY_STRTO_H

#include <evilcandy/typedefs.h>
#include <evilcandy/enums.h>
#include <stddef.h>

/* strto.c */
extern enum result_t evc_strtod(const char *s, char **endptr, double *d);
extern enum result_t evc_strtol(const char *s, char **endptr,
                                int base, long long *v);
extern enum result_t string_tod(Object *str, size_t *pos, double *v);
extern enum result_t string_toll(Object *str, int base,
                                 size_t *pos, long long *reslt);
extern char *strtod_scanonly(const char *s, int *may_be_int);

#endif /* EVILCANDY_STRTO_H */
