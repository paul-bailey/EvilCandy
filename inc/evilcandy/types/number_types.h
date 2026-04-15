#ifndef EVILCANDY_TYPES_NUMBER_TYPES_H
#define EVILCANDY_TYPES_NUMBER_TYPES_H

#include <evilcandy/typedefs.h>

/* types/comlex.c */
extern Object *complexvar_new(double real, double imag);
/* types/float.c */
extern Object *floatvar_new(double value);
/* types/integer.c */
extern Object *intvar_new(long long value);

#endif /* EVILCANDY_TYPES_NUMBER_TYPES_H */
