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

/**
 * DOC: Tunable parameters
 *
 * @STACK_MAX:          Size of evaluation stack
 * @LOAD_MAX:           Max number of external modules that may be loaded
 * @RECURSION_MAX:      Max permissible recursion permitted by eval() and
 *                      expression()
 */
enum {
        /* Tunable parameters */
        RECURSION_MAX   = 256,

        /* for vm.c and assembler.c */
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
 * @TYPE_FUNCTION:    Function callable by script.
 * @TYPE_FLOAT:         Floating point number
 * @TYPE_INT:           Integer number
 * @TYPE_STRING:      C-string and some useful metadata
 * @TYPE_LIST:       Numerical array, ie. [ a, b, c...]-type array
 * @NTYPES_USER:           Boundary to check a magic number against
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

enum {
        FE_FOR = 0x01,
        FE_TOP = 0x02,
};

struct var_t;
struct object_handle_t;
struct array_handle_t;
struct string_handle_t;
struct function_handle_t;
struct frame_t;
struct executable_t;
struct token_t;

/**
 * struct object_handle_t - Descriptor for an object handle
 * @children:   List of children members
 * @priv:       Internal private data, used by some built-in object types
 * @priv_cleanup: Way to clean up @priv if destroying this object handle.
 *              If this is NULL and @priv is not NULL, @priv will be
 *              simply freed.
 * @lock:       Prevent SETATTR, GETATTR during an iterable cycle, such as
 *              foreach.
 *
 * PRIVATE STRUCT, placed here so I can inline some things
 */
struct object_handle_t {
        void *priv;
        void (*priv_cleanup)(struct object_handle_t *, void *);
        int nchildren;
        struct hashtable_t dict;
        unsigned int lock;
};

/**
 * DOC: Variable flags
 * @VF_PRIV:    Private variable, only applies to object members
 * @VF_CONST:   Constant variable, variable can be destroyed, but before
 *              then, it cannot be changed.
 *
 * These are the .flags field of a struct var_t
 */
enum {
        VF_PRIV = 0x1,
        VF_CONST = 0x2,
};


/**
 * struct var_t - User variable type
 * @magic: Magic number to determine which builtin type
 * @flags: a VF_* enum
 * @refcount: DON'T TOUCH THIS! Use VAR_INCR_REF and VAR_DECR_REF instead
 *
 * The remaining fields are specific to the type, determined by @magic,
 * and are handled privately by the type-specific sources in types/xxx.c
 *
 * floats and integers are pass-by-value, so their values are stored in
 * this struct directly.  The remainder are pass-by-reference; this
 * struct only stores the pointers to their more meaningful data.
 *
 * Even though these are small, their object structs might be large,
 * so they can't be carelessly declared on the stack and then discarded.
 * Nor may they be allocated with a simple malloc() or freed with a simple
 * free() call.  Instead, call var_new() to allocate one, and then
 * var_delete() to destroy it (which handles the garbage collection,
 * destructor callbacks, etc.).  Do not memset() it to zero or manually
 * change it, either.  Use things like var_reset(), qop_assign...(), etc.
 * access functions.
 */
struct var_t {
        unsigned int magic;
        unsigned short flags;
        short refcount; /* signed for easier bug trapping */
        union {
                struct object_handle_t *o;
                struct function_handle_t *fn;
                struct array_handle_t *a;
                double f;
                long long i;
                struct string_handle_t *s;

