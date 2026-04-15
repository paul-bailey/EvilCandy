#ifndef EVC_INTERNAL_INSTRUCTION_NAME_H
#define EVC_INTERNAL_INSTRUCTION_NAME_H

#include <evilcandy/typedefs.h>

/* instruction_name.c */
extern const char *instruction_name(int opcode);
extern int instruction_from_name(const char *name);
extern int instruction_from_key(Object *key);

#endif /* EVC_INTERNAL_INSTRUCTION_NAME_H */
