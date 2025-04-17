/*
 * common to types/ and builtin/ folders, else they'd
 * be in a header in one of those
 */
#ifndef EVILCANDY_UARG_H
#define EVILCANDY_UARG_H

#include "var.h"

/* err.c */
extern int arg_type_check_failed(struct var_t *v,
                                 struct type_t *want);

static inline int
arg_type_check(struct var_t *v, struct type_t *want)
{
        if (v && v->v_type == want)
                return 0;
        else
                return arg_type_check_failed(v, want);
}

#endif /* EGQ_UARG_H */
