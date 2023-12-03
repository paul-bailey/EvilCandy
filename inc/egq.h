#ifndef EGQ_H
#define EGQ_H

#define ONLY_VM 1

#include "hashtable.h"
#include "helpers.h"
#include "buffer.h"
#include "opcodes.h"
#include "trie.h"
#include "list.h"
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
        STACK_MAX       = 8192,
        LOAD_MAX        = 128,
        RECURSION_MAX   = 256,
        CALL_DEPTH_MAX  = 256,
        FRAME_DEPTH_MAX = CALL_DEPTH_MAX * 2,

        /* for vm.c and assemble.c */
        FRAME_ARG_MAX   = 24,
        FRAME_STACK_MAX = 128,
        FRAME_NEST_MAX  = 32,
        FRAME_CLOSURE_MAX = 24,
};

/**
 * DOC: Magic numbers for built-in typedefs
 * @QEMPTY_MAGIC:       Uninitialized variable
 * @QOBJECT_MAGIC:      Object, or to be egg-headed and more precise, an
 *                      associative array
 * @QFUNCTION_MAGIC:    Function callable by script.
 * @QFLOAT_MAGIC:       Floating point number
 * @QINT_MAGIC:         Integer number
 * @QSTRING_MAGIC:      C-string and some useful metadata
 * @QARRAY_MAGIC:       Numerical array, ie. [ a, b, c...]-type array
 * @Q_NMAGIC:           Boundary to check a magic number against
 */
enum type_magic_t {
        QEMPTY_MAGIC = 0,
        QOBJECT_MAGIC,
        QFUNCTION_MAGIC,
        QFLOAT_MAGIC,
        QINT_MAGIC,
        QSTRING_MAGIC,
        QARRAY_MAGIC,
        Q_NMAGIC,

        /* internal use, user should never be able to access these */
        Q_STRPTR_MAGIC = Q_NMAGIC,
        /*
         * FIXME: This causes lots of inefficient de-referencing.
         * The problem is, floats and ints are pass-by-value.  If a
         * dictionary's attribute is pushed onto the stack and then
         * modified, and that attribute is float or int, the dictionary's
         * attribute will not be truly changed.  So we have this VARPTR
         * type, which is just a pointer to another struct var_t.
         */
        Q_VARPTR_MAGIC,
        Q_XPTR_MAGIC,
};

/* XXX: needed outside of assembly.c? */
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

/*
 * Per-type callbacks for mathematical operators, like + or -
 */
struct operator_methods_t {
        void (*mul)(struct var_t *, struct var_t *);    /* a = a * b */
        void (*div)(struct var_t *, struct var_t *);    /* a = a / b */
        void (*mod)(struct var_t *, struct var_t *);    /* a = a % b */
        void (*add)(struct var_t *, struct var_t *);    /* a = a + b */
        void (*sub)(struct var_t *, struct var_t *);    /* a = a - b */

        /* <0 if a<b, 0 if a==b, >0 if a>b, doesn't set a or b */
        int (*cmp)(struct var_t *, struct var_t *);

        void (*lshift)(struct var_t *, struct var_t *); /* a = a << b */
        void (*rshift)(struct var_t *, struct var_t *); /* a = a >> b */
        void (*bit_and)(struct var_t *, struct var_t *); /* a = a & b */
        void (*bit_or)(struct var_t *, struct var_t *); /* a = a | b */
        void (*xor)(struct var_t *, struct var_t *);    /* a = a ^ b */
        bool (*cmpz)(struct var_t *);                   /* a == 0 ? */
        void (*incr)(struct var_t *);                   /* a++ */
        void (*decr)(struct var_t *);                   /* a-- */
        void (*bit_not)(struct var_t *);                /* ~a */
        void (*negate)(struct var_t *);                 /* -a */
        void (*mov)(struct var_t *, struct var_t *);    /* a = b */

        /*
         * hard reset, clobber var's type as well.
         * Used for removing temporary vars from stack or freeing heap
         * vars; if any type-specific garbage collection needs to be
         * done, declare it here, or leave NULL for the generic cleanup.
         */
        void (*reset)(struct var_t *);
};

/**
 * struct type_t - Used to get info about a typedef
 * @name:       Name of the type
 * @methods:    Linked list of built-in methods for the type; these are
 *              things scripts call as functions.
 * @reset:      Callback to reset the variable, or NULL if no special
 *              action is needed.
 * @opm:        Callbacks for performing primitive operations like
 *              + or - on type
 */
struct type_t {
        const char *name;
        struct list_t methods;
        void (*reset)(struct var_t *);
        const struct operator_methods_t *opm;
};

/**
 * struct ns_t - metadata for a loaded script
 * @list:       list of fellow loaded files
 * @pgm:        Byte code of the loaded file
 * @fname:      File name of this script
 *
 * FIXME: Badly named, this isn't a namespace.
 */
struct ns_t {
        struct list_t list;
        struct buffer_t pgm;
        char *fname;
};

/**
 * struct marker_t - Used for saving a place, either for
 *      declaring a symbol or for recalling an earlier token.
 * @ns: Which file we're executing
 * @oc: A pointer into @ns.pgm.oc
 */
