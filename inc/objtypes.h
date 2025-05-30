/*
 * quasi-internal code, shared by op.c, var.c, and the sources for each
 * object class in types/
 */
#ifndef EVILCANDY_OBJTYPES_H
#define EVILCANDY_OBJTYPES_H

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

typedef Object *(*binary_operator_t)(Object *, Object *);
typedef Object *(*unary_operator_t)(Object *);

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
        unary_operator_t abs;      /* new = abs(a) */
};

struct map_methods_t {
        Object *(*getitem)(Object *d, Object *key);
        int (*setitem)(Object *d,
                       Object *key, Object *item);
        /* @d is this type; @key must be type-checked */
        int (*hasitem)(Object *d, Object *key);
        /*
         * Not an in-place operation.  Make shallow copy of lval
         * and fill it--possibly overriding it--with rval.
         */
        binary_operator_t mpunion;
};

/**
 * struct seq_fastiter_t - Specialized implementations for min(), max(),
 *                         any(), and all().
 *
 * If the .fast_iter field in a type's .sqm field is non-NULL, then all
 * of these fields must be non-NULL, even if the procedure would be
 * trivial.  This is intended for sequential classes which have large
 * arrays of raw data (eg. bytes and 'floats' arrays) rather than small
 * arrays of pointers to Objects (eg. lists and tuples), and therefore
 * can run these algorithms much more quickly than the general-purpose
 * algorithm in var.c.  It comes at the cost of a slight DRY violation,
 * so most classes will not use this.
 */
struct seq_fastiter_t {
        /* set error if size=0 */
        Object *(*max)(Object *);
        Object *(*min)(Object *);
        /* return true or false regardless of size */
        bool (*any)(Object *);
        bool (*all)(Object *);
};

struct seq_methods_t {
        Object *(*getitem)(Object *, int);
        /* @haystack is this type; @needle must be type-checked */
        bool (*hasitem)(Object *haystack, Object *needle);
        enum result_t (*setitem)(Object *, int, Object *);
        Object *(*getslice)(Object *, int start, int stop, int step);
        enum result_t (*setslice)(Object *, int start, int stop, int step, Object *);
        /* new = a + b; if b is NULL, return new empty var */
        binary_operator_t cat;
        void (*sort)(Object *);
        const struct seq_fastiter_t *fast_iter;
};

/*
 * struct type_inittbl_t - Used for initializing a built-in function.
 *      TBLEND declares an end to the table.
 */
#define V_INITTBL(n, cb, m, M, o, k)    \
        { .name = n, .fn = cb,          \
          .minargs = m, .maxargs = M,   \
          .optind = o, .kwind = k }

#define TBLEND { .name = NULL }

/* XXX: should be called builtin_func_tbl_t or something */
struct type_inittbl_t {
        const char *name;
        Object *(*fn)(Frame *);
        int minargs;
        int maxargs;
        int optind;
        int kwind;
};

/* see struct type_t below, @prop_getsets */
struct type_prop_t {
        const char *name;
        Object *(*getprop)(Object *self);
        enum result_t (*setprop)(Object *self, Object *value);
};

