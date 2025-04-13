/* quasi-internal code, shared by op.c and var.c */
#ifndef EVILCANDY_TYPEDEFS_H
#define EVILCANDY_TYPEDEFS_H

#include <evilcandy.h>

typedef struct var_t *(*binary_operator_t)(struct var_t *,
                                           struct var_t *);

/*
 * Per-type callbacks for mathematical operators, like + or -
 */
struct operator_methods_t {
        binary_operator_t mul;    /* new = a * b */
        binary_operator_t div;    /* new = a / b */
        binary_operator_t mod;    /* new = a % b */
        binary_operator_t add;    /* new = a + b */
        binary_operator_t sub;    /* new = a - b */

        /* <0 if a<b, 0 if a==b, >0 if a>b, doesn't set a or b */
        int (*cmp)(struct var_t *, struct var_t *);

        binary_operator_t lshift;  /* new = a << b */
        binary_operator_t rshift;  /* new = a >> b */
        binary_operator_t bit_and; /* new = a & b */
        binary_operator_t bit_or;  /* new = a | b */
        binary_operator_t xor;     /* new = a ^ b */
        bool (*cmpz)(struct var_t *);    /* a == 0 ? */
        void (*incr)(struct var_t *);    /* a++ (in place) */
        void (*decr)(struct var_t *);    /* a-- (in place) */
        struct var_t *(*bit_not)(struct var_t *); /* new = ~a */
        struct var_t *(*negate)(struct var_t *);  /* new = -a */

        /*
         * Copy a variable
         *      If it's a by-ref type, just produce a reference
                        and return yourself.
         *      If it's a by-val type, copy all the data except the refcount.
         *
         * Use var_new(), NOT just malloc/memcpy, which will break things.
         *
         * The only error would be out-of-memory which is always trapped,
         * so this should ALWAYS return a successful copy.
         */
        struct var_t *(*cp)(struct var_t *);

        /*
         * "You're about to be deleted, so delete your private
         * data before it gets zombified."
         */
        void (*reset)(struct var_t *);
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
 * @methods:    Hash table of built-in methods for the type; these are
 *              things scripts call as functions. moduleinit_var() fills
 *              in this hashtable during initialization time.
 * @opm:        Callbacks for performing primitive operations like
 *              + or - on type.  This may not be NULL, however any of its
 *              fields may be NULL.
 * @cbm:        Array of built-in methods that var_config_type will
 *              put into @methods, or NULL if no such methods exist.
 */
struct type_t {
        const char *name;
        struct hashtable_t methods;
        const struct operator_methods_t *opm;
        const struct type_inittbl_t *cbm;
        size_t size;
};

/*
 * Declared in type C modules in types/xxx.c
 * Only put these here and give them extern linkage if they are meaningful
 * outside of whatever module that uses them.  Otherwise, keep them local
 * to the module so the namespace doesn't get cluttered up.
 */
extern struct type_t ArrayType;
extern struct type_t EmptyType; /* XXX should be NullType */
extern struct type_t FloatType;
extern struct type_t FunctionType;
extern struct type_t IntType;
extern struct type_t XptrType;
extern struct type_t ObjectType;
extern struct type_t StringType;
extern struct type_t UuidptrType;

static inline bool isvar_array(struct var_t *v)
        { return v->v_type == &ArrayType; }
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
static inline bool isvar_object(struct var_t *v)
        { return v->v_type == &ObjectType; }
#define isvar_dict isvar_object
static inline bool isvar_string(struct var_t *v)
        { return v->v_type == &StringType; }
static inline bool isvar_uuidptr(struct var_t *v)
        { return v->v_type == &UuidptrType; }

static inline bool isnumvar(struct var_t *v)
        { return isvar_int(v) || isvar_float(v); }

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
struct xptrvar_t {
        struct var_t base;
        struct executable_t *xptr;
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
static inline struct executable_t *xptrvar_tox(struct var_t *v)
        { return ((struct xptrvar_t *)v)->xptr; }

#endif /* EVILCANDY_TYPEDEFS_H */

