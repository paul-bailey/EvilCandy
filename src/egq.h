#ifndef EGQ_H
#define EGQ_H

#include "opcodes.h"
#include "list.h"
#include "hashtable.h"
#include <stdbool.h>
#include <stdio.h>

enum {
        /* Tunable parameters */
        QSTACKMAX = 8192,
        NS_STACKSIZE = 128,

        /* magic numbers */
        QEMPTY_MAGIC = 0,
        QOBJECT_MAGIC,
        QFUNCTION_MAGIC,
        QFLOAT_MAGIC,
        QINT_MAGIC,
        QSTRING_MAGIC,
        QPTRX_MAGIC,
        QINTL_MAGIC,
        QARRAY_MAGIC,
        Q_NMAGIC,

        /* q_.charmap flags */
        QDELIM = 0x01,
        QIDENT = 0x02,
        QIDENT1 = 0x04,
        QDDELIM = 0x08,
};

/**
 * struct token_t - Handle to metadata about a dynamically allocated
 *                  string
 * @s:  Pointer to the C string.  After token_init(), it's always either
 *      NULL or nulchar-terminated.
 * @p:  Current index into @s following last character in this struct.
 * @size: Size of allocated data for @s.
 *
 * WARNING!  @s is NOT a constant pointer!  A call to token_put{s|c}()
 *      may invoke a reallocation function that moves it.  So do not
 *      store the @s pointer unless you are finished filling it in.
 *
 * ANOTHER WARNING!!!!   Do not use the string-related functions
 *                      on the same token where you use token_putcode
 *
 * XXX: Poorly specific name for what's being used for more than
 * just tokens.
 */
struct token_t {
        union {
                char *s;
                struct opcode_t *oc;
        };
        ssize_t p;
        ssize_t size;
};

struct type_t {
        const char *name;
        struct list_t methods;
};

/**
 * struct ns_t - metadata for everything between a @script...@end block
 * @list:       Sibling list
 * @pgm:        Text of the script as a C string
 * @fname:      File name of this script.
 *
 * FIXME: Badly named, this isn't a namespace.
 * struct object_t is the closest thing we have to a namespace.
 */
struct ns_t {
        struct list_t list;
        struct token_t pgm;
        char *fname;
};

/**
 * struct marker_t - Used for saving a place, either for
 *      declaring a symbol or for recalling an earlier token.
 */
struct marker_t {
        struct ns_t *ns;
        struct opcode_t *oc;
};

struct qvar_t;

/**
 * struct func_intl_t - descriptor for built-in function
 * @fn:         Pointer to the function
 * @minargs:    Minimum number of arguments allowed
 * @maxargs:    <0 if varargs allowed, maximum number of args
 *              (usu.=minargs) if not.
 */
struct func_intl_t {
        void (*fn)(struct qvar_t *ret);
        int minargs;
        int maxargs;
};

struct object_handle_t {
        struct list_t children;
        int nref;
};

/*
 * symbol types - object, function, float, integer, string
 */
struct qvar_t {
        unsigned long magic;
        char *name;
        /*
         * TODO: waste alert! unused if stack var.
         * maybe need special case for member var
         */
        struct list_t siblings;
        union {
                struct {
                        struct qvar_t *owner;
                        struct object_handle_t *h;
                } o;
                struct {
                        struct qvar_t *owner;
                        struct marker_t mk;
                } fn;
                /*
                 * FIXME: This makes numerical arrays (which can get
                 * quite large) extremely inefficient, in both
                 * computation and memory usage.  I need some kind
                 * of look-up table, compressed to the size of the
                 * values instead.
                 */
                struct list_t a;
                double f;
                long long i;
                const struct func_intl_t *fni;
                struct token_t s;
                struct marker_t px;
                struct qvar_t *ps;
        };
};

struct opcode_t {
        unsigned int t;
        unsigned int line; /* for error tracing */
        char *s;
        union {
                double f;
                long long i;
        };
};

/* This library's private data */
struct global_t {
        struct hashtable_t *kw_htbl;
        struct hashtable_t *literals;
        struct qvar_t *gbl; /* "__gbl__" as user sees it */
        struct list_t ns;
        struct qvar_t pc;  /* "program counter" */
        struct qvar_t *fp; /* "frame pointer" */
        struct qvar_t *sp; /* "stack pointer" */
        struct qvar_t lr;  /* "link register */
        struct qvar_t stack[QSTACKMAX];
};

/* I really hate typing this everywhere */
#define cur_oc  q_.pc.px.oc
#define cur_ns  q_.pc.px.ns

/* main.c */
extern struct global_t q_;
extern const char *typestr(int magic);
extern const char *nameof(struct qvar_t *v);
static inline struct qvar_t *get_this(void) { return q_.fp; }

/* helpers for return value of qlex */
static inline int tok_delim(int t) { return (t >> 8) & 0x7fu; }
static inline int tok_type(int t) { return t & 0x7fu; }
static inline int tok_keyword(int t) { return (t >> 8) & 0x7fu; }

