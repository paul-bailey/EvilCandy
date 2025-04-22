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

/* Tunable parameters */
enum {
        /*
         * XXX: Arbitrary choice for value, do some research and find out
         * if there's a known reason for a specific pick/method for stack
         * overrun protection.
         */
        RECURSION_MAX   = 256,

        /* for vm.c */
        /* TODO: Make VM_STACK_SIZE configurable by the command-line. */
        VM_STACK_SIZE   = 1024 * 16,

        /*
         * These are static definitions of array sizes in struct
         * asframe_t, a temporary struct used by the parser.  I could
         * replace these limits with something more dynamic, though I'm
         * getting sick of calling malloc everywhere.  The heap doesn't
         * grow on trees.
         */
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
struct vmframe_t;

#include "typedefs.h"
#include "uarg.h"

/* TODO: to stringutils.h, see comment at bottom of types/dict.c */
typedef uint64_t hash_t;

/*
 * Global User-Type Variables
 *
 *      When returning NullVar to mean 'null' (as the script user sees
 *      it), produce a reference, just as if you would for any other
 *      variable.  Ditto with GlobalObject, this is what the user sees
 *      as '__gbl__'.
 *
 *      Do not produce a reference for the ErrorVar, since it tells you
 *      an error occurred.  You should never use ErrorVar such that it
 *      could be 'seen' by the user, eg. never push it onto the user
 *      stack.
 *
 *      The others (ParserError et al.) are visible to the user in
 *      __gbl__._builtins.  Produce a reference if they are requested
 *      with the SYMTAB instruction, but do not produce a reference
 *      when passing these as the first argument to err_setstr.
 */
/* main.c */
extern struct var_t *ErrorVar;
extern struct var_t *NullVar;
extern struct var_t *GlobalObject;
extern struct var_t *ParserError;
extern struct var_t *RuntimeError;
extern struct var_t *SystemError;

/* assembler.c */
extern struct var_t *assemble(const char *filename,
                              FILE *fp, bool toeof, int *status);

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
extern void err_permit2(const char *op,
                        struct var_t *a, struct var_t *b);
extern void err_errno(const char *msg, ...);

/* disassemble.c */
extern void disassemble(FILE *fp, struct var_t *ex,
                        const char *sourcefile_name);
extern void disassemble_lite(FILE *fp, struct var_t *ex);

/* ewrappers.c */
extern char *estrdup(const char *s);
extern void *emalloc(size_t size);
extern void *ecalloc(size_t size);
extern void *erealloc(void *buf, size_t size);
extern void *ememdup(void *buf, size_t size);
extern void efree(void *ptr);

/* hash.c */
extern hash_t calc_string_hash(struct var_t *key);

/* json.c */
struct var_t *dict_from_json(const char *filename);

/* op.c */
extern struct var_t *qop_mul(struct var_t *a, struct var_t *b);
extern struct var_t *qop_div(struct var_t *a, struct var_t *b);
extern struct var_t *qop_mod(struct var_t *a, struct var_t *b);
extern struct var_t *qop_add(struct var_t *a, struct var_t *b);
extern struct var_t *qop_sub(struct var_t *a, struct var_t *b);
extern struct var_t *qop_bit_and(struct var_t *a, struct var_t *b);
extern struct var_t *qop_bit_or(struct var_t *a, struct var_t *b);
extern struct var_t *qop_xor(struct var_t *a, struct var_t *b);
extern struct var_t *qop_bit_not(struct var_t *v);
extern struct var_t *qop_negate(struct var_t *v);

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
extern int serialize_write(FILE *fp, struct var_t *ex);
extern struct var_t *serialize_read(FILE *fp, const char *file_name);

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
extern const unsigned char *bytes_getbuf(struct var_t *v);
extern struct var_t *bytesvar_nocopy(unsigned char *buf, size_t len);

/* types/empty.c */
extern struct var_t *emptyvar_new(void);

/* types/file.c */
enum {
        FMODE_BINARY    = 0x01,
        FMODE_READ      = 0x02,
        FMODE_WRITE     = 0x04,
};
struct var_t *filevar_new(FILE *fp,
                        struct var_t *name, unsigned int mode);

/* types/float.c */
extern struct var_t *floatvar_new(double value);

/* types/function.c */
extern struct var_t *funcvar_new_user(struct var_t *ex);
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
extern struct var_t *uuidptrvar_new(char *uuid);
extern char *uuidptr_get_cstring(struct var_t *v);

/* types/dict.c */
extern struct var_t *dictvar_new(void);
extern struct var_t *dict_keys(struct var_t *obj);
extern struct var_t *dict_getattr(struct var_t *o, struct var_t *key);
extern enum result_t dict_setattr(struct var_t *o,
                                    struct var_t *key, struct var_t *attr);
extern void dict_add_to_globals(struct var_t *obj);
extern enum result_t dict_setattr_replace(struct var_t *dict,
                                struct var_t *key, struct var_t *attr);
extern enum result_t dict_setattr_exclusive(struct var_t *dict,
                                struct var_t *key, struct var_t *attr);
extern char *dict_unique(struct var_t *dict, const char *key);

/* types/string.c */
extern void string_assign_cstring(struct var_t *str, const char *s);
extern char *string_get_cstring(struct var_t *str);
extern struct var_t *stringvar_new(const char *cstr);
extern struct var_t *stringvar_from_buffer(struct buffer_t *b);
extern struct var_t *stringvar_from_source(const char *tokenstr, bool imm);
extern struct var_t *stringvar_nocopy(const char *cstr);

/* uuid.c */
extern char *uuidstr(void);

/* vm.c */
#include "vm.h"

#endif /* EVILCANDY_H */
