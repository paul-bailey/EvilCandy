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

/**
 * struct type_t - Used to get info about a typedef
 * @name:       Name of the type
 * @methods:    Linked list of built-in methods for the type; these are
 *              things scripts call as functions. var_config_type() fills
 *              in this hashtable during initialization time.
 * @opm:        Callbacks for performing primitive operations like
 *              + or - on type
 */
struct type_t {
        const char *name;
        struct hashtable_t methods;
        const struct operator_methods_t *opm;
};

/* Indexed by TYPE_* (max NTYPES_USER-1), located in var.c */
extern struct type_t TYPEDEFS[];

#endif /* EVILCANDY_TYPEDEFS_H */