/* .flags field in struct type_t */
enum {
        OBF_NUMBER      = 0x01,
        OBF_REAL        = 0x02,
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
 *              ('+' for 'cat' is in @sqm.)  DO NOT SET THIS UNLESS YOU
 *              CAN ADD, SUBTRACT, ETC., WITH FLOAT, INTEGERS, ETC.
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
 * @reset:      May be NULL.  Destructor for a variable's private data.
 * @prop_getsets: Array of property getters/setters.  Its .setprop or
 *              .getprop fields may be NULL in the case of read-only or
 *              write-only properties.  This may be NULL if there are no
 *              built-in properties for the type; if not NULL, the array
 *              must be terminated with an item whose .name is NULL.
 */
struct type_t {
        unsigned int flags;
        const char *name;
        struct var_mem_t *freelist;
        size_t n_freelist;
        Object *methods;
        const struct operator_methods_t *opm;
        const struct type_inittbl_t *cbm;
        const struct map_methods_t *mpm;
        const struct seq_methods_t *sqm;
        size_t size;
        Object *(*str)(Object *);
        int (*cmp)(Object *, Object *);
        bool (*cmpz)(Object *);    /* a == 0 ? */
        void (*reset)(Object *);
        const struct type_prop_t *prop_getsets;
};

/*
 * Syntactic sugar to get the name of the XxxType, useful for
 * debugging and error messages.
 */
static inline const char *typestr(Object *v) { return v->v_type->name; }

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
extern struct type_t ComplexType;
extern struct type_t FunctionType;
extern struct type_t MethodType;
extern struct type_t IntType;
extern struct type_t XptrType;
extern struct type_t DictType;
extern struct type_t StringType;
extern struct type_t BytesType;
extern struct type_t PropertyType;
extern struct type_t RangeType;
extern struct type_t UuidptrType;
extern struct type_t IdType;
extern struct type_t FileType;
extern struct type_t FloatsType;
extern struct type_t StarType;

static inline bool isvar_array(Object *v)
        { return v->v_type == &ArrayType; }
static inline bool isvar_tuple(Object *v)
        { return v->v_type == &TupleType; }
static inline bool isvar_empty(Object *v)
        { return v->v_type == &EmptyType; }
static inline bool isvar_float(Object *v)
        { return v->v_type == &FloatType; }
static inline bool isvar_complex(Object *v)
        { return v->v_type == &ComplexType; }
static inline bool isvar_function(Object *v)
        { return v->v_type == &FunctionType; }
static inline bool isvar_method(Object *v)
        { return v->v_type == &MethodType; }
static inline bool isvar_int(Object *v)
        { return v->v_type == &IntType; }
static inline bool isvar_xptr(Object *v)
        { return v->v_type == &XptrType; }
static inline bool isvar_dict(Object *v)
        { return v->v_type == &DictType; }
static inline bool isvar_string(Object *v)
        { return v->v_type == &StringType; }
static inline bool isvar_bytes(Object *v)
        { return v->v_type == &BytesType; }
static inline bool isvar_range(Object *v)
        { return v->v_type == &RangeType; }
static inline bool isvar_uuidptr(Object *v)
        { return v->v_type == &UuidptrType; }
static inline bool isvar_file(Object *v)
        { return v->v_type == &FileType; }
static inline bool isvar_property(Object *v)
        { return v->v_type == &PropertyType; }
static inline bool isvar_floats(Object *v)
        { return v->v_type == &FloatsType; }
static inline bool isvar_star(Object *v)
        { return v->v_type == &StarType; }

static inline bool isvar_number(Object *v)
        { return !!(v->v_type->flags & OBF_NUMBER); }
static inline bool isvar_real(Object *v)
        { return isvar_int(v) || isvar_float(v); }
        // { return !!(v->v_type->flags & OBF_REAL); }
static inline bool isvar_seq(Object *v)
        { return v->v_type->sqm != NULL; }
static inline bool isvar_seq_readable(Object *v)
        { return isvar_seq(v) && v->v_type->sqm->getitem != NULL; }
static inline bool isvar_map(Object *v)
        { return v->v_type->mpm != NULL; }
static inline bool hasvar_len(Object *v)
        { return isvar_seq(v) || isvar_map(v); }

/*
 * Some objects made public so some functions can be inline.
 * These are otherwise used privately in integer.c and float.c
 */
struct intvar_t {
        Object base;
        long long i;
};
struct floatvar_t {
        Object base;
        double f;
};

struct arrayvar_t {
        struct seqvar_t base;
        Object **items;
        int lock;
        size_t alloc_size;
};

struct tuplevar_t {
        struct seqvar_t base;
        Object **items;
};

/*
 * Do not confuse 'floats' with 'float'!
 * This is the array, the one above is the single item.
 */
struct floatsvar_t {
        struct seqvar_t base;
        double *data;
};

struct bytesvar_t {
        struct seqvar_t base;
        unsigned char *b_buf;
};

struct stringvar_t {
        struct seqvar_t base;
        char *s;        /* the actual C string */
        size_t s_ascii_len;
        void *s_unicode;
        size_t s_width;
        size_t s_enc_len;
        int s_ascii;
        hash_t s_hash;
};


/* Warning!! Only call these if you already type-checked @v */
static inline double floatvar_tod(Object *v)
        { return ((struct floatvar_t *)v)->f; }
static inline long long intvar_toll(Object *v)
        { return ((struct intvar_t *)v)->i; }
static inline long long realvar_toint(Object *v)
        { return isvar_int(v) ? intvar_toll(v) : (long long)floatvar_tod(v); }
static inline double realvar_tod(Object *v)
        { return isvar_float(v) ? floatvar_tod(v) : (double)intvar_toll(v); }
static inline Object **array_get_data(Object *v)
        { return ((struct arrayvar_t *)v)->items; }
static inline Object **tuple_get_data(Object *v)
        { return ((struct tuplevar_t *)v)->items; }
static inline double *floats_get_data(Object *v)
        { return ((struct floatsvar_t *)v)->data; }
static inline unsigned char *bytes_get_data(Object *v)
        { return ((struct bytesvar_t *)v)->b_buf; }
extern int intvar_toi(Object *v);

/* only call if isvar_seq_readable() is true */
static inline Object *seqvar_getitem(Object *v, int i)
        { return v->v_type->sqm->getitem(v, i); }

/*
 * string helpers - Only call these if you already type-checked @v
 */
static inline hash_t string_hash(Object *v)
        { return ((struct stringvar_t *)v)->s_hash; }

/* may be different from seqvar_size if not entirely ASCII */
static inline size_t string_nbytes(Object *v)
        { return ((struct stringvar_t *)v)->s_ascii_len; }
static inline bool string_isascii(Object *v)
        { return !!((struct stringvar_t *)v)->s_ascii; }

static inline const char *
string_cstring(Object *v)
{
        bug_on(!isvar_string(v));
        return ((struct stringvar_t *)v)->s;
}

#endif /* EVILCANDY_OBJTYPES_H */

