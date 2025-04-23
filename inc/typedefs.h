/* quasi-internal code, shared by op.c and var.c */
#ifndef EVILCANDY_TYPEDEFS_H
#define EVILCANDY_TYPEDEFS_H

#include "var.h"

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

typedef struct var_t *(*binary_operator_t)(struct var_t *,
                                           struct var_t *);
typedef struct var_t *(*unary_operator_t)(struct var_t *);

/*
 * Per-type callbacks for mathematical operators, like + or -
 *
 * For binary operations, FloatType code expects either float or integer
 * either a or b.  IntType code expects only integers for both a and b.
 * Remaining types expect their own type for a, and any type for b.
 *
 * For unary operations, type need not be checked.
 */
struct operator_methods_t {
        binary_operator_t pow;    /* new = a ** b */
        binary_operator_t mul;    /* new = a * b */
        binary_operator_t div;    /* new = a / b */
        binary_operator_t mod;    /* new = a % b */
        binary_operator_t add;    /* new = a + b */
        binary_operator_t sub;    /* new = a - b */
        binary_operator_t lshift;  /* new = a << b */
        binary_operator_t rshift;  /* new = a >> b */
        binary_operator_t bit_and; /* new = a & b */
        binary_operator_t bit_or;  /* new = a | b */
        binary_operator_t xor;     /* new = a ^ b (binary not logical) */
        unary_operator_t bit_not;  /* new = ~a */
        unary_operator_t negate;   /* new = -a */
};

struct map_methods_t {
        struct var_t *(*getitem)(struct var_t *d, struct var_t *key);
        int (*setitem)(struct var_t *d,
                       struct var_t *key, struct var_t *item);
        int (*hasitem)(struct var_t *d, struct var_t *key);
};

struct seq_methods_t {
        struct var_t *(*getitem)(struct var_t *, int);
        enum result_t (*setitem)(struct var_t *, int, struct var_t *);
        binary_operator_t cat; /* new = a + b */
        void (*sort)(struct var_t *);
};

/*
 * struct type_inittbl_t - Used for initializing a typedef.
 *      TBLEND declares an end to the table.
 */
#define V_INITTBL(n, cb, m, M) \
        { .name = n, .fn = cb, .minargs = m, .maxargs = M }
#define TBLEND { .name = NULL }
/* XXX: should be called type_methods_t or something */
struct type_inittbl_t {
        const char *name;
        struct var_t *(*fn)(struct vmframe_t *);
        int minargs;
        int maxargs;
};

/**
 * struct type_t - Used to get info about a typedef
 * @name:       Name of the type
 * @freelist:   Used by var.c for memory management. Statically initialize
 *              this to NULL.
 * @n_freelist: Used by var.c for memory management. Statically initialize
 *              this to zero.
 * @methods:    Dictionary of built-in methods for the type; these are
 *              things scripts call as functions.  moduleinit_var()
 *              allocates this and fills it with .cbm entries during
 *              initialization time.
 * @opm:        Callbacks for performing primitive operations like
 *              + or - on type.  This is for numerical operations only.
 *              ('+' for 'cat' is in @sqm.)
 * @cbm:        Array of built-in methods that var_config_type will
 *              put into @methods, or NULL if no such methods exist.
 *              In-language, this looks something like like 'x.method()'.
 * @mpm:        Methods for accessing hash-mapped data, or NULL if none
 *              exist
 * @sqm:        Methods for accessing sequential data, or NULL if none
 *              exist
 * @size:       Size of the type-specific struct to allocate with var_new,
 *              in bytes.
 * @str:        Method that returns a string representation of itself,
 *              in a way that (for most data types) can be re-interpreted
 *              back.  Exceptions are things like functions, where angle
 *              brackets bookend the expression.
 * @cmp:        Returns -1 if a<b, 0 if a==b, >0 if a>b.  For non-
 *              numerical types, a and b are already checked to be the
 *              correct type.  For numbers, a is the correct type, b is
 *              a number which may be either int or float; @cmp will have
 *              to check and make a conversion.
 * @cmpz:       Returns 1 if some kind of zero.
 * reset:       May be NULL.  Destructor for a variable's private data.
 */
