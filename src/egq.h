#ifndef EGQ_H
#define EGQ_H

#include "opcodes.h"
#include "hashtable.h"
#include <stdbool.h>
#include <stdio.h>

enum {
        /* Tunable parameters */
        QSTACKMAX = 8192,

        /* magic numbers */
        QOBJECT_MAGIC = 0x72346578,
        QFUNCTION_MAGIC,
        QFLOAT_MAGIC,
        QINT_MAGIC,
        QSTRING_MAGIC,
        QEMPTY_MAGIC,
        QPTRX_MAGIC,
        QINTL_MAGIC,

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

struct list_t {
        struct list_t *next;
        struct list_t *prev;
};

/**
 * struct ns_t - metadata for everything between a @script...@end block
 * @next:       Handle to the next @script command for the same namespace
 * @pgm:        Text of the script as a C string
 * @lineno:     Line number of the first line of this script
 * @fname:      File name of this script.
 *
 * FIXME: Badly named, this isn't a namespace.
 * struct object_t is the closest thing we have to a namespace.
 */
struct ns_t {
        struct ns_t *next;
        struct token_t pgm;
        int lineno;
        char *fname;
};

/**
 * struct qmarker_t - Used for saving a place, either for
 *      declaring a symbol or for recalling an earlier token.
 */
struct qmarker_t {
        struct ns_t *ns;
        struct opcode_t *oc;
};

struct qvar_t;

/**
 * struct qfunc_intl_t - descriptor for built-in function
 * @fn:         Pointer to the function
 * @minargs:    Minimum number of arguments allowed
 * @maxargs:    <0 if varargs allowed, maximum number of args
 *              (usu.=minargs) if not.
 */
struct qfunc_intl_t {
        void (*fn)(struct qvar_t *ret);
        int minargs;
        int maxargs;
};

struct qobject_handle_t {
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
                        struct qobject_handle_t *h;
                } o;
                struct {
                        struct qvar_t *owner;
                        struct qmarker_t mk;
                } fn;
                double f;
                long long i;
                const struct qfunc_intl_t *fni;
                struct token_t s;
                struct qmarker_t px;
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
struct q_private_t {
        unsigned char charmap[128];
        unsigned char char_xtbl[128];
        unsigned char char_x2tbl[128];
        struct hashtable_t *kw_htbl;
        struct hashtable_t *literals;
        struct qvar_t *gbl; /* "__gbl__" as user sees it */
        struct ns_t *ns_top;
        struct qvar_t pc;
        struct qvar_t *fp; /* "frame pointer" */
        struct qvar_t *sp; /* "stack pointer" */
        struct qvar_t lr;  /* "link register */
        struct qvar_t stack[QSTACKMAX];
};

/* I really hate typing this everywhere */
#define cur_oc  q_.pc.px.oc
#define cur_ns  q_.pc.px.ns

/* script.c */
extern struct q_private_t q_;
extern const char *q_typestr(int magic);
extern const char *q_nameof(struct qvar_t *v);

/* helpers for return value of qlex */
static inline int tok_delim(int t) { return (t >> 8) & 0x7fu; }
static inline int tok_type(int t) { return t & 0x7fu; }
static inline int tok_keyword(int t) { return (t >> 8) & 0x7fu; }

/* builtin.c */
extern struct qvar_t *q_builtin_seek(const char *key);
extern void q_builtin_initlib(void);

/* file.c */
extern void file_push(const char *name);
extern char *next_line(unsigned int flags);

/* err.c */
#define bug() bug__(__FILE__, __LINE__)
#define breakpoint() breakpoint__(__FILE__, __LINE__)
#define bug_on(cond_) do { if (cond_) bug(); } while (0)
extern void qsyntax(const char *msg, ...);
extern void qerr_expected(const char *what);
extern void fail(const char *msg, ...);
extern void warning(const char *msg, ...);
extern void bug__(const char *, int);
extern void breakpoint__(const char *file, int line);

/* eval.c */
extern void q_eval(struct qvar_t *v);

/* exec.c */
extern void exec_script(struct ns_t *ns);
extern void qcall_function(struct qvar_t *fn, struct qvar_t *retval);

/* helpers.c */
extern char *estrdup(const char *s);
extern void *emalloc(size_t size);
extern void * ecalloc(size_t size);
extern int x2bin(int c);
static inline bool isodigit(int c) { return c >= '0' && c <= '7'; }
static inline bool isquote(int c) { return c == '"' || c == '\''; }

/* lex.c */
extern int qlex(void);
extern void q_unlex(void);
extern struct ns_t *prescan(const char *filename);

/* list.c */
extern void list_insert_before(struct list_t *a,
                        struct list_t *b, struct list_t *owner);
extern void list_insert_after(struct list_t *a,
                        struct list_t *b, struct list_t *owner);
extern void list_remove(struct list_t *list);
static inline void list_init(struct list_t *list)
        { list->next = list->prev = list; }
static inline bool list_is_empty(struct list_t *list)
        { return list->next == list; }
static inline struct list_t *
list_prev(struct list_t *list, struct list_t *owner)
        { return list->prev == owner ? NULL : list->next; }
static inline struct list_t *
list_next(struct list_t *list, struct list_t *owner)
        { return list->next == owner ? NULL : list->next; }
static inline void
list_add_tail(struct list_t *list, struct list_t *owner)
        { list_insert_before(list, owner, owner); }
static inline void
list_add_front(struct list_t *list, struct list_t *owner)
        { list_insert_after(list, owner, owner); }
static inline struct list_t *
list_first(struct list_t *list)
        { return list_next(list, list); }
static inline struct list_t *
list_last(struct list_t *list)
        { return list_prev(list, list); }

/* literal.c */
extern char *q_literal(const char *s);
extern void q_literal_free(char *s);

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
extern void qstack_pop(struct qvar_t *to);
extern struct qvar_t *qstack_getpush(void);
extern void qstack_push(struct qvar_t *v);

/* symbol.c */
enum { F_FORCE = 0x1 };
extern struct qvar_t *qsymbol_walk(struct qvar_t *o, unsigned int flags);
extern struct qvar_t *qsymbol_lookup(const char *s, unsigned int flags);

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

#endif /* EGQ_H */
