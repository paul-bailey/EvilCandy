/*
 * common to types/ and builtin/ folders, else they'd
 * be in a header in one of those
 */
#ifndef EGQ_UARG_H
#define EGQ_UARG_H

#include <evilcandy.h>

/* err.c */
extern int arg_type_check_failed(struct var_t *v, int want);

static inline int
arg_type_check(struct var_t *v, int want)
{
        if (v && v->magic == want)
                return 0;
        else
                return arg_type_check_failed(v, want);
}

#endif /* EGQ_UARG_H */