/* builtin.c */
extern void moduleinit_builtin(void);
extern struct qvar_t *builtin_method(struct qvar_t *v,
                                const char *method_name);
extern struct qvar_t *ebuiltin_method(struct qvar_t *v,
                                const char *method_name);

/* file.c */
extern void file_push(const char *name);
extern char *next_line(unsigned int flags);

/* err.c */
#define bug() bug__(__FILE__, __LINE__)
#define breakpoint() breakpoint__(__FILE__, __LINE__)
#define bug_on(cond_) do { if (cond_) bug(); } while (0)
extern void syntax(const char *msg, ...);
extern void fail(const char *msg, ...);
extern void warning(const char *msg, ...);
extern void bug__(const char *, int);
extern void breakpoint__(const char *file, int line);
extern void err_expected__(int opcode);
static inline void
expect(int opcode)
{
        if (cur_oc->t != opcode)
                err_expected__(opcode);
}

/* eval.c */
extern void eval(struct qvar_t *v);
extern void eval_safe(struct qvar_t *v);

/* exec.c */
extern void exec_block(void);
extern void call_function(struct qvar_t *fn,
                        struct qvar_t *retval, struct qvar_t *owner);

/* file.c */
extern void load_file(const char *filename);

/* helpers.c */
extern char *estrdup(const char *s);
extern void *emalloc(size_t size);
extern void *ecalloc(size_t size);
extern int x2bin(int c);
static inline bool isodigit(int c) { return c >= '0' && c <= '7'; }
static inline bool isquote(int c) { return c == '"' || c == '\''; }
extern char *my_strrchrnul(const char *s, int c);
/* Why isn't this in stdlib.h? */
#define container_of(x, type, member) \
        ((type *)(((void *)(x)) - offsetof(type, member)))

/* lex.c */
extern int qlex(void);
extern void q_unlex(void);
extern struct ns_t *prescan(const char *filename);
extern void initialize_lexer(void);


/* literal.c */
extern char *literal(const char *s);

/* op.c */
extern void qop_mul(struct qvar_t *a, struct qvar_t *b);
extern void qop_div(struct qvar_t *a, struct qvar_t *b);
extern void qop_mod(struct qvar_t *a, struct qvar_t *b);
extern void qop_add(struct qvar_t *a, struct qvar_t *b);
extern void qop_sub(struct qvar_t *a, struct qvar_t *b);
extern void qop_cmp(struct qvar_t *a, struct qvar_t *b, int op);
extern void qop_shift(struct qvar_t *a, struct qvar_t *b, int op);
extern void qop_bit_and(struct qvar_t *a, struct qvar_t *b);
extern void qop_bit_or(struct qvar_t *a, struct qvar_t *b);
extern void qop_xor(struct qvar_t *a, struct qvar_t *b);
extern void qop_land(struct qvar_t *a, struct qvar_t *b);
extern void qop_lor(struct qvar_t *a, struct qvar_t *b);
extern void qop_mov(struct qvar_t *to, struct qvar_t *from);
extern bool qop_cmpz(struct qvar_t *v);
/* for assigning literals */
extern void qop_assign_cstring(struct qvar_t *v, const char *s);
extern void qop_assign_int(struct qvar_t *v, long long i);
extern void qop_assign_float(struct qvar_t *v, double f);

/* stack.c */
extern void stack_pop(struct qvar_t *to);
extern struct qvar_t *stack_getpush(void);
extern void stack_push(struct qvar_t *v);

/* symbol.c */
extern struct qvar_t *symbol_seek(const char *s);
extern void symbol_walk(struct qvar_t *result,
                        struct qvar_t *parent, bool expression);

/* token.c */
extern void token_init(struct token_t *tok);
extern void token_reset(struct token_t *tok);
extern void token_putc(struct token_t *tok, int c);
extern void token_puts(struct token_t *tok, const char *s);
extern void token_rstrip(struct token_t *tok);
extern void token_free(struct token_t *tok);
extern void token_putcode(struct token_t *tok, struct opcode_t *oc);

/* var.c */
extern struct qvar_t *qvar_init(struct qvar_t *v);
extern struct qvar_t *qvar_new(void);
extern void qvar_delete(struct qvar_t *v);
extern void qvar_reset(struct qvar_t *v);
extern struct qvar_t *qobject_new(struct qvar_t *owner, const char *name);
extern struct qvar_t *qobject_from_empty(struct qvar_t *v);
extern struct qvar_t *qobject_child(struct qvar_t *o, const char *s);
extern struct qvar_t *qobject_nth_child(struct qvar_t *o, int n);
extern void qobject_add_child(struct qvar_t *o, struct qvar_t *v);
extern struct qvar_t *qarray_child(struct qvar_t *array, int n);
extern void qarray_add_child(struct qvar_t *array, struct qvar_t *child);
extern struct qvar_t *qarray_from_empty(struct qvar_t *array);

/* Indexed by Q*_MAGIC */
extern struct type_t TYPEDEFS[];

#endif /* EGQ_H */
