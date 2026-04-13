#ifndef EVC_INC_INTERNAL_TYPE_PROTOCOL_H
#define EVC_INC_INTERNAL_TYPE_PROTOCOL_H

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
};

struct seq_methods_t {
        Object *(*getitem)(Object *, size_t);
        /* @haystack is this type; @needle must be type-checked */
        bool (*hasitem)(Object *haystack, Object *needle);
        enum result_t (*setitem)(Object *, size_t, Object *);
        Object *(*getslice)(Object *, ssize_t start,
                            ssize_t stop, ssize_t step);
        enum result_t (*setslice)(Object *, ssize_t start,
                                  ssize_t stop, ssize_t step, Object *);
        /* new = a + b; if b is NULL, return new empty var */
        binary_operator_t cat;
        enum result_t (*sort)(Object *);
};

/**
 * struct type_method_t - Used for initializing a function meant to be
 *                        visible to the UAPI, usu. as an attribute to
 *                        a built-in type.
 * @name:  Name of the function as user will see it.
 * @fn:    Function to call and pass the frame to.
 */
struct type_method_t {
        const char *name;
        Object *(*fn)(Frame *);
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
 * @cmp:        Returns RES_ERROR and throws/propogates exception if error;
 *              otherwise, stores in result arg: negative number if a < b,
 *              0 if a==b, positive nonzero number if a > b.  For non-
 *              numerical types, a and b are already checked to be the
 *              correct type.  If NULL, it means "</>/<=/>= not permitted
 *              for this type".  If these comparisons are not permitted
 *              but '==' is permitted, leave this NULL and fill .cmpeq
 *              instead.
 * @cmpz:       Returns 1 if some kind of zero.
 * @cmpeq:      Return true if a and b match, false otherwise.  Do not
 *              raise an exception.  If this is NULL, match results will
 *              be reduced to a simple pointer match.
 * @reset:      May be NULL.  Destructor for a variable's private data.
 * @prop_getsets: Array of property getters/setters.  Its .setprop or
 *              .getprop fields may be NULL in the case of read-only or
 *              write-only properties.  This may be NULL if there are no
 *              built-in properties for the type; if not NULL, the array
 *              must be terminated with an item whose .name is NULL.
 * @create:     May be NULL.  Built-in function callback to create a new
 *              instance. The frame arguments are 0: list containing the
 *              va args, and 1: dictionary containing the keyword args.
 *              Each type may have its own rules about how many va args
 *              if any are necessary.
 * @hash:       Function to hash object, or NULL if type is not hashable.
 *              Hash function should not throw exceptions; rather return
 *              HASH_ERROR if object is not hashable.
 * @get_iter:   Function to return an iterator, whose private fields
 *              are initialized in whatever way prepares it for the first
 *              call to .next().  IF THIS FIELD IS NON-NULL, OBJECT HEAD
 *              MUST BE struct seqvar_t!!
 */
struct type_t {
        unsigned int flags;
        const char *name;
        struct var_mem_t *freelist;
        size_t n_freelist;
        Object *methods;
        const struct operator_methods_t *opm;
        const struct type_method_t *cbm;
        const struct map_methods_t *mpm;
        const struct seq_methods_t *sqm;
        size_t size;
        Object *(*str)(Object *);
        enum result_t (*cmp)(Object *, Object *, int *result);
        bool (*cmpz)(Object *);    /* a == 0 ? */
        bool (*cmpeq)(Object *, Object *);
        void (*reset)(Object *);
        const struct type_prop_t *prop_getsets;
        Object *(*create)(Frame *fr);
        hash_t (*hash)(Object *);
        Object *(*iter_next)(Object *);
        Object *(*get_iter)(Object *);
};

/*
 * Syntactic sugar to get the name of the XxxType, useful for
 * debugging and error messages.
 */
static inline const char *typestr(Object *v) { return v->v_type->name; }

#endif /* EVC_INC_INTERNAL_TYPE_PROTOCOL_H */
