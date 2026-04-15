#ifndef EVILCANDY_ASSEMBLE_H
#define EVILCANDY_ASSEMBLE_H

#include <evilcandy/typedefs.h>
#include <stdio.h>
#include <stdbool.h>

/* assembler.c */
extern Object *assemble(const char *filename,
                        FILE *fp, Object *localdict);
extern Object *assemble_string(const char *str, bool eval_only);


#endif /* EVILCANDY_ASSEMBLE_H */