struct marker_t {
        struct ns_t *ns;
        struct opcode_t *oc;
};

/*
 * PRIVATE STRUCT, placed here so I can inline some things
 */
struct string_handle_t {
        int nref;
        struct buffer_t b;
};

/**
 * struct object_handle_t - Descriptor for an object handle
 * @children:   List of children members
 * @priv:       Internal private data, used by some built-in object types
 * @priv_cleanup: Way to clean up @priv if destroying this object handle.
 *              If this is NULL and @priv is not NULL, @priv will be
 *              simply freed.
 * @nref:       Number of variables that have a handle to this object.
 *              Used for garbage collection
 *
 * PRIVATE STRUCT, placed here so I can inline some things
 */
struct object_handle_t {
        void *priv;
        void (*priv_cleanup)(struct object_handle_t *, void *);
        int nref;
        int nchildren;
        struct hashtable_t dict;
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
 *
 * The remaining fields are specific to the type, determined by @magic,
 * and are handled privately by the type-specific sources in types/xxx.c
 *
 * floats and integers are pass-by-value, so their values are stored in
 * this struct directly.  The remainder are pass-by-reference; this
 * struct only stores the pointers to their more meaningful data.
 */
struct var_t {
        unsigned int magic;
        unsigned int flags;
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
};


/**
 * struct opcode_t - The byte-code version of a token
 * @t:          Type of opcode, an OC_* enum, or one of "fiuq"
 * @line:       Line number in file where this opcode was parsed,
 *              used for tracing for error messages.
 * @s:          Content of the token parsed
 * @f:          Value of the token, if @t is 'f'
 * @i:          Value of the token, if @t is 'i'
 */
struct opcode_t {
        unsigned int t;
        unsigned int line;
        char *s;
        union {
                double f;
                long long i;
        };
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
        struct list_t ns;
        struct marker_t pc; /* "program counter" */
        int recursion;
        struct frame_t *frame;
        struct {
                bool disassemble;
                bool disassemble_only;
                bool use_vm;
                char *disassemble_outfile;
                char *infile;
        } opt;
        struct list_t executables;
};

/* I really hate typing this everywhere */
#define cur_mk  (&q_.pc)
#define cur_oc  (cur_mk->oc)
#define cur_ns  (cur_mk->ns)

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
extern const char *typestr(int magic);
extern const char *nameof(struct var_t *v);

/* helpers to classify a variable */
static inline bool isconst(struct var_t *v)
        { return !!(v->flags & VF_CONST); }
static inline bool isprivate(struct var_t *v)
        { return !!(v->flags & VF_PRIV); }
/* true if v is float or int */
static inline bool isnumvar(struct var_t *v)
        { return v->magic == QINT_MAGIC || v->magic == QFLOAT_MAGIC; }

/* helpers for return value of qlex */
static inline int tok_delim(int t) { return (t >> 8) & 0x7fu; }
static inline int tok_type(int t) { return t & 0x7fu; }
static inline int tok_keyword(int t) { return (t >> 8) & 0x7fu; }

/* assemble.c */
extern struct executable_t *assemble(struct ns_t *ns);

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
extern struct var_t *ebuiltin_method(struct var_t *v,
                                const char *method_name);
extern char *estrdup(const char *s);
extern void *emalloc(size_t size);
extern void *ecalloc(size_t size);
extern int ebuffer_substr(struct buffer_t *buf, int i);
extern struct var_t *eobject_child(struct var_t *o, const char *s);
extern struct var_t *eobject_child_l(struct var_t *o, const char *s);
extern struct var_t *eobject_nth_child(struct var_t *o, int n);
extern struct var_t *earray_child(struct var_t *array, int n);
extern int earray_set_child(struct var_t *array,
                            int idx, struct var_t *child);
extern struct var_t *esymbol_seek(const char *name);
extern char *eliteral(const char *key);

/* keyword.c */
extern int keyword_seek(const char *s);
extern void moduleinit_keyword(void);

/* lex.c */
extern struct ns_t *prescan(const char *filename);
extern void moduleinit_lex(void);

/* literal.c */
extern char *literal(const char *s);
extern char *literal_put(const char *s);
extern void moduleinit_literal(void);

/* load_file.c */
extern void load_file(const char *filename);

/* mempool.c */
struct mempool_t; /* opaque data type to user */
extern struct mempool_t *mempool_new(size_t datalen);
extern void *mempool_alloc(struct mempool_t *pool);
extern void mempool_free(struct mempool_t *pool, void *data);

/* op.c */
extern void qop_mul(struct var_t *a, struct var_t *b);
extern void qop_div(struct var_t *a, struct var_t *b);
extern void qop_mod(struct var_t *a, struct var_t *b);
extern void qop_add(struct var_t *a, struct var_t *b);
extern void qop_sub(struct var_t *a, struct var_t *b);
extern void qop_cmp(struct var_t *a, struct var_t *b, int op);
extern void qop_shift(struct var_t *a, struct var_t *b, int op);
extern void qop_bit_and(struct var_t *a, struct var_t *b);
extern void qop_bit_or(struct var_t *a, struct var_t *b);
extern void qop_xor(struct var_t *a, struct var_t *b);
extern bool qop_cmpz(struct var_t *v);
extern void qop_incr(struct var_t *v);
extern void qop_decr(struct var_t *v);
extern void qop_bit_not(struct var_t *v);
extern void qop_negate(struct var_t *v);
extern void qop_lnot(struct var_t *v);
extern struct var_t *qop_mov(struct var_t *to, struct var_t *from);
extern void qop_clobber(struct var_t *to, struct var_t *from);
extern bool qop_cmpz(struct var_t *v);
/* for assigning literals */
extern void qop_assign_cstring(struct var_t *v, const char *s);
extern void qop_assign_int(struct var_t *v, long long i);
extern void qop_assign_float(struct var_t *v, double f);
extern void qop_assign_char(struct var_t *v, int c);

/* symbol.c */
extern struct var_t *symbol_seek(const char *s);
extern struct var_t *symbol_seek_stack(const char *s);
extern struct var_t *symbol_seek_stack_l(const char *s);

/* var.c */
extern struct var_t *var_init(struct var_t *v);
extern struct var_t *var_new(void);
extern void var_delete(struct var_t *v);
extern void var_reset(struct var_t *v);
extern void moduleinit_var(void);
extern struct var_t *builtin_method(struct var_t *v,
                                    const char *method_name);
extern void var_bucket_delete(void *data);
extern struct var_t *var_copy_of(struct var_t *v);

/* Indexed by Q*_MAGIC */
extern struct type_t TYPEDEFS[];

/* types/array.c */
extern struct var_t *array_child(struct var_t *array, int idx);
extern void array_add_child(struct var_t *array, struct var_t *child);
extern int array_set_child(struct var_t *array,
                            int idx, struct var_t *child);
extern struct var_t *array_from_empty(struct var_t *array);

/* types/function.c */
extern void call_function(struct var_t *fn,
                        struct var_t *retval, struct var_t *owner);
extern void call_function_from_intl(struct var_t *fn,
                        struct var_t *retval, struct var_t *owner,
                        int argc, struct var_t *argv[]);
/*
 * Creating functions API
 * Built-in:    function_init_internal
 * User-defined:
 *              function_init
 *              function_add_arg ...repeat for ea. arg
 *              function_add_closure ...ditto
 *              function_set_user
 */
extern void function_add_arg(struct var_t *func,
                             char *name, struct var_t *deflt);
extern void function_init(struct var_t *func);
extern void function_set_user(struct var_t *func,
                        const struct marker_t *pc, bool lambda);
extern void function_init_internal(struct var_t *func,
                        void (*cb)(struct var_t *),
                        int minargs, int maxargs);
extern void function_add_closure(struct var_t *func, char *name,
                        struct var_t *init);

/* For the virtual machine in all of us */
extern struct var_t *call_vmfunction_prep_frame(struct var_t *fn,
                        struct vmframe_t *fr, struct var_t *owner);
extern struct var_t *call_vmfunction(struct var_t *fn);
extern void function_vmadd_closure(struct var_t *func, struct var_t *clo);
extern void function_vmadd_default(struct var_t *func,
                        struct var_t *deflt, int argno);
extern void function_init_vm(struct var_t *func,
                        struct executable_t *ex);

/* types/object.c */
extern struct var_t *object_init(struct var_t *v);
extern struct var_t *object_child_l(struct var_t *o, const char *s);
extern struct var_t *object_nth_child(struct var_t *o, int n);
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
static inline struct buffer_t *string_buf__(struct var_t *str)
        { return &str->s->b; }
extern void string_assign_cstring(struct var_t *str, const char *s);
extern struct var_t *string_init(struct var_t *var);
static inline void string_clear(struct var_t *str)
        { string_assign_cstring(str, ""); }
extern int string_substr(struct var_t *str, int i);
static inline size_t
string_length(struct var_t *str)
{
        bug_on(str->magic != QSTRING_MAGIC);
        return buffer_size(string_buf__(str));
}
/*
 * WARNING!! This is not reentrance safe!  Whatever you are doing
 * with the return value, do it now.
 *
 * FIXME: This is also not thread safe, and "do it quick" is not a good
 * enough solution.
 */
static inline char *
string_get_cstring(struct var_t *str)
{
        bug_on(str->magic != QSTRING_MAGIC);
        return str->s->b.s;
}
static inline void
string_putc(struct var_t *str, int c)
{
        bug_on(str->magic != QSTRING_MAGIC);
        buffer_putc(string_buf__(str), c);
}
static inline void
string_puts(struct var_t *str, const char *s)
{
        bug_on(str->magic != QSTRING_MAGIC);
        buffer_puts(string_buf__(str), s);
}

/* vm.c */
extern void vm_execute(struct executable_t *top_level);
extern void moduleinit_vm(void);
extern struct var_t *vm_get_this(void);
extern struct var_t *vm_get_arg(unsigned int idx);
/* TODO: Get rid of references ot frame_get_arg */
# define frame_get_arg(i)       vm_get_arg(i)
# define get_this()             vm_get_this()


#endif /* EGQ_H */
