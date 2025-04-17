/*
 * evilcandy.h - Main API header for EvilCandy
 */
#ifndef EVILCANDY_H
#define EVILCANDY_H

/* headers for stuff I may want to port to other projects */
#include <lib/hashtable.h>
#include <lib/helpers.h>
#include <lib/buffer.h>
#include <lib/list.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

/* Tunable parameters */
enum {
        /*
         * XXX: Make RECURSION_MAX configurable by the command-line.
         * Arbitrary choice for value, do some research and find out if
         * there's a known reason for a specific pick/method for stack
         * overrun protection.
         */
        RECURSION_MAX   = 256,

        /* for vm.c and assembler.c */
        VM_STACK_SIZE   = 1024 * 16,
        FRAME_ARG_MAX   = 24,
        FRAME_STACK_MAX = 128,
        FRAME_NEST_MAX  = 32,
        FRAME_CLOSURE_MAX = 24,
};

/**
 * DOC: Result values
 *
 * Fatal errors--mostly bug traps or running out of memory--cause the
 * program to exit immediately after printing an error message, so they
 * don't have any return values enumerated.  The following are for
 * runtime (ie. post-parsing) errors caused by user, system errors that
 * are not considered fatal, or exceptions intentionally raised by the
 * user.  They will eventually trickle their way back into the VM's
 * main loop, which will decide what to do next.
 *
 * For functions that must return a struct var_t (which is like half of
 * them), return ErrorVar if there is an error.  (I'm trying to reduce
 * the number of points where NULL could be returned, since it's so easy
 * to accidentally de-reference them and cause a segmentation fault.)
 *
 * @RES_OK:             Success
 * @RES_EXCEPTION:      User raised an exception
 * @RES_RETURN:         Return from function or script.  Used only by VM
 * @RES_ERROR:          Marklar error. Sometimes I plan ahead and think
 *                      things through.  Other times I type away YOLO-like
 *                      and say "I should return an error code here but I
 *                      haven't defined any yet, so I'll just return my
 *                      trusty old -1 for now and change it later."
 *                      Don't judge, you KNOW you do it too.
 *                      Anyway, it's "later" now, and I can't be bothered
 *                      to track down all those -1s.
 */
enum result_t {
        RES_OK = 0,
        RES_EXCEPTION = 1,
        RES_RETURN = 2,
        RES_ERROR = -1,
};

struct var_t;
struct array_handle_t;
struct string_handle_t;
struct function_handle_t;
struct executable_t;
struct vmframe_t;

#include "typedefs.h"
#include "uarg.h"

/* main.c */
extern void load_file(const char *filename, struct vmframe_t *fr);
extern struct var_t *ErrorVar;
extern struct var_t *NullVar;

/* assembler.c */
extern struct executable_t *assemble(const char *filename,
                        FILE *fp, bool toeof, int *status);

/* builtin/builtin.c */
extern struct var_t *GlobalObject;
extern struct var_t *ParserError;
extern struct var_t *RuntimeError;
extern struct var_t *SystemError;
extern void moduleinit_builtin(void);

#include "debug.h"

/* err.c */
extern void fail(const char *msg, ...);
extern void err_setstr(struct var_t *exc, const char *msg, ...);
extern void err_get(struct var_t **exc, char **msg);
extern bool err_exists(void);
extern void err_print(FILE *fp, struct var_t *exc, char *msg);
extern void err_print_last(FILE *fp);
extern bool err_occurred(void);
extern void err_attribute(const char *getorset,
                          struct var_t *deref, struct var_t *obj);
extern void err_argtype(const char *what);
extern void err_locked(void);
extern void err_mismatch(const char *op);
extern void err_permit(const char *op, struct var_t *var);
extern void err_errno(const char *msg, ...);

/* disassemble.c */
extern void disassemble(FILE *fp, struct executable_t *ex,
                        const char *sourcefile_name);
extern void disassemble_lite(FILE *fp, struct executable_t *ex);

/* ewrappers.c */
extern char *estrdup(const char *s);
extern void *emalloc(size_t size);
extern void *ecalloc(size_t size);
extern void *erealloc(void *buf, size_t size);
extern void *ememdup(void *buf, size_t size);

/* json.c */
struct var_t *dict_from_json(const char *filename);

/* keyword.c */
extern int keyword_seek(const char *s);
extern void moduleinit_keyword(void);

/* literal.c */
extern struct hashtable_t literal_htbl__;
/* see comments above literal.c for usage */
static inline char *literal_put(const char *key)
        { return hashtable_put_literal(&literal_htbl__, key); }
static inline char *literal(const char *key)
        { return hashtable_get(&literal_htbl__, key); }