                /* non-user types, only visible in the C code */
                char *strptr;
                struct executable_t *xptr;
                struct var_t *vptr;
        };
};

/* FIXME: Needs to be more private than this */
struct vmframe_t {
        struct var_t *owner, *func;
        struct var_t **stackptr;
        struct var_t *stack[FRAME_STACK_MAX];
        struct executable_t *ex;
        int ap;
        instruction_t *ppii;
        struct var_t **clo;
        struct vmframe_t *prev;
        struct list_t alloc_list;
#ifndef NDEBUG
        bool freed;
#endif
};

/**
 * struct global_t - This program's global data, declared as q_
 * @gbl:        __gbl__, as the user sees it
 * @ns:         Linked list of all loaded files' opcodes in RAM.
 * @pc:         "program counter", often called PC in comments
 * @recursion:  For the RECURSION_INCR and RECURSION_DECR macros,
 *              to keep check on excess recursion with our eval()
 *              and expression() functions.
 * @frame:      Current stack, FP, SP, LR, etc.
 * @opt:        Command-line options
 * @executables: Active executable functions
 */
struct global_t {
        struct var_t *gbl; /* "__gbl__" as user sees it */
        int recursion;
        struct frame_t *frame;
        struct {
                bool disassemble;
                bool disassemble_only;
                char *disassemble_outfile;
                char *infile;
        } opt;
        struct list_t executables;
};

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

/* helpers to classify a variable */
static inline bool isconst(struct var_t *v)
        { return !!(v->flags & VF_CONST); }
static inline bool isprivate(struct var_t *v)
        { return !!(v->flags & VF_PRIV); }
/* true if v is float or int */
static inline bool isnumvar(struct var_t *v)
        { return v->magic == TYPE_INT || v->magic == TYPE_FLOAT; }

/* assembler.c */
extern struct executable_t *assemble(const char *source_file_name,
                                     struct token_t *token_arr);

/* builtin/builtin.c */
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
#define warn_once(...) do {             \
        static bool once_ = false;      \
        if (!once_) {                   \
                warning(__VA_ARGS__);   \
                once_ = true;           \
        }                               \
} while (0)
extern void syntax(const char *msg, ...);
extern void fail(const char *msg, ...);
extern void warning(const char *msg, ...);
extern void bug__(const char *, int);
extern void breakpoint__(const char *file, int line);
extern void err_expected__(int opcode);
#define expect(oc_) do {                \
        if (cur_oc->t != oc_)           \
                err_expected__(oc_);    \
} while (0)

/* compile.c */
extern void compile_function(struct var_t *v);
extern void compile_object(struct var_t *v);
extern void compile_array(struct var_t *v);
extern void compile_lambda(struct var_t *v);

/* disassemble.c */
extern void disassemble_start(FILE *fp, const char *sourcefile_name);
extern void disassemble(FILE *fp, struct executable_t *ex);

/* ewrappers.c */
extern char *estrdup(const char *s);
extern void *emalloc(size_t size);
extern void *ecalloc(size_t size);
extern int ebuffer_substr(struct buffer_t *buf, int i);
extern struct var_t *evar_get_attr(struct var_t *v,
                                   struct var_t *deref);
extern int evar_set_attr(struct var_t *v,
                        struct var_t *deref, struct var_t *attr);

/* types/float.c */
extern struct var_t *float_init(struct var_t *v, double value);
/* types/integer.c */
extern struct var_t *integer_init(struct var_t *v, long long value);

/* keyword.c */
extern int keyword_seek(const char *s);
extern void moduleinit_keyword(void);

/* lex.c */
extern struct token_t *prescan(const char *filename);
extern void moduleinit_lex(void);

/* literal.c */
extern char *literal(const char *s);
extern char *literal_put(const char *s);
extern void moduleinit_literal(void);

/* load_file.c */
extern void load_file(const char *filename);

/* location.c */
extern void getloc_push(unsigned int (*getloc)(const char **, void *),
                        void *data);
extern void getloc_pop(void);
extern unsigned int get_location(const char **file_name);

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
extern struct var_t *qop_cmp(struct var_t *a, struct var_t *b, int op);
extern struct var_t *qop_shift(struct var_t *a, struct var_t *b, int op);
extern struct var_t *qop_bit_and(struct var_t *a, struct var_t *b);
extern struct var_t *qop_bit_or(struct var_t *a, struct var_t *b);
extern struct var_t *qop_xor(struct var_t *a, struct var_t *b);
extern bool qop_cmpz(struct var_t *v);
extern void qop_incr(struct var_t *v);
extern void qop_decr(struct var_t *v);
extern struct var_t *qop_bit_not(struct var_t *v);
extern struct var_t *qop_negate(struct var_t *v);
extern struct var_t *qop_lnot(struct var_t *v);
extern struct var_t *qop_mov(struct var_t *to, struct var_t *from);
#if 0
extern void qop_clobber(struct var_t *to, struct var_t *from);
#endif
extern bool qop_cmpz(struct var_t *v);

