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

        /* lval is TYPE_EMPTY, rval is this type */
        void (*mov)(struct var_t *, struct var_t *);    /* a = b */
        /*
         * lval is this type, rval could be anything
         * return 0 if success, -1 if not allowed or err
         */
        int (*mov_strict)(struct var_t *, struct var_t *);

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
        struct hashtable_t methods;
        void (*reset)(struct var_t *);
        const struct operator_methods_t *opm;
};

/* Indexed by TYPE_* (max NTYPES_USER-1), located in var.c */
extern struct type_t TYPEDEFS[];

#endif /* EVILCANDY_TYPEDEFS_H */

