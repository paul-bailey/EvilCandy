#ifndef EVILCANDY_TYPES_GENERATOR_H
#define EVILCANDY_TYPES_GENERATOR_H

/*
 * XXX Philosophical conundrum.  Does this belong in internal/
 * or (public) evilcandy/?  Only one part of the code accesses it,
 * but the type is visible to the user.
 */
#include <evilcandy/typedefs.h>

/* types/generator.c */
extern Object *generatorvar_new(Frame *frame);

#endif /* EVILCANDY_TYPES_GENERATOR_H */
