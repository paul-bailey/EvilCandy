#ifndef EVC_INC_INTERNAL_TYPES_NUMBER_TYPES_H
#define EVC_INC_INTERNAL_TYPES_NUMBER_TYPES_H

#include <internal/type_protocol.h>
#include <internal/type_registry.h>

/*
 * Some objects made public so some functions can be inline.
 * These are otherwise used privately in integer.c and float.c
 */
struct intvar_t {
        Object base;
        long long i;
};

struct floatvar_t {
        Object base;
        double f;
};

extern int intvar_toi(Object *v);

/* Warning!! Only call these if you already type-checked @v */
static inline double floatvar_tod(Object *v)
        { return ((struct floatvar_t *)v)->f; }
static inline long long intvar_toll(Object *v)
        { return ((struct intvar_t *)v)->i; }
static inline long long realvar_toint(Object *v)
        { return isvar_int(v) ? intvar_toll(v) : (long long)floatvar_tod(v); }
static inline double realvar_tod(Object *v)
        { return isvar_float(v) ? floatvar_tod(v) : (double)intvar_toll(v); }

#endif /* EVC_INC_INTERNAL_TYPES_NUMBER_TYPES_H */
