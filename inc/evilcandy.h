/*
 * evilcandy.h - Main API header for EvilCandy
 */
#ifndef EGQ_H
#define EGQ_H

#include <lib/hashtable.h>
#include <lib/helpers.h>
#include <lib/buffer.h>
#include <lib/list.h>
#include "instructions.h" /* TODO: remove */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

enum {
        /* Tunable parameters */
        RECURSION_MAX   = 256,

        /* for vm.c and assembler.c */
        VM_STACK_SIZE   = 1024 * 16,
        FRAME_ARG_MAX   = 24,
        FRAME_STACK_MAX = 128,
        FRAME_NEST_MAX  = 32,
        FRAME_CLOSURE_MAX = 24,
};

/**
 * DOC: Magic numbers for built-in typedefs
 * @TYPE_EMPTY:         Uninitialized variable
 * @TYPE_DICT:          Object, or to be egg-headed and more precise, an
 *                      associative array
 * @TYPE_FUNCTION:      Function callable by script.
 * @TYPE_FLOAT:         Floating point number
 * @TYPE_INT:           Integer number
 * @TYPE_STRING:        C-string and some useful metadata
 * @TYPE_LIST:          Numerical array, ie. [ a, b, c...]-type array
 * @NTYPES_USER:        Boundary to check a magic number against
 *
 * These are used for serializing and some text representations,
 * but not for the normal type operations, which use the struct type_t's
 * defined in typedefs.h
 */
enum type_magic_t {
        TYPE_EMPTY = 0,
        TYPE_DICT,
        TYPE_FUNCTION,
        TYPE_FLOAT,
        TYPE_INT,
        TYPE_STRING,
        TYPE_LIST,
        NTYPES_USER,

        /*
         * internal use, user should never be able to access these below
         */

        TYPE_STRPTR = NTYPES_USER,
        TYPE_XPTR,
        NTYPES,
};

/**
 * DOC: Flags for scope of currently parsed or executed expression
 * @FE_FOR: We're in that middle part of a for loop between two
 *          semicolons.  Only used by assembler.
 * @FE_TOP: We're at the top level, ie. not in a function.  In assembler
 *          this has the additional meaning that we are not inside of any
 *          program flow statement, even if we're at the script level.
 *
 * 99% for assembler.c use only, but pulled out to this header because
 * FE_TOP is handy for extra info in disassembler and VM
 */
enum {
        FE_FOR = 0x01,
        FE_TOP = 0x02,
};

/**
 * DOC: Statuses passed around during runtime, ie. after parsing.
 *
 * Fatal errors--mostly bug traps or running out of memory--cause the
 * program to exit immediately after printing an error message, so they
 * don't have any return values enumerated.  The following are for
 * runtime (ie. post-parsing) errors caused by user, system errors that
 * are not considered fatal, or exceptions intentionally raised by the
 * user.  They will eventually trickle their way back into the VM's
 * main loop, which will decide what to do next.
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
struct token_t;
struct token_state_t;

/**
 * struct var_t - User variable type
 * @magic: Magic number to determine which builtin type
 * @v_refcnt: DON'T TOUCH THIS! Use VAR_INCR_REF and VAR_DECR_REF instead
 *
 * built-in types have their own XXXXvar_t struct, which embeds this at
 * the very top, so they can be de-referenced with a simple cast.
 *
 * These are allocated with var_new.  After that, VAR_INCR/DECR_REF
 * is used to produce or consume a reference.
 */
struct var_t {
        struct type_t *v_type;
        union {
                void *v_dummy; /* keep v_refcnt same size as v_type */
                int v_refcnt;  /* signed for easier bug trapping */
        };
};

struct block_t {
        struct var_t **stack_level;
        unsigned char type;
};

/* FIXME: Needs to be more private than this */
struct vmframe_t {
        struct var_t *owner, *func;
        struct var_t **stackptr;
        struct var_t **stack;
        struct executable_t *ex;
        int ap;
        int n_blocks;
        struct block_t blocks[FRAME_NEST_MAX];
        instruction_t *ppii;
        struct var_t **clo;
        struct list_t alloc_list;
#ifndef NDEBUG
        bool freed;
#endif
};

