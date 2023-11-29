/*
 * common to types/ and builtin/ folders, else they'd
 * be in a header in one of those
 */
#ifndef EGQ_UARG_H
#define EGQ_UARG_H

#include "egq.h"

#define arg_type_err(v, want) do { \
        syntax("Argument is type '%s' but '%s' is expected", \
                typestr((v)->magic), typestr(want)); \
} while (0)
#define arg_type_check(v, want) do { \
        if ((v)->magic != (want)) \
                arg_type_err(v, want); \
} while (0)

#endif /* EGQ_UARG_H */
