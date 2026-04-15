/*
 * evilcandy.h - Main API header for EvilCandy
 */
#ifndef EVILCANDY_H
#define EVILCANDY_H

#include "config.h"

#ifdef GIT_VERSION
# define EVILCANDY_VERSION PACKAGE_STRING "-" GIT_VERSION
#else
# define EVILCANDY_VERSION PACKAGE_STRING
#endif

/*
 * headers for stuff I wish was standard
 * so I wouldn't have to write them myself.
 */
#include <lib/helpers.h>
#include <lib/buffer.h>
#include <lib/list.h>
#include <lib/utf8.h>

/* Standard C headers */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <sys/types.h>
/* REVISIT: include unistd and stdlib at this level? math.h? */

#include "iterator.h"

struct type_method_t;
struct type_prop_t;

/* assembler.c */
extern Object *assemble(const char *filename,
                        FILE *fp, Object *localdict);
extern Object *assemble_string(const char *str, bool eval_only);

/* import.c */
extern Object *evc_import(Frame *fr, const char *file_name);

/* instruction_name.c */
extern const char *instruction_name(int opcode);
extern int instruction_from_name(const char *name);
extern int instruction_from_key(Object *key);

/* namespace.c */
Object *namespacevar_new(Object *dict, Object *name);

/* readline.c */
extern ssize_t myreadline(char **linep, size_t *size,
                          FILE *fp, const char *prompt);

/* strto.c */
extern enum result_t evc_strtod(const char *s, char **endptr, double *d);
extern enum result_t evc_strtol(const char *s, char **endptr,
                                int base, long long *v);
extern enum result_t string_tod(Object *str, size_t *pos, double *v);
extern enum result_t string_toll(Object *str, int base,
                                 size_t *pos, long long *reslt);
extern char *strtod_scanonly(const char *s, int *may_be_int);


/* types/empty.c */
extern Object *emptyvar_new(void);

/* types/generator.c */
extern Object *generatorvar_new(Frame *frame);

/* types/intl.c */
extern Object *uuidptrvar_new(char *uuid);
extern char *uuidptr_get_cstring(Object *v);
extern long long idvar_toll(Object *v);
extern Object *idvar_new(long long id);

#endif /* EVILCANDY_H */
