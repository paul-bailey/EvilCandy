/*
 * evilcandy.h - Main API header for EvilCandy
 */
#ifndef EVILCANDY_H
#define EVILCANDY_H

#include "config.h"

/*
 * headers for stuff I wish was standard
 * so I wouldn't have to write them myself.
 */
#include <lib/helpers.h>
#include <lib/buffer.h>
#include <lib/list.h>

/* Standard C headers */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <sys/types.h>
/* REVISIT: include unistd and stdlib at this level? math.h? */

#include "evcenums.h"
#include "typedefs.h"
#include "uarg.h"
#include "debug.h"
#include "recursion.h"
#include "global.h"
#include "objtypes.h"
#include "var.h"
#include "vm.h"

/* constructors/destructors for certain C file */
/* global.c */
extern void cfile_init_global(void);
extern void cfile_deinit_global(void);
/* ewrappers.c */
extern void cfile_init_ewrappers(void);
/* var.c */
extern void cfile_init_var(void);
extern void cfile_deinit_var(void);
/* vm.c */
extern void cfile_init_vm(void);
extern void cfile_deinit_vm(void);
/* instruction_name.c */
extern void cfile_init_instruction_name(void);
extern void cfile_deinit_instruction_name(void);

/* constructors/destructors for built-in modules */
/* builtin/builtin.c */
extern void moduleinit_builtin(void);
/* builtin/math.c */
extern void moduleinit_math(void);
/* builtin/io.c */
extern void moduleinit_io(void);

/* assembler.c */
extern Object *assemble(const char *filename,
                        FILE *fp, bool toeof, int *status);

/* err.c */
extern void fail(const char *msg, ...);
extern void err_setstr(Object *exc, const char *msg, ...);
extern void err_set_from_user(Object *exc);
extern Object *err_get(void);
extern void err_print(FILE *fp, Object *exc);
extern void err_print_last(FILE *fp);
extern bool err_occurred(void);
extern void err_clear(void);
extern void err_attribute(const char *getorset,
                          Object *deref, Object *obj);
extern void err_index(Object *index);
extern void err_argtype(const char *what);
extern void err_locked(void);
extern void err_permit(const char *op, Object *var);
extern void err_permit2(const char *op, Object *a, Object *b);
extern void err_errno(const char *msg, ...);

/* disassemble.c */
extern void disassemble(FILE *fp, Object *ex,
                        const char *sourcefile_name);
extern void disassemble_lite(FILE *fp, Object *ex);
extern void disassemble_minimal(FILE *fp, Object *ex);

/* ewrappers.c */
extern char *estrdup(const char *s);
extern void *emalloc(size_t size);
extern void *ecalloc(size_t size);
extern void *erealloc(void *buf, size_t size);
extern void *ememdup(const void *buf, size_t size);
extern ssize_t egetline(char **line, size_t *linecap, FILE *fp);
extern void efree(void *ptr);

/* hash.c */
extern hash_t calc_string_hash(Object *key);

/* instruction_name.c */
extern const char *instruction_name(int opcode);
extern int instruction_from_name(const char *name);
extern int instruction_from_key(Object *key);

/* json.c */
Object *dict_from_json(const char *filename);

/* op.c */
extern Object *qop_mul(Object *a, Object *b);
extern Object *qop_pow(Object *a, Object *b);
extern Object *qop_div(Object *a, Object *b);
extern Object *qop_mod(Object *a, Object *b);
extern Object *qop_add(Object *a, Object *b);
extern Object *qop_sub(Object *a, Object *b);
extern Object *qop_lshift(Object *a, Object *b);
extern Object *qop_rshift(Object *a, Object *b);
extern Object *qop_bit_and(Object *a, Object *b);
extern Object *qop_bit_or(Object *a, Object *b);
extern Object *qop_xor(Object *a, Object *b);
extern Object *qop_bit_not(Object *v);
extern Object *qop_negate(Object *v);

/* find_import.c */
extern FILE *find_import(const char *cur_path, const char *file_name,
                         char *pathfill, size_t size);

/* path.c */
extern void pop_path(FILE *fp);
extern FILE *push_path(const char *filename);

