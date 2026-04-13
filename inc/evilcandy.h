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

#include "compiler.h"
#include "evcenums.h"
#include "typedefs.h"
#include "uarg.h"
#include "debug.h"
#include "hash.h"
#include "recursion.h"
#include "global.h"
#include "var.h"
#include "vm.h"
#include "string_writer.h"
#include "string_reader.h"
#include "iterator.h"

struct type_method_t;
struct type_prop_t;

/* assembler.c */
extern Object *assemble(const char *filename,
                        FILE *fp, Object *localdict);
extern Object *assemble_string(const char *str, bool eval_only);

/* ctype.c */
extern bool evc_isalnum(unsigned long c);
extern bool evc_isalpha(unsigned long c);
extern bool evc_isdigit(unsigned long c);
extern bool evc_isprint(unsigned long c);
extern bool evc_isspace(unsigned long c);
extern bool evc_isupper(unsigned long c);
extern bool evc_islower(unsigned long c);
extern bool evc_isgraph(unsigned long c);
extern unsigned long evc_toupper(unsigned long c);
extern unsigned long evc_tolower(unsigned long c);
static inline bool evc_isascii(unsigned long c) { return c < 128; }

/* err.c */
extern void fail(const char *msg, ...);
extern void err_setstr(Object *exc, const char *msg, ...);
extern void err_set_from_user(Object *exc);
extern Object *err_get(void);
extern void err_print(FILE *fp, Object *exc);
extern void err_print_last(FILE *fp);
extern bool err_occurred(void);
extern void err_clear(void);

/* errmsg.c */
extern void err_hashable(Object *obj, const char *fname);
extern void err_iterable(Object *obj, const char *fname);
extern void err_decode(int codec, const char *why);
extern void err_ord(int codec, long ord);
extern void err_attribute(const char *getorset,
                          Object *deref, Object *obj);
extern void err_subscript(const char *getorset,
                          Object *deref, Object *obj);
extern void err_index(Object *index);
extern void err_argtype(const char *what);
extern void err_locked(void);
extern void err_permit(const char *op, Object *var);
extern void err_permit2(const char *op, Object *a, Object *b);
extern void err_errno(const char *msg, ...);
extern void err_notreal(const char *tpname);
extern void err_doublearg(const char *argname);
extern void err_exactargs(int nargs, int expect);
extern void err_minargs(int nargs, int expect);
extern void err_frame_minargs(Frame *fr, int expect);
extern void err_va_minargs(Object *varargs, int expect);
extern void err_maxargs(int nargs, int expect);

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

/* types/array.c */
extern Object *arrayvar_new(int n_items);
extern Object *arrayvar_from_stack(Object **items, int n_items, bool consume);
extern enum result_t array_setitem(Object *array, size_t i, Object *child);
extern Object *array_getitem(Object *array, size_t idx);
extern enum result_t array_append(Object *array, Object *child);
extern enum result_t array_extend(Object *array, Object *seq);
extern void array_reverse(Object *array);
extern Object *array_borrowitem(Object *array, size_t idx);
extern enum result_t array_delete_chunk(Object *array,
                                        size_t at, size_t n_items);
extern ssize_t array_indexof(Object *arr, Object *item);
extern ssize_t array_rindexof(Object *arr, Object *item);
extern ssize_t array_indexof_strict(Object *arr, Object *item);
extern Object *array_getslice(Object *obj, ssize_t start,
                              ssize_t stop, ssize_t step);

/* types/bytes.c */
extern Object *bytesvar_new(const unsigned char *buf, size_t len);
extern Object *bytesvar_from_source(char *src);
extern const unsigned char *bytes_getbuf(Object *v);
extern Object *bytesvar_nocopy(const unsigned char *buf, size_t len);
extern Object *bytes_getslice(Object *bytes, ssize_t start,
                              ssize_t stop, ssize_t step);
extern Object *bytesvar_new_sg(size_t size, ...);

/* types/cell.c */
extern Object *cellvar_new(Object *value);
extern Object *cell_get_value(Object *cell);
extern void cell_replace_value(Object *cell, Object *new_value);

/* types/class.c */
extern Object *classvar_new(Object *bases, Object *dict,
                            Object *name, Object *priv);
extern bool class_issubclass(Object *class, Object *base);
extern Object *class_get_name(Object *class);
extern Object *instance_super_getattr(Object *instance,
                                      Object *attribute_name);
extern Object *instancevar_new(Object *class, Object *args,
                               Object *kwargs, bool call_init);