extern void moduleinit_literal(void);

/* mempool.c */
struct mempool_t; /* opaque data type to user */
extern struct mempool_t *mempool_new(size_t datalen);
extern void *mempool_alloc(struct mempool_t *pool);
extern void mempool_free(struct mempool_t *pool, void *data);

/* op.c */
extern struct var_t *qop_mul(struct var_t *a, struct var_t *b);
extern struct var_t *qop_div(struct var_t *a, struct var_t *b);
extern struct var_t *qop_mod(struct var_t *a, struct var_t *b);
extern struct var_t *qop_add(struct var_t *a, struct var_t *b);
extern struct var_t *qop_sub(struct var_t *a, struct var_t *b);
extern struct var_t *qop_bit_and(struct var_t *a, struct var_t *b);
extern struct var_t *qop_bit_or(struct var_t *a, struct var_t *b);
extern struct var_t *qop_xor(struct var_t *a, struct var_t *b);
extern bool qop_cmpz(struct var_t *v, enum result_t *status);
extern struct var_t *qop_bit_not(struct var_t *v);
extern struct var_t *qop_negate(struct var_t *v);
extern struct var_t *qop_lnot(struct var_t *v);
extern struct var_t *qop_cp(struct var_t *v);

/* find_import.c */
extern FILE *find_import(const char *cur_path, const char *file_name,
                         char *pathfill, size_t size);

/* path.c */
extern void pop_path(FILE *fp);
extern FILE *push_path(const char *filename);

/* range.c */
extern struct var_t *rangevar_new(long long start,
                        long long stop, long long step);

/* var.c */
#include "var.h"

/* serializer.c */
extern int serialize_write(FILE *fp, struct executable_t *ex);
extern struct executable_t *serialize_read(FILE *fp,
                                        const char *file_name);

/* token.c */
extern void moduleinit_token(void);

/* types/array.c */
extern struct var_t *arrayvar_new(int n_items);
extern struct var_t *tuplevar_new(int n_items);
extern enum result_t array_setitem(struct var_t *array,
                                   int i, struct var_t *child);
/* user doesn't have access to this but internal code needs it */
#define tuple_setitem   array_setitem
extern struct var_t *array_getitem(struct var_t *array, int idx);
extern enum result_t array_append(struct var_t *array,
                                  struct var_t *child);

/* types/bytes.c */
extern struct var_t *bytesvar_new(unsigned char *buf, size_t len);
extern struct var_t *bytesvar_from_source(char *src);

/* types/empty.c */
extern struct var_t *emptyvar_new(void);

/* types/float.c */
extern struct var_t *floatvar_new(double value);

/* types/function.c */
extern struct var_t *funcvar_new_user(struct executable_t *ex);
struct var_t *funcvar_new_intl(struct var_t *(*cb)(struct vmframe_t *),
                               int minargs, int maxargs);
extern struct var_t *function_prep_frame(struct var_t *fn,
                        struct vmframe_t *fr, struct var_t *owner);
extern struct var_t *call_function(struct vmframe_t *fr, struct var_t *fn);
extern void function_add_closure(struct var_t *func, struct var_t *clo);
extern void function_add_default(struct var_t *func,
                        struct var_t *deflt, int argno);

/* types/integer.c */
extern struct var_t *intvar_new(long long value);

/* types/intl.c */
extern struct var_t *xptrvar_new(struct executable_t *x);
extern struct var_t *uuidptrvar_new(char *uuid);
extern char *uuidptr_get_cstring(struct var_t *v);

/* types/object.c */
extern struct var_t *objectvar_new(void);
extern struct var_t *object_keys(struct var_t *obj);
extern struct var_t *object_getattr(struct var_t *o, const char *key);
extern enum result_t object_setattr(struct var_t *o,
                                    const char *key, struct var_t *attr);
extern void object_set_priv(struct var_t *o, void *priv,
                      void (*cleanup)(struct var_t *, void *));
extern void *object_get_priv(struct var_t *o);
extern void object_add_to_globals(struct var_t *obj);

/* types/string.c */
extern void string_assign_cstring(struct var_t *str, const char *s);
extern char *string_get_cstring(struct var_t *str);
extern struct var_t *string_from_file(FILE *fp,
                                      int delim, bool stuff_delim);
extern struct var_t *stringvar_new(const char *cstr);
extern struct var_t *stringvar_from_immortal(const char *immstr);
extern struct var_t *stringvar_from_source(const char *tokenstr, bool imm);

/* uuid.c */
extern char *uuidstr(void);

/* vm.c */
#include "vm.h"

#endif /* EVILCANDY_H */
