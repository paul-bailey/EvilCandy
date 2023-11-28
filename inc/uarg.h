/*
 * common to types/ and builtin/ folders, else they'd
 * be in a header in one of those
 */
#ifndef EGQ_UARG_H
#define EGQ_UARG_H

#include "egq.h"

/*
 * see function.c, push_uargs().  FP points at "this", FP+1 is
 * currently-executed function, FP+2 is first arg.
 */
#define ARG_FP_OFFSET           2

/* make function-like, so users don't assume it's constant */
#define ARG_FRAME_START()       (q_.fp + ARG_FP_OFFSET)

/* assumes stack is set up, but automatic variables not yet declared */
static inline int arg_count(void) { return q_.sp - ARG_FRAME_START(); }

static inline struct var_t *
getarg(unsigned int n)
{
        int retp = ARG_FRAME_START() + n;
        if (retp < ARG_FRAME_START() || retp >= q_.sp)
                return NULL;
        return q_.stack[retp];
}

#define arg_type_err(v, want) do { \
        syntax("Argument is type '%s' but '%s' is expected", \
                typestr((v)->magic), typestr(want)); \
} while (0)
#define arg_type_check(v, want) do { \
        if ((v)->magic != (want)) \
                arg_type_err(v, want); \
} while (0)

#endif /* EGQ_UARG_H */