/* types/array.c */
extern Object *arrayvar_new(int n_items);
extern Object *arrayvar_from_stack(Object **items, int n_items, bool consume);
extern enum result_t array_setitem(Object *array, int i, Object *child);
extern Object *array_getitem(Object *array, int idx);
extern enum result_t array_append(Object *array, Object *child);
extern void array_reverse(Object *array);

/* types/bytes.c */
extern Object *bytesvar_new(const unsigned char *buf, size_t len);
extern Object *bytesvar_from_source(char *src);
extern const unsigned char *bytes_getbuf(Object *v);
extern Object *bytesvar_nocopy(const unsigned char *buf, size_t len);

/* types/comlex.c */
extern Object *complexvar_new(double real, double imag);

/* types/empty.c */
extern Object *emptyvar_new(void);

/* types/file.c */
extern Object *filevar_new(FILE *fp, Object *name, unsigned int mode);
extern enum result_t file_write(Object *file, Object *data);

/* types/float.c */
extern Object *floatvar_new(double value);

/* types/floats.c */
extern Object *floatsvar_from_bytes(Object *v,
                                    enum floats_enc_t enc, int le);
extern Object *floatsvar_from_list(Object *v);

/* types/function.c */
extern Object *funcvar_new_user(Object *ex);
extern Object *funcvar_new_intl(Object *(*cb)(Frame *),
                               int minargs, int maxargs);
extern Object *funcvar_from_lut(const struct type_inittbl_t *tbl);
extern int function_setattr(Object *func, int attr, int value);
extern Object *function_call(Frame *fr, bool have_dict);
extern void function_add_closure(Object *func, Object *clo);
extern void function_add_default(Object *func,
                        Object *deflt, int argno);

/* types/integer.c */
extern Object *intvar_new(long long value);

/* types/intl.c */
extern Object *uuidptrvar_new(char *uuid);
extern char *uuidptr_get_cstring(Object *v);
extern long long idvar_toll(Object *v);
extern Object *idvar_new(long long id);

/* types/dict.c */
extern Object *dictvar_new(void);
extern Object *dictvar_from_methods(Object *parent,
                        const struct type_inittbl_t *tbl);
extern Object *dict_keys(Object *obj, bool sorted);
extern Object *dict_getitem(Object *o, Object *key);
extern Object *dict_getitem_cstr(Object *o, const char *cstr_key);
extern enum result_t dict_setitem(Object *o, Object *key, Object *attr);
extern void dict_add_to_globals(Object *obj);
extern enum result_t dict_setitem_replace(Object *dict,
                                Object *key, Object *attr);
extern enum result_t dict_setitem_exclusive(Object *dict,
                                Object *key, Object *attr);
extern char *dict_unique(Object *dict, const char *key);
extern void dict_unpack(Object *obj, ...);

/* types/method.c */
extern int methodvar_tofunc(Object *meth,
                            Object **func, Object **owner);
extern Object *methodvar_new(Object *func, Object *owner);
extern Object *method_peek_self(Object *meth);

/* types/property.c */
extern Object *propertyvar_new(const struct type_prop_t *props);
extern Object *property_get(Object *prop, Object *owner);
extern enum result_t property_set(Object *prop,
                                  Object *owner, Object *value);

/* types/range.c */
extern Object *rangevar_new(long long start,
                        long long stop, long long step);

/* types/star.c */
extern Object *starvar_new(Object *x);
extern Object *star_unpack(Object *star);

/* types/string.c */
extern Object *stringvar_new(const char *cstr);
extern Object *stringvar_newn(const char *cstr, size_t n);
extern Object *stringvar_from_buffer(struct buffer_t *b);
extern Object *stringvar_from_source(const char *tokenstr, bool imm);
extern Object *stringvar_nocopy(const char *cstr);
extern hash_t string_update_hash(Object *v);

/* types/tuple.c */
extern enum result_t tuple_validate(Object *tup, const char *descr,
                                    bool map_function);
extern Object *tuplevar_from_stack(Object **items, int n_items, bool consume);
extern Object *tuplevar_new(int n_items);
extern Object *tuple_getitem(Object *tup, int idx);

/* uuid.c */
extern char *uuidstr(void);

#endif /* EVILCANDY_H */