/**
 * struct global_t - This program's global data, declared as q_
 * @recursion:  For the RECURSION_INCR and RECURSION_DECR macros,
 *              to keep check on excess recursion with our eval()
 *              and expression() functions.
 * @opt:        Command-line options
 */
struct global_t {
        int recursion;
        struct {
                bool disassemble;
                bool disassemble_only;
                char *disassemble_outfile;
                char *infile;
        } opt;
};

#ifndef NDEBUG
# define DBUG(msg, args...) \
        fprintf(stderr, "[EvilCandy DEBUG]: " msg "\n", ##args)
# define DBUG_FN(msg)   \
        DBUG("function %s line %d: %s", __FUNCTION__, __LINE__, msg)
#else
# define DBUG(...)      do { (void)0; } while (0)
# define DBUG_FN(msg)   do { (void)0; } while (0)
#endif

#define RECURSION_INCR() do { \
        if (q_.recursion >= RECURSION_MAX) \
                fail("Recursion overflow"); \
        q_.recursion++; \
} while (0)

#define RECURSION_DECR() do { \
        bug_on(q_.recursion <= 0); \
        q_.recursion--; \
} while (0)

/* main.c */
extern struct global_t q_;
extern void load_file(const char *filename, struct vmframe_t *fr);
extern struct var_t *ErrorVar;
extern struct var_t *NullVar;

/* assembler.c */
extern struct executable_t *assemble(const char *filename,
                        FILE *fp, bool toeof, int *status);

/* builtin/builtin.c */
/*
 * These global variables probably ought to be in struct global_t,
 * but do I really want to type "q_.Something" all over the place?
 */
extern struct var_t *GlobalObject;
extern struct var_t *ParserError;
extern struct var_t *RuntimeError;
extern struct var_t *SystemError;
extern void moduleinit_builtin(void);

/* err.c */
#ifndef NDEBUG
# define bug() bug__(__FILE__, __LINE__)
# define bug_on(cond_) do { if (cond_) bug(); } while (0)
#else
# define bug()          do { (void)0; } while (0)
# define bug_on(...)    do { (void)0; } while (0)
#endif
#define breakpoint() breakpoint__(__FILE__, __LINE__)

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

extern void bug__(const char *, int);
extern void breakpoint__(const char *file, int line);

/* disassemble.c */
extern void disassemble(FILE *fp, struct executable_t *ex,
                        const char *sourcefile_name);
extern void disassemble_lite(FILE *fp, struct executable_t *ex);

/* ewrappers.c */
extern char *estrdup(const char *s);
extern void *emalloc(size_t size);
extern void *ecalloc(size_t size);
extern void *erealloc(void *buf, size_t size);

/* types/float.c */
extern struct var_t *floatvar_new(double value);
/* types/integer.c */
extern struct var_t *intvar_new(long long value);

/* json.c */
struct var_t *dict_from_json(const char *filename);

/* keyword.c */
extern int keyword_seek(const char *s);
extern void moduleinit_keyword(void);

/* lex.c */
extern void token_state_trim(struct token_state_t *state);
extern void token_state_free(struct token_state_t *state);
extern struct token_state_t *token_state_new(FILE *fp,
                                        const char *filename);
extern int get_tok(struct token_state_t *state, struct token_t **tok);
extern void unget_tok(struct token_state_t *state, struct token_t **tok);
extern void moduleinit_token(void);

/* literal.c */
extern struct hashtable_t literal_htbl__;
/* see comments above literal.c for usage */
static inline char *literal_put(const char *key)
{
        return hashtable_put_literal(&literal_htbl__, key);
}
static inline char *literal(const char *key)
{
        return hashtable_get(&literal_htbl__, key);
}
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

/* var.c */
extern struct var_t *var_new(struct type_t *type);
extern void var_initialize_type(struct type_t *tp);
extern struct var_t *var_getattr(struct var_t *v,
                                 struct var_t *deref);
extern enum result_t var_setattr(struct var_t *v,
                                 struct var_t *deref,
                                 struct var_t *attr);