struct type_t {
        const char *name;
        struct var_mem_t *freelist;
        size_t n_freelist;
        struct var_t *methods;
        const struct operator_methods_t *opm;
        const struct type_inittbl_t *cbm;
        const struct map_methods_t *mpm;
        const struct seq_methods_t *sqm;
        size_t size;
        struct var_t *(*str)(struct var_t *);
        int (*cmp)(struct var_t *, struct var_t *);
        bool (*cmpz)(struct var_t *);    /* a == 0 ? */
        void (*reset)(struct var_t *);
};

/*
 * Declared in type C modules in types/xxx.c
 * Only put these here and give them extern linkage if they are meaningful
 * outside of whatever module that uses them.  Otherwise, keep them local
 * to the module so the namespace doesn't get cluttered up.
 */
extern struct type_t ArrayType;
extern struct type_t TupleType;
extern struct type_t EmptyType; /* XXX should be NullType */
extern struct type_t FloatType;
extern struct type_t FunctionType;
extern struct type_t IntType;
extern struct type_t XptrType;
extern struct type_t DictType;
extern struct type_t StringType;
extern struct type_t BytesType;
extern struct type_t RangeType;
extern struct type_t UuidptrType;
extern struct type_t FileType;

static inline bool isvar_array(struct var_t *v)
        { return v->v_type == &ArrayType; }
static inline bool isvar_tuple(struct var_t *v)
        { return v->v_type == &TupleType; }
static inline bool isvar_empty(struct var_t *v)
        { return v->v_type == &EmptyType; }
static inline bool isvar_float(struct var_t *v)
        { return v->v_type == &FloatType; }
static inline bool isvar_function(struct var_t *v)
        { return v->v_type == &FunctionType; }
static inline bool isvar_int(struct var_t *v)
        { return v->v_type == &IntType; }
static inline bool isvar_xptr(struct var_t *v)
        { return v->v_type == &XptrType; }
static inline bool isvar_dict(struct var_t *v)
        { return v->v_type == &DictType; }
static inline bool isvar_string(struct var_t *v)
        { return v->v_type == &StringType; }
static inline bool isvar_bytes(struct var_t *v)
        { return v->v_type == &BytesType; }
static inline bool isvar_range(struct var_t *v)
        { return v->v_type == &RangeType; }
static inline bool isvar_uuidptr(struct var_t *v)
        { return v->v_type == &UuidptrType; }
static inline bool isvar_file(struct var_t *v)
        { return v->v_type == &FileType; }

/* not 'isvar_num'... there always has to be an odd one out */
static inline bool isnumvar(struct var_t *v)
        { return v->v_type->opm != NULL; }
static inline bool isvar_seq(struct var_t *v)
        { return v->v_type->sqm != NULL; }
static inline bool isvar_map(struct var_t *v)
        { return v->v_type->mpm != NULL; }
static inline bool hasvar_len(struct var_t *v)
        { return isvar_seq(v) || isvar_map(v); }

/*
 * Made public so intvar_toll and floatvar_tod can be inline.
 * These are otherwise used privately in integer.c and float.c
 */
struct intvar_t {
        struct var_t base;
        long long i;
};
struct floatvar_t {
        struct var_t base;
        double f;
};

/* Warning!! Only call these if you already type-checked @v */
static inline double floatvar_tod(struct var_t *v)
        { return ((struct floatvar_t *)v)->f; }
static inline long long intvar_toll(struct var_t *v)
        { return ((struct intvar_t *)v)->i; }
static inline long long numvar_toint(struct var_t *v)
        { return isvar_int(v) ? intvar_toll(v) : (long long)floatvar_tod(v); }
static inline double numvar_tod(struct var_t *v)
        { return isvar_float(v) ? floatvar_tod(v) : (double)intvar_toll(v); }

#endif /* EVILCANDY_TYPEDEFS_H */