extern Object *instance_get_class(Object *instance);
extern void *instance_get_priv(Object *instance);
extern void instance_set_priv(Object *instance,
                              void (*cleanup)(void *), void *priv);
extern Object *instance_call(Object *instance, Object *method_name,
                             Object *args, Object *kwargs);
extern Object *instance_getattr(Frame *fr, Object *instance, Object *key);
extern enum result_t instance_setattr(Frame *fr, Object *instance,
                                      Object *key, Object *value);
extern Object *instance_dir(Object *instance);
extern bool instance_instanceof(Object *instance, Object *class);

/* types/comlex.c */
extern Object *complexvar_new(double real, double imag);

/* types/empty.c */
extern Object *emptyvar_new(void);

/* types/file.c */
extern Object *filevar_new(FILE *fp, Object *name, unsigned int mode);
extern enum result_t file_write(Object *file, Object *data);

/* types/float.c */
extern Object *floatvar_new(double value);

/* types/function.c */
extern Object *funcvar_new_user(Object *ex, Object *argspec);
extern Object *funcvar_new_intl(Object *(*cb)(Frame *));
extern Object *funcvar_from_lut(const struct type_method_t *tbl);
extern int function_setattr(Object *func, int attr, int value);
extern Object *function_call(Frame *fr, Object *func, Object *args,
                             Object *kwargs);
extern void function_add_closure(Object *func, Object *clo);
extern void function_add_default(Object *func,
                        Object *deflt, int argno);
extern Object *function_get_executable(Object *func);

/* types/generator.c */
extern Object *generatorvar_new(Frame *frame);

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
                        const struct type_method_t *tbl);
extern Object *dict_keys(Object *obj, bool sorted);
extern Object *dict_getitem(Object *o, Object *key);
extern Object *dict_getitem_cstr(Object *o, const char *cstr_key);
extern enum result_t dict_setitem(Object *o, Object *key, Object *attr);
extern void dict_add_to_globals(Object *obj);
extern enum result_t dict_setitem_replace(Object *dict,
                                Object *key, Object *attr);
extern enum result_t dict_setitem_exclusive(Object *dict,
                                Object *key, Object *attr);
extern int dict_copyto(Object *to, Object *from);

/* types/method.c */
extern int methodvar_tofunc(Object *meth,
                            Object **func, Object **owner);
extern Object *methodvar_new(Object *func, Object *owner);
extern Object *method_peek_self(Object *meth);

/* types/property.c */
extern Object *propertyvar_new_intl(const struct type_prop_t *props);
extern Object *propertyvar_new_user(Object *setter, Object *getter);
extern Object *property_get(Object *prop, Object *owner, Object *name);
extern enum result_t property_set(Object *prop, Object *owner,
                                  Object *value, Object *name);

/* types/set.c */
extern Object *setvar_new(Object *seq);
extern enum result_t set_extend(Object *set, Object *seq);
extern enum result_t set_additem(Object *set, Object *child, Object **unique);
extern Object *set_unique(Object *set, Object *item);
extern Object *set_intern(Object *set, Object *item);
extern bool set_hasitem(Object *set, Object *item);

/* types/string.c */
extern Object *stringvar_new(const char *cstr);
extern Object *stringvar_newn(const char *cstr, size_t n);
extern Object *stringvar_from_source(const char *tokenstr, bool imm);
extern Object *stringvar_from_binary(const void *data, size_t n, int encoding);
extern Object *stringvar_from_format(const char *fmt, ...);
extern Object *stringvar_from_vformat(const char *fmt, va_list ap);
extern Object *stringvar_from_ascii(const char *cstr);
extern Object *stringvar_from_substr(Object *old, size_t start, size_t stop);
extern long string_ord(Object *str, size_t idx);
extern Object *string_format(Object *str, Object *tup);
extern Object *string_cat(Object *romeo, Object *juliet);
extern size_t string_slide(Object *str, Object *sep, size_t startpos);
extern bool string_chr(Object *str, long pt);
extern ssize_t string_search(Object *haystack, Object *needle, size_t startpos);
extern char *string_encode_utf8(Object *str, size_t *size);

/* types/tuple.c */
extern enum result_t tuple_validate(Object *tup, const char *descr,
                                    bool map_function);
extern Object *tuplevar_from_stack(Object **items, int n_items, bool consume);
extern Object *tuplevar_new(int n_items);
extern Object *tuple_getitem(Object *tup, size_t idx);
extern Object *tuple_borrowitem(Object *tup, size_t idx);

#endif /* EVILCANDY_H */