/* var.c */
extern struct var_t *var_new(void);
#define VAR_INCR_REF(v) do { (v)->refcount++; } while (0)
#define VAR_DECR_REF(v) do { \
        struct var_t *v_ = (v); \
        v_->refcount--; \
        if (v_->refcount <= 0) \
                var_delete__(v_); \
} while (0)
extern void var_delete__(struct var_t *v);
extern void var_reset(struct var_t *v);
extern void moduleinit_var(void);
extern struct var_t *var_get_attr(struct var_t *v,
                                struct var_t *deref);
extern struct var_t *var_get_attr_by_string_l(struct var_t *v,
                                const char *s);
extern int var_set_attr(struct var_t *v,
                        struct var_t *deref, struct var_t *attr);
extern const char *typestr(struct var_t *v);
extern const char *attr_str(struct var_t *deref);
static inline struct var_t *var_copy(struct var_t *v)
        { return qop_mov(var_new(), v); }

/* common hashtable callback for var-storing hashtables */
extern void var_bucket_delete(void *data);

/* types/array.c */
extern struct var_t *array_child(struct var_t *array, int idx);
extern void array_add_child(struct var_t *array, struct var_t *child);
extern int array_set_child(struct var_t *array,
                            int idx, struct var_t *child);
extern struct var_t *array_from_empty(struct var_t *array);

/* types/function.c */
extern void function_init_internal(struct var_t *func,
                        void (*cb)(struct var_t *),
                        int minargs, int maxargs);
extern struct var_t *function_prep_frame(struct var_t *fn,
                        struct vmframe_t *fr, struct var_t *owner);
extern struct var_t *call_function(struct var_t *fn);
extern void function_add_closure(struct var_t *func, struct var_t *clo);
extern void function_add_default(struct var_t *func,
                        struct var_t *deflt, int argno);
extern void function_init(struct var_t *func,
                        struct executable_t *ex);

/* types/object.c */
extern struct var_t *object_init(struct var_t *v);
extern struct var_t *object_child_l(struct var_t *o, const char *s);
extern void object_add_child(struct var_t *o, struct var_t *v, char *name);
extern void object_set_priv(struct var_t *o, void *priv,
                      void (*cleanup)(struct object_handle_t *, void *));
static inline void *object_get_priv(struct var_t *o)
        { return o->o->priv; }
/*
 * Return:    - the child if found
 *            - the built-in method matching @s if the child is not found
 *            - NULL if neither are found.
 */
static inline struct var_t *
object_child(struct var_t *o, const char *s)
        { return ((s = literal(s))) ? object_child_l(o, s) : NULL; }

/* types/string.c */
extern void string_assign_cstring(struct var_t *str, const char *s);
extern struct var_t *string_init(struct var_t *var, const char *cstr);
extern struct var_t *string_nth_child(struct var_t *str, int idx);
extern size_t string_length(struct var_t *str);
extern char *string_get_cstring(struct var_t *str);
extern void string_init_from_file(struct var_t *ret, FILE *fp,
                                  int delim, bool stuff_delim);

/* vm.c */
extern void vm_execute(struct executable_t *top_level);
extern void vm_reenter(struct var_t *func, struct var_t *owner,
                       int argc, struct var_t **argv);
extern void moduleinit_vm(void);
extern struct var_t *vm_get_this(void);
extern struct var_t *vm_get_arg(unsigned int idx);
/* TODO: Get rid of references ot frame_get_arg */
# define frame_get_arg(i)       vm_get_arg(i)
# define get_this()             vm_get_this()


#endif /* EGQ_H */
