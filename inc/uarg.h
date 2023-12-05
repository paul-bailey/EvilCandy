/*
 * common to types/ and builtin/ folders, else they'd
 * be in a header in one of those
 */
#ifndef EGQ_UARG_H
#define EGQ_UARG_H

#include <evilcandy.h>

#define arg_type_err(v) \
        syntax("Argument wrong type: '%s'", typestr(v))
#define arg_type_check(v, want) do { \
        if ((v)->magic != (want)) \
                arg_type_err(v); \
} while (0)

#endif /* EGQ_UARG_H */