extern int var_compare(struct var_t *a, struct var_t *b);
extern const char *typestr(struct var_t *v);
extern const char *typestr_(int magic);
extern const char *attr_str(struct var_t *deref);
/* common hashtable callback for var-storing hashtables */
extern void var_bucket_delete(void *data);
/* note: v only evaluated once in VAR_*_REF() */
#define VAR_INCR_REF(v) do { (v)->v_refcnt++; } while (0)
#define VAR_DECR_REF(v) do {      \
        struct var_t *v_ = (v);   \
        v_->v_refcnt--;           \
        if (v_->v_refcnt <= 0)    \
                var_delete__(v_); \
} while (0)
#ifndef NDEBUG
  /*
   * keep this a macro so I can tell where the bug was trapped
   * I'd like to also sanity-check v_->v_type, but that's probably
   * too many checks for even debug mode.
   */
# define VAR_SANITY(v_) do {                            \
        struct var_t *v__ = (v_);                       \
        if (!v__) {                                     \
                DBUG("unexpected NULL var");            \
                bug();                                  \
        }                                               \
        if (v__->v_refcnt <= 0) {                       \
                DBUG("v_refcnt=%d", v__->v_refcnt);     \
                bug();                                  \
        }                                               \
} while (0)
#else
# define VAR_SANITY(v_) do { (void)0; } while (0)
#endif
extern void var_delete__(struct var_t *v);
extern void moduleinit_var(void);

/* serializer.c */
extern int serialize_write(FILE *fp, struct executable_t *ex);
extern struct executable_t *serialize_read(FILE *fp,
                                        const char *file_name);

/* types/array.c */
extern struct var_t *array_child(struct var_t *array, int idx);
extern enum result_t array_append(struct var_t *array,
                                  struct var_t *child);
extern enum result_t array_insert(struct var_t *array,
                                  struct var_t *idx, struct var_t *child);
extern struct var_t *arrayvar_new(void);
extern int array_get_type(struct var_t *array);
extern int array_length(struct var_t *array);
extern void array_sort(struct var_t *array);

/* types/empty.c */
extern struct var_t *emptyvar_new(void);

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

/* types/intl.c */
extern struct var_t *xptrvar_new(struct executable_t *x);
extern struct var_t *uuidptrvar_new(char *uuid);
extern char *uuidptr_get_cstring(struct var_t *v);

/* types/object.c */
extern struct var_t *objectvar_new(void);
extern struct var_t *object_getattr(struct var_t *o, const char *s);
extern enum result_t object_addattr(struct var_t *o,
                                    struct var_t *v, const char *name);
extern enum result_t object_setattr(struct var_t *o,
                                    struct var_t *name, struct var_t *attr);
extern enum result_t object_remove_child(struct var_t *o, const char *s);
extern void object_set_priv(struct var_t *o, void *priv,
                      void (*cleanup)(struct var_t *, void *));
extern void *object_get_priv(struct var_t *o);
extern void object_add_to_globals(struct var_t *obj);

/* types/string.c */
extern void string_assign_cstring(struct var_t *str, const char *s);
extern struct var_t *string_nth_child(struct var_t *str, int idx);
extern size_t string_length(struct var_t *str);
extern char *string_get_cstring(struct var_t *str);
extern struct var_t *string_from_file(FILE *fp,
                                      int delim, bool stuff_delim);
extern struct var_t *stringvar_new(const char *cstr);
struct var_t *stringvar_from_immortal(const char *immstr);

/* uuid.c */
extern char *uuidstr(void);

/* vm.c */
extern enum result_t vm_execute(struct executable_t *top_level,
                                struct vmframe_t *fr);
extern struct var_t *execute_loop(struct vmframe_t *fr);
extern struct var_t *vm_reenter(struct vmframe_t *fr, struct var_t *func,
                                struct var_t *owner, int argc,
                                struct var_t **argv);
extern void vm_add_global(const char *name, struct var_t *var);
extern void moduleinit_vm(void);
static inline struct var_t *vm_get_this(struct vmframe_t *fr)
        { return fr->owner; }
static inline struct var_t *vm_get_arg(struct vmframe_t *fr, unsigned int idx)
        { return idx >= fr->ap ? NULL : fr->stack[idx]; }

/* TODO: Get rid of references to frame_get_arg */
# define frame_get_arg(fr, i)   vm_get_arg(fr, i)
# define get_this(fr)           vm_get_this(fr)


#endif /* EGQ_H */
